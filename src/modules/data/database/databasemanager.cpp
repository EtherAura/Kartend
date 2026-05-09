// Coordinates SQLite database access via worker thread for collection metadata
// queries.
#include <functional>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtGlobal>
#include <QThread>
#include <QTimer>
#include <stdexcept>

#include "collectionutils.h"
#include "databasemanager.h"
#include "dbmigrations.h"
#include "errorutils.h"
#include "loggingcategories.h"
#include "pathutils.h"
#include "querymanager.h"
#include "sessionmanager.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcDatabaseManager, "kartend.databasemanager")
#define debugLog(msg) qCDebug(lcDatabaseManager) << msg

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

// Construct the database manager and initialize the database
DatabaseManager::DatabaseManager(SessionManager *sessionManager, QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager),
      m_connectionName(QStringLiteral("kartend_main")) {
  qRegisterMetaType<CollectionConfig>("CollectionConfig");
  qRegisterMetaType<CollectionContext>("CollectionContext");
  qRegisterMetaType<QList<CollectionConfig>>("QList<CollectionConfig>");
  qRegisterMetaType<QHash<QString, qint64>>("QHash<QString, qint64>");

  initDatabase();

  // NOTE: QThreads are intentionally NOT parented to DatabaseManager. If they
  // were, ~QObject would auto-delete them mid-run during shutdown, and
  // ~QThread qFatals when destroyed while running. The destructor handles
  // bounded shutdown explicitly.
  m_workerThread = new QThread();
  m_worker = new QueryManager(m_sessionManager, QStringLiteral("kartend_query_worker"));
  m_worker->moveToThread(m_workerThread);

  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

  // Dedicated scan worker (separate thread + separate DB connection) so
  // long scans don't block query operations and UI updates.
  m_scanThread = new QThread();
  m_scanWorker = new QueryManager(m_sessionManager, QStringLiteral("kartend_scan_worker"));
  m_scanWorker->moveToThread(m_scanThread);
  connect(m_scanThread, &QThread::finished, m_scanWorker, &QObject::deleteLater);

  connect(this, &DatabaseManager::requestLoadAllCollections, m_worker,
          &QueryManager::loadAllCollections);
  connect(this, &DatabaseManager::requestLoadItems, m_worker, &QueryManager::loadItems);
  connect(this, &DatabaseManager::requestLoadItemsWithSubcollections, m_worker,
          &QueryManager::loadItemsWithSubcollections);
  connect(this, &DatabaseManager::requestFetchItemCount, m_worker,
          &QueryManager::fetchItemCountWithToken);
  connect(this, &DatabaseManager::requestFetchItemsRange, m_worker, &QueryManager::fetchItemsRange);
  connect(this, &DatabaseManager::requestFetchVisualIndexForPath, m_worker,
          &QueryManager::fetchVisualIndexForPath);
  connect(this, &DatabaseManager::requestInvalidateCache, m_worker,
          &QueryManager::invalidateCollectionCache);
  connect(this, &DatabaseManager::requestUpdateCachedCounts, m_worker,
          &QueryManager::updateCachedCounts);

  // Background scanning is handled by the scan worker.
  connect(this, &DatabaseManager::requestEnsureScannedForContext, m_scanWorker,
          &QueryManager::ensureScannedForContext);

  // Lazy background FTS backfill is handled by the scan worker.
  connect(this, &DatabaseManager::requestEnsureItemsFtsReady, m_scanWorker,
          &QueryManager::ensureItemsFtsReady);

  connect(m_worker, &QueryManager::itemsLoaded, this, &DatabaseManager::onWorkerItemsLoaded);
  connect(m_worker, &QueryManager::itemCountLoaded, this,
          &DatabaseManager::onWorkerItemCountLoaded);
  connect(m_worker, &QueryManager::itemCountLoadedWithToken, this,
          &DatabaseManager::onWorkerItemCountLoadedWithToken);
  connect(m_worker, &QueryManager::itemsRangeLoaded, this,
          &DatabaseManager::onWorkerItemsRangeLoaded);
  connect(m_worker, &QueryManager::visualIndexForPathLoaded, this,
          &DatabaseManager::visualIndexForPathLoaded);
  connect(m_worker, &QueryManager::cachedCountsComputed, this,
          &DatabaseManager::onWorkerCachedCountsComputed);
  connect(m_worker, &QueryManager::errorOccurred, this, &DatabaseManager::errorOccurred);
  // Query worker should remain responsive; scan signals come from scan worker.
  connect(m_worker, &QueryManager::cacheInvalidated, this, &DatabaseManager::cacheInvalidated);

  connect(m_scanWorker, &QueryManager::errorOccurred, this, &DatabaseManager::errorOccurred);
  connect(m_scanWorker, &QueryManager::scanProgress, this, &DatabaseManager::scanProgress);
  connect(m_scanWorker, &QueryManager::scanStarting, this, &DatabaseManager::scanStarting);
  connect(m_scanWorker, &QueryManager::scanItemsProgress, this,
          &DatabaseManager::scanItemsProgress);
  connect(m_scanWorker, &QueryManager::collectionScanCompleted, this,
          &DatabaseManager::collectionScanCompleted);
  connect(m_scanWorker, &QueryManager::collectionScanSummary, this,
          &DatabaseManager::collectionScanSummary);

  m_cachedCountsUpdateTimer = new QTimer(this);
  m_cachedCountsUpdateTimer->setSingleShot(true);
  connect(m_cachedCountsUpdateTimer, &QTimer::timeout, this,
          &DatabaseManager::dispatchCachedCountsUpdate);

  m_workerThread->start();
  m_scanThread->start();

  // Defer FTS backfill until after the event loop starts so startup UI is not
  // blocked by any optional maintenance work.
  QTimer::singleShot(0, this, [this] { emit requestEnsureItemsFtsReady(); });
}

