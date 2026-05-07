// Database connection lifecycle + FTS readiness extracted from querymanager.cpp:
//   - refreshWalView, getPreparedStatement, clearStatementCache
//   - ensureDatabaseConnection, ensureDatabaseAvailable
//   - initDatabase, refreshSearchCapabilities
//   - isItemsFtsReadyFromDb, ensureItemsFtsReady
// Members of QueryManager; access existing class state.
#include "querymanager.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QThread>
#include <QTimer>

#include "dbmigrations.h"
#include "errorutils.h"
#include "querymanagersql.h"
#include "uiconstants.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)
#define debugLog(msg) qCDebug(lcQueryManager) << msg

void QueryManager::refreshWalView() {
  // Starting and immediately committing a deferred transaction forces SQLite
  // to acquire a fresh read snapshot that includes all prior commits.
  // This is lighter than wal_checkpoint and doesn't interfere with writes.
  QSqlQuery query(m_db);
  query.exec("BEGIN");
  query.exec("COMMIT");

  // Clear statement cache to prevent stale bound values from interfering
  // with subsequent queries that need fresh data.
  m_statementCache.clear();
}

// Gets or creates a prepared statement for the given SQL
// Caches compiled statements to avoid repeated prepare() overhead
// Uses LRU eviction when cache exceeds MAX_STATEMENT_CACHE_SIZE
auto QueryManager::getPreparedStatement(const QString &sql) -> QSqlQuery & {
  if (QSqlQuery *cached = m_statementCache.object(sql)) {
    // Fully reset before reuse.
    // QSqlQuery may retain bound values / internal state across exec() calls.
    // If the cached instance is reused for dynamic search SQL, SQLite can
    // report "Parameter count mismatch" unless we reinitialize it.
    cached->finish();
    *cached = QSqlQuery(m_db);
    cached->prepare(sql);
    return *cached;
  }

  // Create new prepared statement and cache it (QCache takes ownership).
  auto *query = new QSqlQuery(m_db);
  query->prepare(sql);
  m_statementCache.insert(sql, query, 1);
  return *query;
}

// Clears statement cache - call when database connection changes
void QueryManager::clearStatementCache() {
  m_statementCache.clear();
}

// Attempts to reconnect to the database if connection was lost
// Used to handle transient SQLite errors (disk full, I/O errors, etc.)
// Returns true if database is now open, false otherwise
// NOTE: Only attempts reconnection if database was previously initialized
//       (has valid driver). Returns false for uninitialized databases.
auto QueryManager::ensureDatabaseConnection() -> bool {
  static constexpr int MAX_RECONNECT_ATTEMPTS = UIConstants::Database::WORKER_RECONNECT_ATTEMPTS;
  static constexpr int RECONNECT_DELAY_MS = UIConstants::Database::WORKER_RECONNECT_DELAY_MS;

  if (m_db.isOpen()) {
    return true;
  }

  // Don't attempt reconnection if database was never initialized
  // (no driver means initDatabase() hasn't been called yet)
  if (!m_db.isValid() || m_db.driverName().isEmpty()) {
    return false;
  }

  auto logReconnectAttempt = [this](int attempt) {
    auto info =
        ErrorContext::info(ErrorCode::DatabaseConnectionLost,
                           QString("Database connection lost, attempting reconnection (%1/%2)")
                               .arg(attempt)
                               .arg(MAX_RECONNECT_ATTEMPTS),
                           "QueryManager::ensureDatabaseConnection");
    ErrorUtils::logError(info);
  };

  for (int attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS; ++attempt) {
    logReconnectAttempt(attempt);

    // Close and clear the old connection state
    clearStatementCache();
    m_db.close();

    // Try to reopen
    if (m_db.open()) {
      auto success =
          ErrorContext::info(ErrorCode::DatabaseConnectionRestored,
                             QString("Database reconnection successful on attempt %1").arg(attempt),
                             "QueryManager::ensureDatabaseConnection");
      ErrorUtils::logError(success);

      // Re-initialize PRAGMAs after reconnection
      QSqlQuery query(m_db);
      query.exec("PRAGMA foreign_keys = ON");
      query.exec("PRAGMA journal_mode = WAL");
      query.exec(QStringLiteral("PRAGMA busy_timeout = %1")
                     .arg(UIConstants::Database::WORKER_BUSY_TIMEOUT_MS));
      query.exec("PRAGMA synchronous = NORMAL");

      return true;
    }

    // Wait before next attempt (unless it's the last one)
    if (attempt < MAX_RECONNECT_ATTEMPTS) {
      QThread::msleep(RECONNECT_DELAY_MS);
    }
  }

  // All attempts failed
  auto err =
      ErrorContext::critical(
          ErrorCode::DatabaseConnectionFailed,
          QString("Failed to reconnect to database after %1 attempts").arg(MAX_RECONNECT_ATTEMPTS),
          "QueryManager::ensureDatabaseConnection")
          .withDetails(m_db.lastError().text());
  ErrorUtils::logError(err);
  emit errorOccurred(err);

  return false;
}

