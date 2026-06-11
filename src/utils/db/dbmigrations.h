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