// Destroy the database manager and close/remove the connection
DatabaseManager::~DatabaseManager() {
  // Cancel any in-flight scans so the workers can return promptly.
  if (m_worker) {
    m_worker->requestCancelScan();
  }
  if (m_scanWorker) {
    m_scanWorker->requestCancelScan();
  }

  // Quit + bounded wait. If the worker thread doesn't return within the
  // budget, intentionally leak it (set pointer to nullptr) rather than let
  // ~QThread qFatal on a still-running thread. The OS will reclaim threads
  // and SQLite handles at process exit (we use std::quick_exit in main).
  constexpr int SHUTDOWN_WAIT_MS = 2000;
  if (m_workerThread) {
    m_workerThread->quit();
    if (m_workerThread->wait(SHUTDOWN_WAIT_MS)) {
      delete m_workerThread;
    }
    // else: leak intentionally to avoid ~QThread qFatal
    m_workerThread = nullptr;
  }

  if (m_scanThread) {
    m_scanThread->quit();
    if (m_scanThread->wait(SHUTDOWN_WAIT_MS)) {
      delete m_scanThread;
    }
    // else: leak intentionally
    m_scanThread = nullptr;
  }

  // Close database connection - this is fast and safe
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }
}

// Initialize the sqlite database and ensure uuid-based identity with
// de-duplication
void DatabaseManager::initDatabase() {
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }

  m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
  QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!QDir().mkpath(dbPath)) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to create database directory",
                                      "DatabaseManager::initDatabase")
                   .withDetails(QString("Path: %1").arg(dbPath));
    ErrorUtils::logError(err);
    return;
  }
  m_db.setDatabaseName(dbPath + "/media.db");

  if (!m_db.open()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to open database", "DatabaseManager::initDatabase")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    return;
  }

  QSqlQuery query(m_db);
  if (!query.exec("PRAGMA foreign_keys = ON")) {
    auto err =
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to enable foreign keys",
                              "DatabaseManager::initDatabase")
            .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  if (!query.exec("PRAGMA journal_mode = WAL")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to enable WAL mode, falling back to DELETE mode",
                                     "DatabaseManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
    // Fall back to DELETE journal mode for safer operation on systems
    // that don't support WAL (e.g., network filesystems, older SQLite)
    if (!query.exec("PRAGMA journal_mode = DELETE")) {
      auto fallbackErr =
          ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to set DELETE journal mode",
                                "DatabaseManager::initDatabase")
              .withDetails(query.lastError().text());
      ErrorUtils::logError(fallbackErr);
    }
  }
  // Set busy timeout to wait up to 30 seconds for locks to be released -
  // prevents "database is locked" errors during concurrent access
  const QString busyTimeoutPragma =
      QStringLiteral("PRAGMA busy_timeout = %1").arg(UIConstants::Database::MAIN_BUSY_TIMEOUT_MS);
  if (!query.exec(busyTimeoutPragma)) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to set busy timeout",
                                     "DatabaseManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }

  QString collectionsTable = "CREATE TABLE IF NOT EXISTS collections ("
                             "id INTEGER PRIMARY KEY, "
                             "name TEXT NOT NULL, "
                             "last_scanned TEXT NOT NULL, "
                             "ext_signature TEXT DEFAULT '', "
                             "uuid TEXT DEFAULT ''"
                             ")";
  if (!query.exec(collectionsTable)) {
    auto err =
        ErrorContext::critical(ErrorCode::DatabaseQueryFailed, "Failed to create collections table",
                               "DatabaseManager::initDatabase")
            .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }

  QString itemsTable = "CREATE TABLE IF NOT EXISTS items ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "collection_id INTEGER NOT NULL, "
                       "path TEXT NOT NULL, "
                       "name TEXT NOT NULL, "
                       "artwork_path TEXT, "
                       "last_modified TEXT NOT NULL, "
                       "file_size INTEGER DEFAULT 0, "
                       "play_count INTEGER DEFAULT 0, "
                       "last_played TEXT, "
                       "rating INTEGER DEFAULT 0, "
                       "collection_uuid TEXT DEFAULT '', "
                       "UNIQUE(collection_id, path), "
                       "FOREIGN KEY(collection_id) REFERENCES collections(id) ON DELETE CASCADE"
                       ")";
  if (!query.exec(itemsTable)) {
    auto err =
        ErrorContext::critical(ErrorCode::DatabaseQueryFailed, "Failed to create items table",
                               "DatabaseManager::initDatabase")
            .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }

  // Ensure any schema upgrades are applied after core tables exist.
  DbMigrations::applySchemaMigrations(m_db, QStringLiteral("DatabaseManager::initDatabase"));

  QSqlQuery idx(m_db);
  idx.prepare("CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)");
  idx.exec();

  idx.prepare("CREATE INDEX IF NOT EXISTS idx_items_collection_uuid ON "
              "items(collection_uuid)");
  idx.exec();

  idx.prepare("CREATE UNIQUE INDEX IF NOT EXISTS uniq_items_uuid_path ON "
              "items(collection_uuid, path)");
  idx.exec();

  // Index on name for search filtering and ORDER BY name COLLATE NOCASE
  idx.prepare("CREATE INDEX IF NOT EXISTS idx_items_name ON items(name COLLATE "
              "NOCASE)");
  idx.exec();

  // Composite index for common query pattern: WHERE collection_uuid IN (...)
  // ORDER BY name This covers filtering by collection UUID and sorting by name
  // in a single index scan
  idx.prepare("CREATE INDEX IF NOT EXISTS idx_items_uuid_name ON "
              "items(collection_uuid, name COLLATE NOCASE)");
  idx.exec();

  // Covering index for paginated queries: includes path to avoid table lookup
  // Query pattern: SELECT path, collection_uuid FROM items WHERE
  // collection_uuid IN (...)
  //                ORDER BY name COLLATE NOCASE LIMIT ? OFFSET ?
  // This index covers: filtering (uuid), sorting (name), and result columns
  // (path)
  idx.prepare("CREATE INDEX IF NOT EXISTS idx_items_covering ON "
              "items(collection_uuid, name COLLATE NOCASE, path)");
  idx.exec();
}