auto QueryManager::ensureDatabaseAvailable(const char *callerContext) -> bool {
  if (ensureDatabaseConnection()) {
    return true;
  }

  // Last-resort fallback: re-run full init in case the connection was never
  // registered (cold start) or was destroyed by Qt cleanup.
  initDatabase();
  if (m_db.isOpen()) {
    return true;
  }

  // All recovery paths exhausted - notify the main thread so callers don't
  // hang waiting on a result signal that will never come.
  auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                    QStringLiteral("Database unavailable after reconnect + init"),
                                    callerContext)
                 .withDetails(m_db.lastError().text());
  ErrorUtils::logError(err);
  emit errorOccurred(err);
  return false;
}

void QueryManager::initDatabase() {
  if (QSqlDatabase::contains(m_connectionName)) {
    m_db = QSqlDatabase::database(m_connectionName);
  } else {
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
  }

  QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!QDir().mkpath(dbPath)) {
    auto err =
        ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                               "Failed to create database directory", "QueryManager::initDatabase")
            .withDetails(QString("Path: %1").arg(dbPath));
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }
  m_db.setDatabaseName(dbPath + "/media.db");

  if (!m_db.open()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to open database", "QueryManager::initDatabase")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  QSqlQuery query(m_db);
  if (!query.exec("PRAGMA foreign_keys = ON")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to enable foreign keys", "QueryManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  if (!query.exec("PRAGMA journal_mode = WAL")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to enable WAL mode",
                                     "QueryManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  // Set busy timeout to prevent indefinite blocking when another connection
  // holds a lock (e.g., FTS backfill on scan worker). Queries will fail with
  // SQLITE_BUSY after the timeout, allowing graceful fallback.
  const QString busyTimeoutPragma =
      QStringLiteral("PRAGMA busy_timeout = %1").arg(UIConstants::Database::WORKER_BUSY_TIMEOUT_MS);
  if (!query.exec(busyTimeoutPragma)) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to set busy timeout",
                                     "QueryManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  // Use NORMAL synchronous mode for better write performance while maintaining
  // data safety - WAL mode already provides crash recovery guarantees
  if (!query.exec("PRAGMA synchronous = NORMAL")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to set synchronous mode", "QueryManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }

  // Ensure tables exist when worker opens DB in isolation.
  query.exec("CREATE TABLE IF NOT EXISTS collections ("
             "id INTEGER PRIMARY KEY, "
             "name TEXT NOT NULL, "
             "last_scanned TEXT NOT NULL, "
             "ext_signature TEXT DEFAULT '', "
             "uuid TEXT DEFAULT ''"
             ")");

  query.exec("CREATE TABLE IF NOT EXISTS items ("
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
             ")");

  // Keep core indexes available even in worker-only initialization.
  query.exec("CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)");
  query.exec("CREATE INDEX IF NOT EXISTS idx_items_collection_uuid ON "
             "items(collection_uuid)");
  // Composite index for sorted range queries - enables efficient ORDER BY name
  // when filtering by collection_uuid (common in showAllSubcollectionItems
  // mode)
  query.exec("CREATE INDEX IF NOT EXISTS idx_items_uuid_name ON "
             "items(collection_uuid, name COLLATE NOCASE)");

  DbMigrations::applySchemaMigrations(m_db, QStringLiteral("QueryManager::initDatabase"));

  refreshSearchCapabilities();
}

void QueryManager::refreshSearchCapabilities() {
  m_itemsFtsAvailable = false;
  m_itemsFtsReady = false; // Conservative default - will be set true by
                           // isItemsFtsReadyFromDb()
  if (!m_db.isOpen()) {
    return;
  }

  QSqlQuery q(m_db);
  q.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
  q.addBindValue(QStringLiteral("items_fts"));
  if (q.exec() && q.next()) {
    m_itemsFtsAvailable = true;
  }

  if (m_itemsFtsAvailable) {
    m_itemsFtsReady = isItemsFtsReadyFromDb();
  }
}

namespace {
static auto ensureMetaTable(QSqlDatabase &db) -> void {
  if (!db.isOpen()) {
    return;
  }
  QSqlQuery q(db);
  q.exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT "
         "NOT NULL)");
}

static auto tryReadMetaValue(QSqlDatabase &db, const QString &key, QString &valueOut) -> bool {
  QSqlQuery q(db);
  q.prepare("SELECT value FROM meta WHERE key = ?");
  q.addBindValue(key);
  if (!q.exec() || !q.next()) {
    return false;
  }
  valueOut = q.value(0).toString();
  return true;
}

static auto writeMetaValue(QSqlDatabase &db, const QString &key, const QString &value) -> void {
  QSqlQuery q(db);
  q.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)");
  q.addBindValue(key);
  q.addBindValue(value);
  q.exec();
}

static auto tryReadMetaInt(QSqlDatabase &db, const QString &key, qint64 &valueOut) -> bool {
  QString value;
  if (!tryReadMetaValue(db, key, value)) {
    return false;
  }
  bool ok = false;
  const qint64 parsed = value.toLongLong(&ok);
  if (!ok) {
    return false;
  }
  valueOut = parsed;
  return true;
}
} // namespace

