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

static auto tableHasColumn(QSqlDatabase &db, const QString &table, const QString &column) -> bool {
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

static auto ensureColumn(QSqlDatabase &db, const QString &table, const QString &column,
                         const QString &definition, const QString &origin) -> void {
  if (tableHasColumn(db, table, column)) {
    return;
  }
  QSqlQuery q(db);
  if (!q.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, definition))) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to migrate database schema", origin)
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
  }
}

static auto ensureUniqueIndexItemsUuidPath(QSqlDatabase &db, const QString &origin) -> void {
  QSqlQuery q(db);
  if (q.exec("CREATE UNIQUE INDEX IF NOT EXISTS uniq_items_uuid_path ON "
             "items(collection_uuid, path)")) {
    return;
  }

  const QString errText = q.lastError().text();
  if (!errText.contains("unique", Qt::CaseInsensitive) &&
      !errText.contains("constraint", Qt::CaseInsensitive)) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
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
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to create unique index after de-duplication", origin)
                   .withDetails(retry.lastError().text());
    ErrorUtils::logError(err);
  }
}

static auto ensureIndex(QSqlDatabase &db, const QString &sql, const QString &origin,
                        const QString &what) -> void {
  QSqlQuery q(db);
  if (q.exec(sql)) {
    return;
  }
  auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
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

  constexpr int CURRENT_SCHEMA_VERSION = 10;
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
    ensureIndex(db, "CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)", origin,
                "idx_collections_uuid");
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
      const bool created = q.exec("CREATE VIRTUAL TABLE IF NOT EXISTS items_fts USING fts5("
                                  "name, path, collection_uuid, "
                                  "content='items', content_rowid='id', "
                                  "tokenize='unicode61'"
                                  ")");

      if (!created) {
        auto err =
            ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
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
        ensureIndex(db,
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

        ensureIndex(db,
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
    mutableVersion = 3;
  }

  if (mutableVersion < 4) {
    // v4: Add file_size so item lists can sort by size without touching the
    // filesystem at query time. Existing rows backfill to 0 until the next
    // collection rescan refreshes metadata from disk.
    ensureColumn(db, "items", "file_size", "INTEGER DEFAULT 0", origin);
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_items_uuid_last_modified ON "
                "items(collection_uuid, last_modified)",
                origin, "idx_items_uuid_last_modified");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_items_uuid_file_size ON "
                "items(collection_uuid, file_size)",
                origin, "idx_items_uuid_file_size");

    setUserVersion(db, 4);
    mutableVersion = 4;
  }

  if (mutableVersion < 5) {
    // v5: Add item_metadata table for extended per-item metadata
    // (scraper-populated structured fields + user-defined custom fields +
    // optional manual path override). Keyed by (collection_uuid, path) so
    // entries survive item id renumbering across rescans.
    ensureIndex(db,
                "CREATE TABLE IF NOT EXISTS item_metadata ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "collection_uuid TEXT NOT NULL DEFAULT '', "
                "path TEXT NOT NULL, "
                "title TEXT, "
                "description TEXT, "
                "genre TEXT, "
                "developer TEXT, "
                "publisher TEXT, "
                "release_date TEXT, "
                "content_rating TEXT, "
                "players TEXT, "
                "runtime_seconds INTEGER, "
                "tags TEXT, "
                "custom_fields TEXT, "
                "manual_path TEXT, "
                "source TEXT, "
                "updated_at TEXT NOT NULL DEFAULT '', "
                "UNIQUE(collection_uuid, path)"
                ")",
                origin, "item_metadata");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_item_metadata_uuid_path ON "
                "item_metadata(collection_uuid, path)",
                origin, "idx_item_metadata_uuid_path");

    setUserVersion(db, 5);
    mutableVersion = 5;
  }

  if (mutableVersion < 6) {
    // v6: Add item_artwork table for multi-type per-item artwork.
    // Keyed by (collection_uuid, path, artwork_type) so each item can have one
    // row per type (box, screenshot, marquee, etc., plus user-defined custom
    // types added in a later sub-issue). manual_path is the per-item override;
    // when NULL, callers fall back to subdirectory auto-discovery for the
    // standard types only. Mirrors the v5 pattern so entries survive id
    // renumbering across rescans.
    ensureIndex(db,
                "CREATE TABLE IF NOT EXISTS item_artwork ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "collection_uuid TEXT NOT NULL DEFAULT '', "
                "path TEXT NOT NULL, "
                "artwork_type TEXT NOT NULL, "
                "manual_path TEXT, "
                "updated_at TEXT NOT NULL DEFAULT '', "
                "UNIQUE(collection_uuid, path, artwork_type)"
                ")",
                origin, "item_artwork");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_item_artwork_uuid_path ON "
                "item_artwork(collection_uuid, path)",
                origin, "idx_item_artwork_uuid_path");

    setUserVersion(db, 6);
    mutableVersion = 6;
  }

  if (mutableVersion < 7) {
    // v7: Cumulative play-time tracking. play_count and
    // last_played already exist from v1; add total_play_seconds for the
    // runtime-detection accumulated session duration. Also
    // index last_played so the "Recently played" view can sort cheaply.
    ensureColumn(db, "items", "total_play_seconds", "INTEGER DEFAULT 0", origin);
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_items_last_played ON "
                "items(last_played)",
                origin, "idx_items_last_played");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_items_play_count ON "
                "items(play_count)",
                origin, "idx_items_play_count");

    setUserVersion(db, 7);
    mutableVersion = 7;
  }

  if (mutableVersion < 8) {
    // v8: Per-item launcher override. Stores a unified
    // launcher index (0 = primary, 1..N = additionalLaunchers[0..N-1]) so an
    // item can pin a specific launcher and skip the multi-launcher chooser
    // dialog. NULL means "no override" — fall through to the chooser /
    // collection default. Lives on item_metadata to share the same
    // (collection_uuid, path) key as manual_path / custom_fields.
    ensureColumn(db, "item_metadata", "launcher_index", "INTEGER", origin);

    setUserVersion(db, 8);
    mutableVersion = 8;
  }

  if (mutableVersion < 9) {
    // v9: Chronological launch history. Append-only log keyed
    // by an auto-incrementing id; the same (collection_uuid, path) appears
    // multiple times on purpose. `name` is denormalized at insert time so
    // rows stay readable after the source item is deleted.
    ensureIndex(db,
                "CREATE TABLE IF NOT EXISTS launch_history ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "collection_uuid TEXT NOT NULL DEFAULT '', "
                "path TEXT NOT NULL DEFAULT '', "
                "name TEXT, "
                "launched_at TEXT NOT NULL DEFAULT ''"
                ")",
                origin, "launch_history");
    // Index for trim-to-N and "recent" queries — both walk the table in
    // descending id order. SQLite already serves this from the implicit
    // PK index, but an explicit launched_at index keeps any future
    // launched_at-based queries cheap (e.g. "older than 30 days").
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_launch_history_launched_at "
                "ON launch_history(launched_at)",
                origin, "idx_launch_history_launched_at");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_launch_history_uuid_path "
                "ON launch_history(collection_uuid, path)",
                origin, "idx_launch_history_uuid_path");

    setUserVersion(db, 9);
    mutableVersion = 9;
  }

  if (mutableVersion < 10) {
    // v10: Playlists. A playlist is a virtual collection whose
    // items are explicit (collection_uuid, path) references into the existing
    // `items` table — so a single playlist can mix media from any number of
    // source collections. `parent_collection_uuid` is optional: empty means
    // "root-level virtual collection", otherwise the playlist nests under the
    // collection with that uuid (mirrors CollectionConfig::parentCollectionIndex
    // for INI-backed collections). `reserved_kind` is a slot for built-in
    // playlists (e.g. 'favorites' in); user-created playlists
    // leave it empty.
    ensureIndex(db,
                "CREATE TABLE IF NOT EXISTS playlists ("
                "id TEXT PRIMARY KEY, "
                "name TEXT NOT NULL, "
                "icon TEXT NOT NULL DEFAULT '', "
                "parent_collection_uuid TEXT NOT NULL DEFAULT '', "
                "reserved_kind TEXT NOT NULL DEFAULT '', "
                "created_at TEXT NOT NULL DEFAULT '', "
                "updated_at TEXT NOT NULL DEFAULT ''"
                ")",
                origin, "playlists");

    // Playlist items reference rows by (collection_uuid, path) — the same key
    // shape used by item_metadata / item_artwork — so entries survive item id
    // renumbering across rescans. `position` is a dense 0-based ordering
    // controlled by the playlist editor; the (playlist_id, position) PK lets
    // ORDER BY position serve the natural list view directly.
    ensureIndex(db,
                "CREATE TABLE IF NOT EXISTS playlist_items ("
                "playlist_id TEXT NOT NULL, "
                "position INTEGER NOT NULL, "
                "source_collection_uuid TEXT NOT NULL, "
                "source_path TEXT NOT NULL, "
                "added_at TEXT NOT NULL DEFAULT '', "
                "PRIMARY KEY (playlist_id, position), "
                "FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE"
                ")",
                origin, "playlist_items");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_playlist_items_lookup "
                "ON playlist_items(source_collection_uuid, source_path)",
                origin, "idx_playlist_items_lookup");
    ensureIndex(db,
                "CREATE INDEX IF NOT EXISTS idx_playlist_items_playlist "
                "ON playlist_items(playlist_id)",
                origin, "idx_playlist_items_playlist");

    setUserVersion(db, 10);
  }
}

} // namespace DbMigrations
