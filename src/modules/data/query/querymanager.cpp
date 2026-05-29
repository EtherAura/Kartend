// Executes SQLite queries on worker thread for paginated item loading and
// filtering.
#include "querymanager.h"
#include "collectiontypes.h"
#include "dbmigrations.h"
#include "errorutils.h"
#include "isessionmanager.h"
#include "pathutils.h"
#include "querymanagerhelpers.h"
#include <algorithm>
#include <atomic>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QMutex>
#include <QRegularExpression>
#include <QRunnable>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QtGlobal>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QVector>
#include <QWaitCondition>
#include <random>
#include <stdexcept>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcQueryManager, "kartend.querymanager")
#define debugLog(msg) qCDebug(lcQueryManager) << msg

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using namespace QueryManagerInternal;

QueryManager::QueryManager(ISessionManager *sessionManager, const QString &connectionName,
                           QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager), m_connectionName(connectionName) {
  // Register ErrorContext for queued signal/slot connections
  qRegisterMetaType<ErrorUtils::ErrorContext>("ErrorUtils::ErrorContext");

  // Forward the scan subsystem's signals so external listeners (DatabaseManager)
  // keep connecting to QueryManager unchanged. m_scanService is a value member
  // of QueryManager so both objects live on the same thread (the database
  // worker thread); Qt::DirectConnection is explicit so this stays correct if
  // a future maintainer adds a moveToThread for m_scanService — the build
  // would still link but the connection would route through the worker
  // thread's event loop, and we'd want a deliberate switch to QueuedConnection
  // at that point.
  connect(&m_scanService, &ScanService::errorOccurred, this, &QueryManager::errorOccurred,
          Qt::DirectConnection);
  connect(&m_scanService, &ScanService::scanStarting, this, &QueryManager::scanStarting,
          Qt::DirectConnection);
  connect(&m_scanService, &ScanService::scanItemsProgress, this, &QueryManager::scanItemsProgress,
          Qt::DirectConnection);
  connect(&m_scanService, &ScanService::collectionScanCompleted, this,
          &QueryManager::collectionScanCompleted, Qt::DirectConnection);
  connect(&m_scanService, &ScanService::collectionScanSummary, this,
          &QueryManager::collectionScanSummary, Qt::DirectConnection);
}

QueryManager::~QueryManager() {
  clearStatementCache();
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }
}

void QueryManager::requestCancelScan() {
  m_scanService.requestCancelScan();
}

bool QueryManager::isScanCancelled() const {
  return m_scanService.isScanCancelled();
}

void QueryManager::resetScanCancellation() {
  m_scanService.resetScanCancellation();
}

// Forces the connection to see the latest WAL commits from other connections.
// In SQLite WAL mode, a connection can hold onto a stale read snapshot if it
// has an open transaction or cached statements. This method ensures we see
// data committed by the scan worker running on a separate connection.

// ... Helper implementations ...

// SQL constants for prepared statement caching live in a shared header so
// sibling translation units (querymanagerlifecycle.cpp) can reuse them.
#include "querymanagersql.h"
