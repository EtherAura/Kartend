#include "dbmigrations.h"

#include <QSqlError>
#include <QSqlQuery>

#include "errorutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace {
static auto getUserVersion(QSqlDatabase &db) -> int {
  QSqlQuery q(db);
  if (q.exec("PRAGMA user_version") && q.next()) {
    return q.value(0).toInt();
  }
  return 0;
}

static auto setUserVersion(QSqlDatabase &db, int version) -> void {
  QSqlQuery q(db);
  q.exec(QString("PRAGMA user_version = %1").arg(version));
}

static auto tableHasColumn(QSqlDatabase &db, const QString &table,
                           const QString &column) -> bool {
  QSqlQuery q(db);
  if (!q.exec(QString("PRAGMA table_info(%1)").arg(table))) {
    return false;
  }
  while (q.next()) {
    if (q.value(1).toString() == column) {
      return true;
    }
  }
  return false;
}

static auto ensureColumn(QSqlDatabase &db, const QString &table,
                         const QString &column, const QString &definition,
                         const QString &origin) -> void {
  if (tableHasColumn(db, table, column)) {
    return;
  }
  QSqlQuery q(db);
  if (!q.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3")
                  .arg(table, column, definition))) {
    auto err =
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to migrate database schema", origin)
            .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
  }
}

static auto ensureUniqueIndexItemsUuidPath(QSqlDatabase &db,
                                           const QString &origin) -> void {
  QSqlQuery q(db);
  if (q.exec("CREATE UNIQUE INDEX IF NOT EXISTS uniq_items_uuid_path ON "
             "items(collection_uuid, path)")) {
    return;
  }

  const QString errText = q.lastError().text();
  if (!errText.contains("unique", Qt::CaseInsensitive) &&
      !errText.contains("constraint", Qt::CaseInsensitive)) {
    auto err = ErrorContext::warning(
                   ErrorCode::DatabaseQueryFailed,
                   "Failed to ensure unique index uniq_items_uuid_path", origin)
                   .withDetails(errText);
    ErrorUtils::logError(err);
    return;
  }

  // Attempt a best-effort de-duplication and retry.
  // Keep the first row (min(rowid)) for each (collection_uuid, path).
  QSqlQuery dedupe(db);
  dedupe.exec("DELETE FROM items WHERE rowid NOT IN (SELECT MIN(rowid) FROM "
              "items GROUP BY collection_uuid, path)");
  QSqlQuery retry(db);
  if (!retry.exec("CREATE UNIQUE INDEX IF NOT EXISTS uniq_items_uuid_path ON "
                  "items(collection_uuid, path)")) {
    auto err = ErrorContext::warning(
                   ErrorCode::DatabaseQueryFailed,
                   "Failed to create unique index after de-duplication", origin)
                   .withDetails(retry.lastError().text());
    ErrorUtils::logError(err);
  }
}

static auto ensureIndex(QSqlDatabase &db, const QString &sql,
                        const QString &origin, const QString &what) -> void {
  QSqlQuery q(db);
  if (q.exec(sql)) {
    return;
  }
  auto err =
      ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                            "Failed to ensure database index", origin)
          .withDetails(QString("%1: %2").arg(what, q.lastError().text()));
  ErrorUtils::logError(err);
}
} // namespace

