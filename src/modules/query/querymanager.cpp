// Executes SQLite queries on worker thread for paginated item loading and
// filtering.
#include "querymanager.h"
#include "collectionutils.h"
#include "dbmigrations.h"
#include "errorutils.h"
#include "pathutils.h"
#include "querymanagerhelpers.h"
#include "sessionmanager.h"
#include "uiconstants.h"
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

QueryManager::QueryManager(SessionManager *sessionManager, const QString &connectionName,
                           QObject *parent)
    : QObject(parent), m_scanCancellationToken(std::make_shared<std::atomic_bool>(false)),
      m_sessionManager(sessionManager), m_connectionName(connectionName) {
  // Pointer-based cache with automatic LRU eviction.
  m_statementCache.setMaxCost(MAX_STATEMENT_CACHE_SIZE);

  const int idealThreads = QThread::idealThreadCount();
  const int base = idealThreads > 0 ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
                                    : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_scanThreadPool = new QThreadPool();
  m_scanThreadPool->setMaxThreadCount(
      std::clamp(base, UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                 UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));

  // Register ErrorContext for queued signal/slot connections
  qRegisterMetaType<ErrorUtils::ErrorContext>("ErrorUtils::ErrorContext");
}

QueryManager::~QueryManager() {
  // Abandon the thread pool without waiting - process is exiting anyway.
  if (m_scanThreadPool) {
    m_scanThreadPool->clear();
    // Intentionally NOT deleting - ~QThreadPool blocks. Let OS clean up.
    m_scanThreadPool = nullptr;
  }

  clearStatementCache();
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }
}

void QueryManager::requestCancelScan() {
  if (m_scanCancellationToken) {
    m_scanCancellationToken->store(true, std::memory_order_release);
  }
  // Drop any queued scan work that hasn't started yet.
  if (m_scanThreadPool) {
    m_scanThreadPool->clear();
  }
}

bool QueryManager::isScanCancelled() const {
  return m_scanCancellationToken && m_scanCancellationToken->load(std::memory_order_acquire);
}

void QueryManager::resetScanCancellation() {
  m_scanCancellationToken = std::make_shared<std::atomic_bool>(false);
}

// Forces the connection to see the latest WAL commits from other connections.
// In SQLite WAL mode, a connection can hold onto a stale read snapshot if it
// has an open transaction or cached statements. This method ensures we see
// data committed by the scan worker running on a separate connection.

// ... Helper implementations ...

// SQL constants for prepared statement caching live in a shared header so
// sibling translation units (querymanagerlifecycle.cpp) can reuse them.
#include "querymanagersql.h"
