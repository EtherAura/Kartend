// Database connection lifecycle + FTS readiness extracted from querymanager.cpp:
//   - refreshWalView, getPreparedStatement, clearStatementCache
//   - ensureDatabaseConnection, ensureDatabaseAvailable
//   - initDatabase, refreshSearchCapabilities
//   - isItemsFtsReadyFromDb, ensureItemsFtsReady
// Members of QueryManager; access existing class state.
#include "querymanager.h"

#include "dbtxn.h"

#include <QDir>
#include <QLoggingCategory>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QThread>

#include "connectionpragmas.h"
#include "dbmigrations.h"
#include "errorutils.h"
#include "querymanagersql.h"
#include "uiconstants/database.h"

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

// Gets or creates a prepared statement for the given SQL — thin wrapper
// over the PreparedStatementCache helper.
auto QueryManager::getPreparedStatement(const QString &sql) -> QSqlQuery & {
  return m_statementCache.get(sql);
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

  auto logReconnectAttempt = [](int attempt) {
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

      // Re-initialize PRAGMAs after reconnection. Kartend-67wo: shared
      // helper keeps this in sync with the initial-open path.
      MediaDbConnectionInit::PragmaConfig cfg;
      cfg.busyTimeoutMs = UIConstants::Database::WORKER_BUSY_TIMEOUT_MS;
      cfg.setSynchronousNormal = true;
      MediaDbConnectionInit::applyPragmas(m_db, cfg,
                                          QStringLiteral("QueryManager::ensureDatabaseConnection"));

      return true;
    }

    // Kartend-kfnv7: bail out of the retry ladder when teardown was
    // requested — sleeping through the remaining attempts could alone
    // exceed the destructor's 2s wait budget.
    if (teardownRequested()) {
      return false;
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
  assertOwnerThread();
  if (QSqlDatabase::contains(m_connectionName)) {
    m_db = QSqlDatabase::database(m_connectionName);
  } else {
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
  }
  m_statementCache.setDatabase(m_db);

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

  // Kartend-67wo: shared PRAGMA helper — worker connection uses the longer
  // busy_timeout (the FTS rebuild etc. holds locks) and synchronous=NORMAL for
  // write throughput.
  MediaDbConnectionInit::PragmaConfig pragmaCfg;
  pragmaCfg.busyTimeoutMs = UIConstants::Database::WORKER_BUSY_TIMEOUT_MS;
  pragmaCfg.setSynchronousNormal = true;
  MediaDbConnectionInit::applyPragmas(m_db, pragmaCfg,
                                      QStringLiteral("QueryManager::initDatabase"));

  // Ensure tables exist when worker opens DB in isolation.
  QSqlQuery query(m_db);
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
// Readiness generation stored under meta key 'items_fts_ready'
// (Kartend-4i5e4):
//   '0' — not ready (v3 migration seed); search uses the LIKE fallback.
//   '1' — LEGACY: the old incremental backfill completed. Those installs ran
//         with sync triggers active while the index was only partially
//         populated, so their index may hold FTS5 'delete' damage. Treated as
//         NOT ready so the next launch funnels them through one self-healing
//         rebuild.
//   '2' — the one-shot rebuild committed (index rebuilt from the items table
//         and the sync triggers created in the same transaction).
constexpr const char *kItemsFtsReadyGeneration = "2";

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

} // namespace

bool QueryManager::isItemsFtsReadyFromDb() {
  if (!m_db.isOpen()) {
    return false;
  }

  // Read-only check: if the meta table or key doesn't exist, assume FTS is NOT
  // ready. The scan worker will stamp the readiness generation when its
  // one-shot rebuild commits. This avoids write operations that could block on
  // the scan worker's FTS rebuild transaction.
  QString ready;
  if (!tryReadMetaValue(m_db, QStringLiteral("items_fts_ready"), ready)) {
    // Key not found - the FTS rebuild hasn't completed yet
    return false;
  }
  // Note: the legacy '1' (incremental backfill completed) is deliberately NOT
  // ready — see kItemsFtsReadyGeneration.
  return ready.trimmed() == QLatin1String(kItemsFtsReadyGeneration);
}

// Brings the items_fts external-content index to a ready state via a one-shot
// rebuild (Kartend-4i5e4). The previous incremental backfill ran with the v3
// sync triggers already active, so (a) trigger-fired FTS 'delete' commands
// could target rows the backfill had not indexed yet — documented FTS5
// external-content corruption that made rescan upserts/prunes on upgraded
// installs fail with SQLITE_CORRUPT — and (b) its progress watermark was
// persisted outside the batch transaction, re-inserting committed batches
// after a crash. The rebuild scheme has no such bookkeeping:
//
//   one transaction:  INSERT INTO items_fts(items_fts) VALUES('rebuild')
//                     + CREATE the sync triggers
//                     + stamp the readiness generation in meta
//
// 'rebuild' discards the entire index and re-derives it from the items table,
// so it is idempotent AND self-healing — any damage left behind by the old
// trigger/backfill interplay is erased. Because the triggers only come into
// existence in the same transaction, every trigger-fired 'delete' from then on
// targets postings that exist; items writes that land before the rebuild are
// simply captured by it. Runs once per database (per readiness generation) on
// the scan worker; cost is one full reindex, after which the triggers maintain
// the index forever. Until then searches use the LIKE fallback, exactly as
// they did while the old backfill was incomplete.
void QueryManager::ensureItemsFtsReady() {
  assertOwnerThread();
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

  KartendDb::DbTransaction txn(
      m_db, m_txnDepth, "QueryManager::ensureItemsFtsReady",
      [this](const ErrorUtils::ErrorContext &e) { emit errorOccurred(e); });
  if (!txn.activeOrReport("Failed to start transaction for FTS rebuild",
                          ErrorUtils::Severity::Warning)) {
    return;
  }

  const auto reportFailure = [this](const char *what, const QSqlQuery &q) {
    // guard dtor rolls back; retried on next launch, LIKE fallback meanwhile
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, what,
                                     "QueryManager::ensureItemsFtsReady")
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    emit errorOccurred(err);
  };

  QSqlQuery q(m_db);
  if (!q.exec("INSERT INTO items_fts(items_fts) VALUES('rebuild')")) {
    reportFailure("FTS index rebuild failed (search will use LIKE)", q);
    return;
  }

  // (Re)create the sync triggers atomically with the rebuilt index — dropped
  // by the v18 migration; see DbMigrations::kItemsFtsTrigger*Sql.
  if (!q.exec(DbMigrations::kItemsFtsTriggerInsertSql) ||
      !q.exec(DbMigrations::kItemsFtsTriggerDeleteSql) ||
      !q.exec(DbMigrations::kItemsFtsTriggerUpdateSql)) {
    reportFailure("FTS sync trigger creation failed (search will use LIKE)", q);
    return;
  }

  // Readiness stamp INSIDE the same transaction as the work it describes.
  // The legacy incremental-backfill watermark key is obsolete; drop it.
  q.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES('items_fts_ready', ?)");
  q.addBindValue(QLatin1String(kItemsFtsReadyGeneration));
  if (!q.exec()) {
    reportFailure("FTS readiness stamp failed (search will use LIKE)", q);
    return;
  }
  QSqlQuery cleanupQ(m_db);
  cleanupQ.prepare("DELETE FROM meta WHERE key = ?");
  cleanupQ.addBindValue(QStringLiteral("items_fts_indexed_up_to_id"));
  (void)cleanupQ.exec(); // best-effort: a stale key is inert

  if (!txn.commitOrReport("Failed to commit FTS rebuild", ErrorUtils::Severity::Warning)) {
    return;
  }
  m_itemsFtsReady = true;
}