bool QueryManager::isItemsFtsReadyFromDb() {
  if (!m_db.isOpen()) {
    return false;
  }

  // Read-only check: if the meta table or key doesn't exist, assume FTS is NOT
  // ready. The scan worker will set the "items_fts_ready" marker when backfill
  // completes. This avoids write operations that could block on the scan
  // worker's FTS backfill transaction.
  QString ready;
  if (!tryReadMetaValue(m_db, QStringLiteral("items_fts_ready"), ready)) {
    // Key not found - FTS backfill hasn't completed yet
    return false;
  }
  return ready.trimmed() == QLatin1String("1");
}

void QueryManager::ensureItemsFtsReady() {
  if (!ensureDatabaseAvailable("QueryManager::ensureItemsFtsReady")) {
    return;
  }

  // Refresh availability and current readiness state.
  refreshSearchCapabilities();
  if (!m_itemsFtsAvailable) {
    return;
  }

  // If already ready, nothing to do.
  if (m_itemsFtsReady) {
    return;
  }

  ensureMetaTable(m_db);

  QElapsedTimer slice;
  slice.start();

  qint64 indexedUpToId = 0;
  (void)tryReadMetaInt(m_db, QStringLiteral("items_fts_indexed_up_to_id"), indexedUpToId);

  while (slice.elapsed() < UIConstants::Database::FTS_BACKFILL_TIME_BUDGET_MS) {
    // Determine the current max id in the content table.
    qint64 maxId = 0;
    {
      QSqlQuery maxQ(m_db);
      if (maxQ.exec("SELECT COALESCE(MAX(id), 0) FROM items") && maxQ.next()) {
        maxId = maxQ.value(0).toLongLong();
      }
    }

    if (maxId == 0) {
      // No items in database yet - don't mark FTS ready since there's nothing
      // to index. Items will be populated by a scan; we'll be called again
      // after the scan completes to do the actual backfill.
      return;
    }

    if (maxId <= indexedUpToId) {
      writeMetaValue(m_db, QStringLiteral("items_fts_ready"), QStringLiteral("1"));
      m_itemsFtsReady = true;
      return;
    }

    if (!m_db.transaction()) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseTransactionFailed,
                                       "Failed to start transaction for FTS backfill",
                                       "QueryManager::ensureItemsFtsReady")
                     .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return;
    }

    QSqlQuery insertQ(m_db);
    insertQ.prepare("INSERT INTO items_fts(rowid, name, path, collection_uuid) "
                    "SELECT id, name, path, collection_uuid "
                    "FROM items "
                    "WHERE id > ? "
                    "ORDER BY id "
                    "LIMIT ?");
    insertQ.addBindValue(indexedUpToId);
    insertQ.addBindValue(UIConstants::Database::FTS_BACKFILL_BATCH_SIZE);

    if (!insertQ.exec()) {
      m_db.rollback();
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "FTS backfill insert failed (search will use LIKE)",
                                       "QueryManager::ensureItemsFtsReady")
                     .withDetails(insertQ.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return;
    }

    qint64 newIndexedUpToId = indexedUpToId;
    {
      // Compute the last id included in this batch.
      // Do NOT use MAX(rowid) from items_fts because triggers may insert higher
      // ids concurrently (e.g., during scans), which would incorrectly skip
      // unindexed rows.
      QSqlQuery lastIdQ(m_db);
      lastIdQ.prepare("SELECT COALESCE(MAX(id), ?) FROM ("
                      "  SELECT id FROM items WHERE id > ? ORDER BY id LIMIT ?"
                      ")");
      lastIdQ.addBindValue(indexedUpToId);
      lastIdQ.addBindValue(indexedUpToId);
      lastIdQ.addBindValue(UIConstants::Database::FTS_BACKFILL_BATCH_SIZE);
      if (lastIdQ.exec() && lastIdQ.next()) {
        newIndexedUpToId = lastIdQ.value(0).toLongLong();
      }
    }

    (void)m_db.commit();

    if (newIndexedUpToId <= indexedUpToId) {
      // No progress; avoid tight looping.
      break;
    }

    indexedUpToId = newIndexedUpToId;
    writeMetaValue(m_db, QStringLiteral("items_fts_indexed_up_to_id"),
                   QString::number(indexedUpToId));
  }

  // Continue in another slice.
  // Defer between slices to keep the scan worker event loop responsive AND
  // avoid a tight 0ms loop that can peg a CPU core on very large databases.
  QTimer::singleShot(UIConstants::Database::FTS_BACKFILL_SLICE_DELAY_MS, this,
                     &QueryManager::ensureItemsFtsReady);
}