void DatabaseManager::loadAllCollections(const QList<CollectionConfig> &allCollections) {
  emit requestLoadAllCollections(allCollections);
}

void DatabaseManager::loadItemsWithSubcollections(const CollectionContext &context,
                                                  const QList<CollectionConfig> &allCollections) {
  emit requestLoadItemsWithSubcollections(context, allCollections);
}

void DatabaseManager::loadItems(const CollectionContext &context,
                                const QList<CollectionConfig> &allCollections) {
  emit requestLoadItems(context, allCollections);
}

void DatabaseManager::fetchItemCount(const CollectionContext &context,
                                     const QList<CollectionConfig> &allCollections,
                                     const QString &filter, int requestToken) {
  qCDebug(lcSearchDiag) << "[DatabaseManager] fetchItemCount: collIndex=" << context.currentIndex
                        << "filter='" << filter << "'"
                        << "token=" << requestToken;
  // Kick off a background scan if needed, but don't block count queries.
  // IMPORTANT: Avoid scheduling scans for every search keystroke.
  // Scans can be expensive and their completion triggers count refreshes that
  // can invalidate in-flight paginated search range loads (causing blank views
  // + high CPU from repeated rebuilds).
  // playlists have no filesystem to scan — their items are
  // resolved directly from playlist_items by the worker, so skipping the scan
  // dispatch keeps the scan worker from chasing an empty mediaDirectory.
  if (filter.trimmed().isEmpty() && !context.config.isPlaylist) {
    emit requestEnsureScannedForContext(context, allCollections);
  }
  emit requestFetchItemCount(context, allCollections, filter, requestToken);
}

