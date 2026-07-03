#ifndef DBMIGRATIONS_H
#define DBMIGRATIONS_H

#include <QSqlDatabase>
#include <QString>

namespace DbMigrations {

// Applies in-place schema migrations using SQLite PRAGMA user_version.
// The operation is idempotent and safe to call on every startup.
//
// `origin` is used for structured ErrorContext logging.
void applySchemaMigrations(QSqlDatabase &db, const QString &origin);

// Probe-first ADD COLUMN, shared by the migration ladder and by secondary
// connections that must keep an already-migrated table schema-compatible
// without re-running the full ladder (e.g. the standalone playlist
// connection). Returns true when the column is present after the call
// (already existed, or was added); logs and returns false when the ALTER
// TABLE genuinely failed. Unlike a blind `ALTER TABLE ... ADD COLUMN` whose
// result is discarded, this distinguishes "column already there" (benign)
// from a locked-database / I/O failure that leaves the schema incomplete.
auto ensureColumn(QSqlDatabase &db, const QString &table, const QString &column,
                  const QString &definition, const QString &origin) -> bool;

// Executes one idempotent DDL statement (CREATE INDEX / CREATE TABLE / DROP
// ... IF [NOT] EXISTS), logging `what` plus the SQL error and returning false
// on failure. Named for its dominant use; the ladder routes its CREATE TABLE
// statements through it too.
auto ensureIndex(QSqlDatabase &db, const QString &sql, const QString &origin, const QString &what)
    -> bool;

// ── items_fts sync trigger DDL (Kartend-4i5e4) ──────────────────────────────
// Single source of truth for the items -> items_fts sync triggers. Historically
// created by the v3 migration block alongside the (empty) FTS table; that let
// the AFTER UPDATE / AFTER DELETE triggers issue FTS5 'delete' commands for
// rows the index had never seen — documented external-content corruption that
// made routine rescan upserts fail with SQLITE_CORRUPT on upgraded installs.
// The v18 block drops the eagerly-created triggers; they are now (re)created by
// QueryManager::ensureItemsFtsReady inside the SAME transaction as the one-shot
// index rebuild, so trigger existence implies the index is consistent with the
// items table and every 'delete' targets postings that actually exist.
inline constexpr const char *kItemsFtsTriggerInsertSql =
    "CREATE TRIGGER IF NOT EXISTS items_fts_ai AFTER INSERT ON items "
    "BEGIN "
    "  INSERT INTO items_fts(rowid, name, path, collection_uuid) "
    "  VALUES (new.id, new.name, new.path, new.collection_uuid); "
    "END;";

inline constexpr const char *kItemsFtsTriggerDeleteSql =
    "CREATE TRIGGER IF NOT EXISTS items_fts_ad AFTER DELETE ON "
    "items BEGIN "
    "  INSERT INTO items_fts(items_fts, rowid, name, path, "
    "collection_uuid) "
    "  VALUES('delete', old.id, old.name, old.path, "
    "old.collection_uuid); "
    "END;";

inline constexpr const char *kItemsFtsTriggerUpdateSql =
    "CREATE TRIGGER IF NOT EXISTS items_fts_au AFTER UPDATE ON items "
    "BEGIN "
    "  INSERT INTO items_fts(items_fts, rowid, name, path, "
    "collection_uuid) "
    "  VALUES('delete', old.id, old.name, old.path, "
    "old.collection_uuid); "
    "  INSERT INTO items_fts(rowid, name, path, collection_uuid) "
    "  VALUES (new.id, new.name, new.path, new.collection_uuid); "
    "END;";

} // namespace DbMigrations

#endif // DBMIGRATIONS_H