namespace DbMigrations {

void applySchemaMigrations(QSqlDatabase &db, const QString &origin) {
  if (!db.isOpen()) {
    return;
  }

  constexpr int CURRENT_SCHEMA_VERSION = 3;
  const int version = getUserVersion(db);
  if (version >= CURRENT_SCHEMA_VERSION) {
    return;
  }

  int mutableVersion = version;

  if (mutableVersion < 1) {
    // v1: Introduce schema versioning + ensure columns needed by newer builds.
    ensureColumn(db, "collections", "ext_signature", "TEXT DEFAULT ''", origin);
    ensureColumn(db, "collections", "uuid", "TEXT DEFAULT ''", origin);
    ensureColumn(db, "collections", "dir_signature", "TEXT DEFAULT ''", origin);

    ensureColumn(db, "items", "collection_uuid", "TEXT DEFAULT ''", origin);
    ensureColumn(db, "items", "artwork_path", "TEXT", origin);
    ensureColumn(db, "items", "play_count", "INTEGER DEFAULT 0", origin);
    ensureColumn(db, "items", "last_played", "TEXT", origin);
    ensureColumn(db, "items", "rating", "INTEGER DEFAULT 0", origin);

    ensureUniqueIndexItemsUuidPath(db, origin);

    setUserVersion(db, 1);
    mutableVersion = 1;
  }

  if (mutableVersion < 2) {
    // v2: Add query performance indexes used by the virtual scroll + filtering
    // paths.
    ensureIndex(
        db,
        "CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)",
        origin, "idx_collections_uuid");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_items_collection_uuid ON "
                "items(collection_uuid)",
                origin, "idx_items_collection_uuid");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_items_name ON items(name "
                "COLLATE NOCASE)",
                origin, "idx_items_name");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_items_uuid_name ON "
                "items(collection_uuid, name COLLATE NOCASE)",
                origin, "idx_items_uuid_name");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_items_covering ON "
                "items(collection_uuid, name COLLATE NOCASE, path)",
                origin, "idx_items_covering");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_items_uuid_last_modified ON "
                "items(collection_uuid, last_modified)",
                origin, "idx_items_uuid_last_modified");

    setUserVersion(db, 2);
    mutableVersion = 2;
  }

  if (mutableVersion < 3) {
    // v3: Add FTS5 index for fast name filtering.
    // This is best-effort: if SQLite is built without FTS5, keep working with
    // LIKE.
    {
      QSqlQuery q(db);
      const bool created =
          q.exec("CREATE VIRTUAL TABLE IF NOT EXISTS items_fts USING fts5("
                 "name, path, collection_uuid, "
                 "content='items', content_rowid='id', "
                 "tokenize='unicode61'"
                 ")");

      if (!created) {
        auto err =
            ErrorContext::warning(
                ErrorCode::DatabaseQueryFailed,
                "Failed to create FTS index (search will use LIKE)", origin)
                .withDetails(q.lastError().text());
        ErrorUtils::logError(err);
      } else {
        // Lightweight metadata store for tracking background maintenance.
        // Note: This is intentionally a plain table (not PRAGMA-based) so it
        // can evolve without bumping user_version again.
        ensureIndex(db,
                    "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, "
                    "value TEXT NOT NULL)",
                    origin, "meta");

        // Mark FTS as not-yet-ready and backfill incrementally on the scan
        // worker. Avoid blocking app startup by running a full synchronous
        // rebuild here.
        ensureIndex(db,
                    "INSERT OR IGNORE INTO meta(key, value) "
                    "VALUES('items_fts_ready', '0')",
                    origin, "meta items_fts_ready");
        ensureIndex(db,
                    "INSERT OR IGNORE INTO meta(key, value) "
                    "VALUES('items_fts_indexed_up_to_id', '0')",
                    origin, "meta items_fts_indexed_up_to_id");

        // Keep the FTS index in sync with the items table.
        ensureIndex(
            db,
            "CREATE TRIGGER IF NOT EXISTS items_fts_ai AFTER INSERT ON items "
            "BEGIN "
            "  INSERT INTO items_fts(rowid, name, path, collection_uuid) "
            "  VALUES (new.id, new.name, new.path, new.collection_uuid); "
            "END;",
            origin, "items_fts_ai");

        ensureIndex(db,
                    "CREATE TRIGGER IF NOT EXISTS items_fts_ad AFTER DELETE ON "
                    "items BEGIN "
                    "  INSERT INTO items_fts(items_fts, rowid, name, path, "
                    "collection_uuid) "
                    "  VALUES('delete', old.id, old.name, old.path, "
                    "old.collection_uuid); "
                    "END;",
                    origin, "items_fts_ad");

        ensureIndex(
            db,
            "CREATE TRIGGER IF NOT EXISTS items_fts_au AFTER UPDATE ON items "
            "BEGIN "
            "  INSERT INTO items_fts(items_fts, rowid, name, path, "
            "collection_uuid) "
            "  VALUES('delete', old.id, old.name, old.path, "
            "old.collection_uuid); "
            "  INSERT INTO items_fts(rowid, name, path, collection_uuid) "
            "  VALUES (new.id, new.name, new.path, new.collection_uuid); "
            "END;",
            origin, "items_fts_au");
      }
    }

    setUserVersion(db, 3);
    // mutableVersion would be 3 here but function ends
  }
}

} // namespace DbMigrations