void DatabaseManager::fetchItemsRange(const CollectionContext &context,
                                      const QList<CollectionConfig> &allCollections, int offset,
                                      int limit, const QString &filter) {
  qCDebug(lcSearchDiag) << "[DatabaseManager] fetchItemsRange: collIndex=" << context.currentIndex
                        << "offset=" << offset << "limit=" << limit << "filter='" << filter << "'";
  emit requestFetchItemsRange(context, allCollections, offset, limit, filter);
}

void DatabaseManager::fetchVisualIndexForPath(const CollectionContext &context,
                                              const QList<CollectionConfig> &allCollections,
                                              const QString &filePath) {
  emit requestFetchVisualIndexForPath(context, allCollections, filePath);
}

void DatabaseManager::invalidateCollectionCache(const QString &collectionUuid) {
  emit requestInvalidateCache(collectionUuid);
}

// Clear a collection's data by uuid (main thread - only used for legacy sync
// operations)
void DatabaseManager::clearCollectionFromDatabaseByUuid(const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return;
  }

  if (!m_db.transaction()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                      "Failed to start database transaction",
                                      "DatabaseManager::clearCollectionFromDatabaseByUuid")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    return;
  }

  try {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM items WHERE collection_uuid = ?");
    query.addBindValue(collectionUuid);
    if (!query.exec()) {
      throw std::runtime_error(query.lastError().text().toStdString());
    }

    QSqlQuery delc(m_db);
    delc.prepare("DELETE FROM collections WHERE uuid = ?");
    delc.addBindValue(collectionUuid);
    if (!delc.exec()) {
      throw std::runtime_error(delc.lastError().text().toStdString());
    }

    if (!m_db.commit()) {
      throw std::runtime_error(m_db.lastError().text().toStdString());
    }
  } catch (const std::exception &e) {
    m_db.rollback();
    auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                      "Failed to clear collection from database",
                                      "DatabaseManager::clearCollectionFromDatabaseByUuid")
                   .withDetails(QString::fromStdString(e.what()));
    ErrorUtils::logError(err);
  }
}

// Count items in a single collection by uuid
auto DatabaseManager::countCollectionByUuid(const QString &collectionUuid) const -> qint64 {
  if (!m_db.isOpen()) {
    return 0;
  }
  QSqlQuery countQuery(m_db);
  countQuery.prepare("SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid=?");
  countQuery.addBindValue(collectionUuid);
  if (!countQuery.exec() || !countQuery.next()) {
    return 0;
  }
  return countQuery.value(0).toLongLong();
}
