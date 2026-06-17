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

static auto setUserVersion(QSqlDatabase &db, int version) -> bool {
  QSqlQuery q(db);
  if (!q.exec(QString("PRAGMA user_version = %1").arg(version))) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     QString("Failed to set user_version to %1").arg(version),
                                     "DbMigrations::setUserVersion")
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }
  return true;
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

// Returns true if the column is present after the call (already existed, or was
// added). Returns false if the ALTER TABLE failed — the caller must abort the
// migration block so user_version is not advanced past a column that does not
// exist (Kartend-o58ur).
static auto ensureColumn(QSqlDatabase &db, const QString &table, const QString &column,
                         const QString &definition, const QString &origin) -> bool {
  if (tableHasColumn(db, table, column)) {
    return true;
  }
  QSqlQuery q(db);
  if (!q.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, definition))) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to migrate database schema", origin)
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }
  return true;
}

// Returns true if the column is absent after the call (already gone, or
// successfully dropped). SQLite supports ALTER TABLE DROP COLUMN (3.35+); the
// column must not be indexed or part of a constraint — used here only for plain
// unindexed columns. Returns false (aborting the block) if the drop failed, so
// user_version is not advanced past a column that still exists.
static auto dropColumn(QSqlDatabase &db, const QString &table, const QString &column,
                       const QString &origin) -> bool {
  if (!tableHasColumn(db, table, column)) {
    return true;
  }
  QSqlQuery q(db);
  if (!q.exec(QString("ALTER TABLE %1 DROP COLUMN %2").arg(table, column))) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to drop database column", origin)
                   .withDetails(q.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }
  return true;
}

// Creates the unique (collection_uuid, path) index, de-duplicating existing
// rows first if a uniqueness conflict blocks creation. Returns false only if
// the index still could not be created (the caller's transaction then rolls the
// whole block back). The merge + delete + retry are made atomic with the rest
// of the v1 block by the surrounding runBlock() transaction (Kartend-l793,
// Kartend-l28zj) — this helper no longer opens its own transaction.
static auto ensureUniqueIndexItemsUuidPath(QSqlDatabase &db, const QString &origin) -> bool {
  QSqlQuery q(db);
  if (q.exec("CREATE UNIQUE INDEX IF NOT EXISTS uniq_items_uuid_path ON "
             "items(collection_uuid, path)")) {
    return true;
  }

  const QString errText = q.lastError().text();
  if (!errText.contains("unique", Qt::CaseInsensitive) &&
      !errText.contains("constraint", Qt::CaseInsensitive)) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to ensure unique index uniq_items_uuid_path", origin)
                   .withDetails(errText);
    ErrorUtils::logError(err);
    return false;
  }

  // Before dropping duplicates, merge each group's user-data columns onto the
  // surviving (min rowid) row. The old code kept the first-inserted row
  // verbatim, silently losing play_count / last_played / rating / artwork that
  // a later duplicate carried. Usage writes target (collection_uuid, path) so
  // duplicates normally hold identical values; MAX / COALESCE picks the
  // populated one when they diverge. Only v1 columns exist here — later
  // migrations (total_play_seconds, date_added, ...) run against the already
  // de-duplicated table. Restricted to groups with duplicates so a clean table
  // isn't rewritten row-by-row.
  QSqlQuery merge(db);
  if (!merge.exec("UPDATE items SET "
                  "play_count = (SELECT MAX(d.play_count) FROM items d "
                  "WHERE d.collection_uuid = items.collection_uuid AND d.path = items.path), "
                  "last_played = (SELECT MAX(d.last_played) FROM items d "
                  "WHERE d.collection_uuid = items.collection_uuid AND d.path = items.path), "
                  "rating = (SELECT MAX(d.rating) FROM items d "
                  "WHERE d.collection_uuid = items.collection_uuid AND d.path = items.path), "
                  "artwork_path = COALESCE(NULLIF(items.artwork_path, ''), "
                  "(SELECT MAX(d.artwork_path) FROM items d "
                  "WHERE d.collection_uuid = items.collection_uuid AND d.path = items.path "
                  "AND d.artwork_path IS NOT NULL AND d.artwork_path != '')) "
                  "WHERE rowid IN (SELECT MIN(rowid) FROM items "
                  "GROUP BY collection_uuid, path HAVING COUNT(*) > 1)")) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to merge duplicate item usage data before de-duplication",
                              origin)
            .withDetails(merge.lastError().text()));
  }

  // Keep the first row (min(rowid)) for each (collection_uuid, path); its
  // user-data columns now hold the merged values.
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
    return false;
  }
  return true;
}

// Returns false if the statement failed; callers that treat the index/table as
// required chain these with && so a failure aborts the migration block.
static auto ensureIndex(QSqlDatabase &db, const QString &sql, const QString &origin,
                        const QString &what) -> bool {
  QSqlQuery q(db);
  if (q.exec(sql)) {
    return true;
  }
  auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                   "Failed to ensure database index", origin)
                 .withDetails(QString("%1: %2").arg(what, q.lastError().text()));
  ErrorUtils::logError(err);
  return false;
}

// Runs one migration block inside a single transaction. `apply` performs the
// block's DDL/DML and returns false on any failure that must abort the
// migration. user_version is stamped INSIDE the transaction and only on
// success, so a power-loss or statement failure mid-block rolls the whole block
// back (including the version stamp) and the block retries on next launch
// (Kartend-l28zj). Returns false if the block did not fully commit.
template <typename ApplyFn>
static auto runBlock(QSqlDatabase &db, int version, const QString &origin, ApplyFn &&apply)
    -> bool {
  if (!db.transaction()) {
    ErrorUtils::logError(
        ErrorContext::warning(
            ErrorCode::DatabaseQueryFailed,
            QString("Failed to begin transaction for schema migration v%1").arg(version), origin)
            .withDetails(db.lastError().text()));
    return false;
  }
  if (!apply() || !setUserVersion(db, version)) {
    db.rollback();
    return false;
  }
  if (!db.commit()) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              QString("Failed to commit schema migration v%1").arg(version), origin)
            .withDetails(db.lastError().text()));
    db.rollback();
    return false;
  }
  return true;
}
} // namespace

namespace DbMigrations {

void applySchemaMigrations(QSqlDatabase &db, const QString &origin) {
  if (!db.isOpen()) {
    return;
  }

  // Must equal the highest migration block below. Bumping a migration without
  // bumping this leaves the early-return gate skipping the new block, so the
  // schema silently lags the code (e.g. a missing items.date_added column that
  // breaks the scanner upsert).
  constexpr int CURRENT_SCHEMA_VERSION = 23;
  const int version = getUserVersion(db);

  // Downgrade / future-version guard: a database written by a newer build
  // carries a user_version this build does not understand. Running our older
  // migrations (or queries) against it risks corruption, so log and bail —
  // mirrors the settings-INI future-version warning (Kartend-vmvzq).
  if (version > CURRENT_SCHEMA_VERSION) {
    ErrorUtils::logError(ErrorContext::warning(
        ErrorCode::DatabaseQueryFailed,
        QString("Database schema version %1 is newer than this build supports (%2); "
                "skipping migrations — a newer Kartend build may have written this database.")
            .arg(version)
            .arg(CURRENT_SCHEMA_VERSION),
        origin));
    return;
  }
  if (version >= CURRENT_SCHEMA_VERSION) {
    return;
  }

  int mutableVersion = version;

  if (mutableVersion < 1) {
    // v1: Introduce schema versioning + ensure columns needed by newer builds.
    if (!runBlock(db, 1, origin, [&]() -> bool {
          return ensureColumn(db, "collections", "ext_signature", "TEXT DEFAULT ''", origin) &&
                 ensureColumn(db, "collections", "uuid", "TEXT DEFAULT ''", origin) &&
                 ensureColumn(db, "collections", "dir_signature", "TEXT DEFAULT ''", origin) &&
                 ensureColumn(db, "items", "collection_uuid", "TEXT DEFAULT ''", origin) &&
                 ensureColumn(db, "items", "artwork_path", "TEXT", origin) &&
                 ensureColumn(db, "items", "play_count", "INTEGER DEFAULT 0", origin) &&
                 ensureColumn(db, "items", "last_played", "TEXT", origin) &&
                 ensureColumn(db, "items", "rating", "INTEGER DEFAULT 0", origin) &&
                 ensureUniqueIndexItemsUuidPath(db, origin);
        })) {
      return;
    }
    mutableVersion = 1;
  }

  if (mutableVersion < 2) {
    // v2: Add query performance indexes used by the virtual scroll + filtering
    // paths.
    if (!runBlock(db, 2, origin, [&]() -> bool {
          return ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)",
                             origin, "idx_collections_uuid") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_collection_uuid ON "
                             "items(collection_uuid)",
                             origin, "idx_items_collection_uuid") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_name ON items(name "
                             "COLLATE NOCASE)",
                             origin, "idx_items_name") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_uuid_name ON "
                             "items(collection_uuid, name COLLATE NOCASE)",
                             origin, "idx_items_uuid_name") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_covering ON "
                             "items(collection_uuid, name COLLATE NOCASE, path)",
                             origin, "idx_items_covering") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_uuid_last_modified ON "
                             "items(collection_uuid, last_modified)",
                             origin, "idx_items_uuid_last_modified");
        })) {
      return;
    }
    mutableVersion = 2;
  }

  if (mutableVersion < 3) {
    // v3: Add FTS5 index for fast name filtering.
    // This is best-effort: if SQLite is built without FTS5, keep working with
    // LIKE. FTS unavailability is NOT a migration failure — the block still
    // advances to v3 (the LIKE fallback is fully functional). The transaction
    // only guarantees the FTS-available path (table + meta + triggers) applies
    // atomically.
    if (!runBlock(db, 3, origin, [&]() -> bool {
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
            return true; // LIKE fallback is valid; advance to v3.
          }

          // Lightweight metadata store for tracking background maintenance.
          // Note: This is intentionally a plain table (not PRAGMA-based) so it
          // can evolve without bumping user_version again.
          // Mark FTS as not-yet-ready; population happens later on the scan
          // worker (ensureItemsFtsReady). Avoid blocking app startup by
          // running a full synchronous rebuild here.
          //
          // Kartend-4i5e4: the statements below are chained with && so a
          // failure rolls the whole block back and v3 retries next launch —
          // previously a discarded ensureIndex result could commit an FTS
          // table with no sync triggers, leaving the index permanently stale.
          // The triggers themselves are dropped again by v18 (and re-created
          // atomically with the index rebuild in ensureItemsFtsReady); v3
          // keeps creating them so its committed semantics stay intact for
          // databases that stop migrating mid-ladder.
          return ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, "
                             "value TEXT NOT NULL)",
                             origin, "meta") &&
                 ensureIndex(db,
                             "INSERT OR IGNORE INTO meta(key, value) "
                             "VALUES('items_fts_ready', '0')",
                             origin, "meta items_fts_ready") &&
                 ensureIndex(db,
                             "INSERT OR IGNORE INTO meta(key, value) "
                             "VALUES('items_fts_indexed_up_to_id', '0')",
                             origin, "meta items_fts_indexed_up_to_id") &&
                 // Keep the FTS index in sync with the items table.
                 ensureIndex(db, DbMigrations::kItemsFtsTriggerInsertSql, origin, "items_fts_ai") &&
                 ensureIndex(db, DbMigrations::kItemsFtsTriggerDeleteSql, origin, "items_fts_ad") &&
                 ensureIndex(db, DbMigrations::kItemsFtsTriggerUpdateSql, origin, "items_fts_au");
        })) {
      return;
    }
    mutableVersion = 3;
  }

  if (mutableVersion < 4) {
    // v4: Add file_size so item lists can sort by size without touching the
    // filesystem at query time. Existing rows backfill to 0 until the next
    // collection rescan refreshes metadata from disk.
    if (!runBlock(db, 4, origin, [&]() -> bool {
          return ensureColumn(db, "items", "file_size", "INTEGER DEFAULT 0", origin) &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_uuid_last_modified ON "
                             "items(collection_uuid, last_modified)",
                             origin, "idx_items_uuid_last_modified") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_uuid_file_size ON "
                             "items(collection_uuid, file_size)",
                             origin, "idx_items_uuid_file_size");
        })) {
      return;
    }
    mutableVersion = 4;
  }

  if (mutableVersion < 5) {
    // v5: Add item_metadata table for extended per-item metadata
    // (scraper-populated structured fields + user-defined custom fields +
    // optional manual path override). Keyed by (collection_uuid, path) so
    // entries survive item id renumbering across rescans.
    if (!runBlock(db, 5, origin, [&]() -> bool {
          return ensureIndex(db,
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
                             origin, "item_metadata") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_item_metadata_uuid_path ON "
                             "item_metadata(collection_uuid, path)",
                             origin, "idx_item_metadata_uuid_path");
        })) {
      return;
    }
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
    if (!runBlock(db, 6, origin, [&]() -> bool {
          return ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS item_artwork ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "collection_uuid TEXT NOT NULL DEFAULT '', "
                             "path TEXT NOT NULL, "
                             "artwork_type TEXT NOT NULL, "
                             "manual_path TEXT, "
                             "updated_at TEXT NOT NULL DEFAULT '', "
                             "UNIQUE(collection_uuid, path, artwork_type)"
                             ")",
                             origin, "item_artwork") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_item_artwork_uuid_path ON "
                             "item_artwork(collection_uuid, path)",
                             origin, "idx_item_artwork_uuid_path");
        })) {
      return;
    }
    mutableVersion = 6;
  }

  if (mutableVersion < 7) {
    // v7: Cumulative play-time tracking. play_count and
    // last_played already exist from v1; add total_play_seconds for the
    // runtime-detection accumulated session duration. Also
    // index last_played so the "Recently played" view can sort cheaply.
    if (!runBlock(db, 7, origin, [&]() -> bool {
          return ensureColumn(db, "items", "total_play_seconds", "INTEGER DEFAULT 0", origin) &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_last_played ON "
                             "items(last_played)",
                             origin, "idx_items_last_played") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_play_count ON "
                             "items(play_count)",
                             origin, "idx_items_play_count");
        })) {
      return;
    }
    mutableVersion = 7;
  }

  if (mutableVersion < 8) {
    // v8: Per-item launcher override. Stores a unified
    // launcher index (0 = primary, 1..N = launcher.additionalLaunchers[0..N-1]) so an
    // item can pin a specific launcher and skip the multi-launcher chooser
    // dialog. NULL means "no override" — fall through to the chooser /
    // collection default. Lives on item_metadata to share the same
    // (collection_uuid, path) key as manual_path / custom_fields.
    if (!runBlock(db, 8, origin, [&]() -> bool {
          return ensureColumn(db, "item_metadata", "launcher_index", "INTEGER", origin);
        })) {
      return;
    }
    mutableVersion = 8;
  }

  if (mutableVersion < 9) {
    // v9: Chronological launch history. Append-only log keyed
    // by an auto-incrementing id; the same (collection_uuid, path) appears
    // multiple times on purpose. `name` is denormalized at insert time so
    // rows stay readable after the source item is deleted.
    if (!runBlock(db, 9, origin, [&]() -> bool {
          return ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS launch_history ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "collection_uuid TEXT NOT NULL DEFAULT '', "
                             "path TEXT NOT NULL DEFAULT '', "
                             "name TEXT, "
                             "launched_at TEXT NOT NULL DEFAULT ''"
                             ")",
                             origin, "launch_history") &&
                 // Index for trim-to-N and "recent" queries — both walk the
                 // table in descending id order. SQLite already serves this
                 // from the implicit PK index, but an explicit launched_at
                 // index keeps any future launched_at-based queries cheap.
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_launch_history_launched_at "
                             "ON launch_history(launched_at)",
                             origin, "idx_launch_history_launched_at") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_launch_history_uuid_path "
                             "ON launch_history(collection_uuid, path)",
                             origin, "idx_launch_history_uuid_path");
        })) {
      return;
    }
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
    if (!runBlock(db, 10, origin, [&]() -> bool {
          return ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS playlists ("
                             "id TEXT PRIMARY KEY, "
                             "name TEXT NOT NULL, "
                             "icon TEXT NOT NULL DEFAULT '', "
                             "parent_collection_uuid TEXT NOT NULL DEFAULT '', "
                             "reserved_kind TEXT NOT NULL DEFAULT '', "
                             "created_at TEXT NOT NULL DEFAULT '', "
                             "updated_at TEXT NOT NULL DEFAULT ''"
                             ")",
                             origin, "playlists") &&
                 // Playlist items reference rows by (collection_uuid, path) —
                 // the same key shape used by item_metadata / item_artwork — so
                 // entries survive item id renumbering across rescans.
                 // `position` is a dense 0-based ordering controlled by the
                 // playlist editor; the (playlist_id, position) PK lets ORDER BY
                 // position serve the natural list view directly.
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
                             origin, "playlist_items") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_playlist_items_lookup "
                             "ON playlist_items(source_collection_uuid, source_path)",
                             origin, "idx_playlist_items_lookup") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_playlist_items_playlist "
                             "ON playlist_items(playlist_id)",
                             origin, "idx_playlist_items_playlist");
        })) {
      return;
    }
    mutableVersion = 10;
  }

  if (mutableVersion < 11) {
    // v11: Smart playlists. is_smart=1 marks the row as filter-driven —
    // its items are evaluated on each open from `smart_filter` (a JSON
    // serialization of SmartFilter::Filter) rather than read from
    // playlist_items. Existing rows default to 0 / '' so they remain
    // static playlists with zero behaviour change. The SmartFilter JSON
    // schema is owned by src/utils/db/smartfilter.{h,cpp}; bumping that
    // schema does NOT require a new migration here as long as the
    // smart_filter column stays TEXT.
    if (!runBlock(db, 11, origin, [&]() -> bool {
          return ensureColumn(db, "playlists", "is_smart", "INTEGER NOT NULL DEFAULT 0", origin) &&
                 ensureColumn(db, "playlists", "smart_filter", "TEXT NOT NULL DEFAULT ''", origin);
        })) {
      return;
    }
    mutableVersion = 11;
  }

  if (mutableVersion < 12) {
    // v12: items.date_added — unix epoch seconds stamped when a row is
    // first inserted by the scanner. Drives the by-date-added smart
    // playlist criterion. Existing rows default to 0; the evaluator
    // treats 0 as "unknown date" and excludes the row from recency-
    // window queries. We deliberately do NOT backfill from
    // last_modified — that's the file's mtime (when the file was last
    // touched on disk), not when the user added the item to their
    // library. Backfilling from mtime would either lie about the date
    // or, if everything's defaulted to upgrade-time, make every old
    // item appear in "Added in last 30 days" right after upgrade. The
    // 0-as-unknown sentinel is the honest answer; users see the
    // recency window populate naturally as they add new items
    // (re-scanning a known item preserves its date_added per the
    // ON CONFLICT clause in commitStagedScanResults).
    if (!runBlock(db, 12, origin, [&]() -> bool {
          return ensureColumn(db, "items", "date_added", "INTEGER NOT NULL DEFAULT 0", origin) &&
                 // Index keeps the by-date-added query (recency window scan)
                 // cheap even on libraries with tens of thousands of items.
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_items_date_added "
                             "ON items(date_added)",
                             origin, "idx_items_date_added");
        })) {
      return;
    }
    mutableVersion = 12;
  }

  if (mutableVersion < 13) {
    // v13: items.path now stores the ABSOLUTE path. Every other consumer
    // (the grid, item_metadata/item_artwork, the launch + usage-stats
    // code) already keys on absolute paths; the scanner alone stored
    // media-dir-relative paths, so UsageStatsStore's `WHERE path = ?`
    // never matched a launch's absolute path and play_count/last_played
    // were never recorded.
    //
    // rel_path keeps the media-dir-relative form, which the subfolder /
    // virtual-folder queries still need (a root-level item has no slash
    // in rel_path; a nested one is `subdir/.../file`).
    //
    // This migration only adds the column. Existing rows still hold a
    // relative `path`; QueryManager::maybeAbsolutizeItemPaths rewrites
    // them in place (and backfills rel_path) on the next load — it runs
    // there, not here, because the relative→absolute rewrite needs each
    // collection's media directory, which lives in kartend.cfg rather
    // than the database. A meta flag (items_paths_absolutized) gates it.
    if (!runBlock(db, 13, origin, [&]() -> bool {
          return ensureColumn(db, "items", "rel_path", "TEXT", origin);
        })) {
      return;
    }
    mutableVersion = 13;
  }

  if (mutableVersion < 14) {
    // v14: Per-item curation fields on item_metadata: user notes, personal
    // rating (0-10 internal, rendered as half-stars in the UI; NULL = unset),
    // and source URL (e.g. where the user found / acquired the item). Lives on
    // item_metadata so the keys stay (collection_uuid, path) — survives rescans
    // identically to tags / custom_fields / manual_path. Existing rows take
    // NULL for all three, which the load() helper maps to "unset" so the
    // sidebar can keep skipping empty fields without special-casing.
    if (!runBlock(db, 14, origin, [&]() -> bool {
          return ensureColumn(db, "item_metadata", "notes", "TEXT", origin) &&
                 ensureColumn(db, "item_metadata", "rating", "INTEGER", origin) &&
                 ensureColumn(db, "item_metadata", "source_url", "TEXT", origin);
        })) {
      return;
    }
    mutableVersion = 14;
  }

  if (mutableVersion < 15) {
    // v15: Boolean per-item state flags — is_pinned (sort-first preference),
    // is_hidden (de-emphasised in browse), continue_later (resume marker).
    // Stored as INTEGER 0/1 with DEFAULT 0 so existing rows take "off" and
    // the load() helper can read q.value().toInt() unconditionally. Lives
    // on item_metadata (same (collection_uuid, path) key as the v14 curation
    // fields) so the flags survive rescans. Favorites continue to use the
    // existing playlist mechanism (ensureFavoritesPlaylist) rather than a
    // duplicate boolean here.
    if (!runBlock(db, 15, origin, [&]() -> bool {
          return ensureColumn(db, "item_metadata", "is_pinned", "INTEGER NOT NULL DEFAULT 0",
                              origin) &&
                 ensureColumn(db, "item_metadata", "is_hidden", "INTEGER NOT NULL DEFAULT 0",
                              origin) &&
                 ensureColumn(db, "item_metadata", "continue_later", "INTEGER NOT NULL DEFAULT 0",
                              origin);
        })) {
      return;
    }
    mutableVersion = 15;
  }

  if (mutableVersion < 16) {
    // v16: file_hash_cache — content hashes (crc32/md5/sha1) keyed by
    // absolute path, validated against (file_size, mtime_unix_ms). Lets the
    // DAT auditor (and the scraper's hash-based ROM identification) skip
    // re-hashing a multi-GB ISO when neither the size nor the mtime changed
    // since the last pass. Regenerable data — a wipe just forces a re-hash —
    // but it lives in the main DB rather than a throwaway cache file so the
    // scraper's write worker and the audit worker share one store through
    // their own connections. Hash columns are nullable only as a sentinel;
    // RomHasher fills all three on success.
    if (!runBlock(db, 16, origin, [&]() -> bool {
          return ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS file_hash_cache ("
                             "path TEXT PRIMARY KEY, "
                             "file_size INTEGER NOT NULL, "
                             "mtime_unix_ms INTEGER NOT NULL, "
                             "crc TEXT, "
                             "md5 TEXT, "
                             "sha1 TEXT, "
                             "computed_at_unix_ms INTEGER NOT NULL DEFAULT 0"
                             ")",
                             origin, "file_hash_cache");
        })) {
      return;
    }
    mutableVersion = 16;
  }

  if (mutableVersion < 17) {
    // v17: DAT-audit profiles. A profile is a first-class, persisted binding
    // of one-or-more DAT catalogues to one-or-more scan roots, plus the
    // audit/fix settings. Independent of collections — collection_uuid is an
    // optional link ('' = none), so a profile can audit an arbitrary folder.
    // List-valued fields (scan_roots, region_prefs, ignore_rules) are stored
    // as compact JSON arrays in TEXT columns; the per-DAT rows live in the
    // dat_audit_profile_dat child table (ordered by position). dat_audit_result
    // is the cached per-entry status snapshot that backs the "last audited N
    // ago" view without forcing a rescan. Children cascade on profile delete;
    // the store also deletes them explicitly so correctness does not hinge on
    // the foreign_keys pragma being on.
    if (!runBlock(db, 17, origin, [&]() -> bool {
          return ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS dat_audit_profile ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "name TEXT NOT NULL, "
                             "collection_uuid TEXT NOT NULL DEFAULT '', "
                             "scan_roots TEXT NOT NULL DEFAULT '', "
                             "merge_mode TEXT NOT NULL DEFAULT 'split', "
                             "region_prefs TEXT NOT NULL DEFAULT '', "
                             "one_per_game INTEGER NOT NULL DEFAULT 0, "
                             "ignore_rules TEXT NOT NULL DEFAULT '', "
                             "fix_mode TEXT NOT NULL DEFAULT 'in_place', "
                             "managed_output_root TEXT NOT NULL DEFAULT '', "
                             "last_scan_at_unix_ms INTEGER NOT NULL DEFAULT 0, "
                             "created_at_unix_ms INTEGER NOT NULL DEFAULT 0, "
                             "updated_at_unix_ms INTEGER NOT NULL DEFAULT 0"
                             ")",
                             origin, "dat_audit_profile") &&
                 ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS dat_audit_profile_dat ("
                             "profile_id INTEGER NOT NULL, "
                             "position INTEGER NOT NULL, "
                             "dat_path TEXT NOT NULL, "
                             "dat_mtime_unix_ms INTEGER NOT NULL DEFAULT 0, "
                             "dialect INTEGER NOT NULL DEFAULT 0, "
                             "record_count INTEGER NOT NULL DEFAULT 0, "
                             "PRIMARY KEY (profile_id, position), "
                             "FOREIGN KEY (profile_id) REFERENCES dat_audit_profile(id) "
                             "  ON DELETE CASCADE"
                             ")",
                             origin, "dat_audit_profile_dat") &&
                 ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS dat_audit_result ("
                             "profile_id INTEGER NOT NULL, "
                             "entry_key TEXT NOT NULL, "
                             "status INTEGER NOT NULL, "
                             "file_path TEXT, "
                             "detail TEXT, "
                             "PRIMARY KEY (profile_id, entry_key), "
                             "FOREIGN KEY (profile_id) REFERENCES dat_audit_profile(id) "
                             "  ON DELETE CASCADE"
                             ")",
                             origin, "dat_audit_result") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_dat_audit_result_profile "
                             "ON dat_audit_result(profile_id, status)",
                             origin, "idx_dat_audit_result_profile");
        })) {
      return;
    }
    mutableVersion = 17;
  }

  if (mutableVersion < 18) {
    // v18 (Kartend-4i5e4): drop the eagerly-created items_fts sync triggers.
    // v3 created them together with the EMPTY external-content FTS table, so
    // until the deferred backfill finished, the AFTER UPDATE / AFTER DELETE
    // triggers issued FTS5 'delete' commands for rows the index never held —
    // documented FTS5 external-content corruption (newer SQLite detects it and
    // fails the triggering items write with SQLITE_CORRUPT; older SQLite lets
    // garbage postings accumulate). The triggers are re-created by
    // QueryManager::ensureItemsFtsReady inside the SAME transaction as a
    // one-shot index rebuild (INSERT INTO items_fts(items_fts)
    // VALUES('rebuild')), so from then on trigger existence implies index
    // consistency. Dropping them here also funnels every existing install —
    // including ones whose index the old interplay already damaged — through
    // that self-healing rebuild on next launch (the readiness check treats the
    // legacy items_fts_ready='1' marker as not-ready).
    //
    // Writes that land between this migration and the rebuild are simply not
    // FTS-indexed; the rebuild reads the items table and captures them. Search
    // falls back to LIKE until the rebuild completes, as it always did while
    // FTS was not ready.
    if (!runBlock(db, 18, origin, [&]() -> bool {
          return ensureIndex(db, "DROP TRIGGER IF EXISTS items_fts_ai", origin,
                             "drop items_fts_ai") &&
                 ensureIndex(db, "DROP TRIGGER IF EXISTS items_fts_ad", origin,
                             "drop items_fts_ad") &&
                 ensureIndex(db, "DROP TRIGGER IF EXISTS items_fts_au", origin,
                             "drop items_fts_au");
        })) {
      return;
    }
    mutableVersion = 18;
  }

  if (mutableVersion < 19) {
    // v19 (Kartend-m6qsb.1 / .6): two DAT-audit profile extensions.
    //
    // 1. Index dat_audit_profile.collection_uuid. The collection-seeded audit
    //    launch resolves "which profile is linked to this collection" — until
    //    now via a listAll() linear scan whose first name-order match was
    //    arbitrary when several profiles claimed one collection.
    //    DatAuditProfile::loadByCollectionUuid makes the choice deterministic
    //    (most-recently-updated wins) and this index backs it. Not UNIQUE:
    //    '' means "not linked" and may repeat freely, and pre-existing
    //    databases may legitimately hold several profiles for one collection.
    // 2. detected_layout / layout_confirmed: the folder-structure probe's
    //    persisted suggestion ('' = never detected; DatAudit::layoutToken
    //    values otherwise) and whether the user confirmed it — only a
    //    confirmed layout changes scan semantics (Kartend-m6qsb.6).
    // 3. archive_member_hash_cache: per-member hashes for archive-per-item
    //    auditing (Kartend-m6qsb.7). file_hash_cache's (path, size, mtime)
    //    key cannot represent members, so they get their own table keyed
    //    (container_path, member_path) and invalidated wholesale by the
    //    container's stamped (size, mtime). Regenerable, but lives in the
    //    main DB beside file_hash_cache so audit + scraper workers share it.
    if (!runBlock(db, 19, origin, [&]() -> bool {
          return ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_dat_audit_profile_collection "
                             "ON dat_audit_profile(collection_uuid)",
                             origin, "idx_dat_audit_profile_collection") &&
                 ensureColumn(db, "dat_audit_profile", "detected_layout",
                              "TEXT NOT NULL DEFAULT ''", origin) &&
                 ensureColumn(db, "dat_audit_profile", "layout_confirmed",
                              "INTEGER NOT NULL DEFAULT 0", origin) &&
                 ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS archive_member_hash_cache ("
                             "container_path TEXT NOT NULL, "
                             "member_path TEXT NOT NULL, "
                             "container_size INTEGER NOT NULL, "
                             "container_mtime_unix_ms INTEGER NOT NULL, "
                             "crc TEXT, "
                             "md5 TEXT, "
                             "sha1 TEXT, "
                             "member_size INTEGER NOT NULL DEFAULT -1, "
                             "computed_at_unix_ms INTEGER NOT NULL DEFAULT 0, "
                             "PRIMARY KEY (container_path, member_path)"
                             ")",
                             origin, "archive_member_hash_cache") &&
                 // 4. dat_library_dismissal: "don't ask again" state for the
                 //    DAT-library scan's proposals (Kartend-m6qsb.5), keyed by
                 //    canonical path + the dismissed revision's mtime so an
                 //    updated catalogue becomes proposable again.
                 ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS dat_library_dismissal ("
                             "path TEXT PRIMARY KEY, "
                             "mtime_unix_ms INTEGER NOT NULL DEFAULT 0, "
                             "dismissed_at_unix_ms INTEGER NOT NULL DEFAULT 0"
                             ")",
                             origin, "dat_library_dismissal");
        })) {
      return;
    }
    mutableVersion = 19;
  }

  if (mutableVersion < 20) {
    // v20 (Kartend-m6qsb.13): drop the inert merge_mode column. MergeMode
    // (split/merged/non-merged) was persisted on every profile (v17) but never
    // read by the audit module — its UI control and the Profile struct field
    // are now removed, leaving the column with no reader. It's a plain
    // unindexed TEXT column, so ALTER TABLE DROP COLUMN succeeds outright.
    if (!runBlock(db, 20, origin, [&]() -> bool {
          return dropColumn(db, "dat_audit_profile", "merge_mode", origin);
        })) {
      return;
    }
    mutableVersion = 20;
  }

  if (mutableVersion < 21) {
    // v21 (Kartend-m6qsb.26): dat_library_provenance — where each library DAT
    // came from, recorded at download/import time, so a later "check for
    // updates" pass can ask the source whether a newer revision exists. Keyed
    // by canonical path (like dat_library_dismissal); 'source' is '', 'nointro'
    // or 'redump'; 'slug'/'system_id' identify it within the source; 'version'
    // is the revision stored at fetch time (No-Intro pack date / Redump header
    // version) that the update check compares against.
    if (!runBlock(db, 21, origin, [&]() -> bool {
          return ensureIndex(db,
                             "CREATE TABLE IF NOT EXISTS dat_library_provenance ("
                             "canonical_path TEXT PRIMARY KEY, "
                             "source TEXT NOT NULL DEFAULT '', "
                             "slug TEXT NOT NULL DEFAULT '', "
                             "system_id INTEGER NOT NULL DEFAULT 0, "
                             "version TEXT NOT NULL DEFAULT '', "
                             "updated_at_unix_ms INTEGER NOT NULL DEFAULT 0"
                             ")",
                             origin, "dat_library_provenance");
        })) {
      return;
    }
    mutableVersion = 21;
  }

  if (mutableVersion < 22) {
    // v22 (Kartend-34lab): give each persisted audit-result row the source DAT
    // + game it belongs to, plus the MIA flag, so the RomVault-style browser's
    // global tree (per-source rollups) and game list (per-game rollups) are
    // simple grouped queries. The entry_key carries no game/source identity, so
    // without these the right panes couldn't attribute a row unambiguously.
    // Pre-v22 snapshots leave these empty (source_name = '') and fall back to
    // profile-level totals until the profile is next re-audited.
    if (!runBlock(db, 22, origin, [&]() -> bool {
          return ensureColumn(db, "dat_audit_result", "source_name", "TEXT NOT NULL DEFAULT ''",
                              origin) &&
                 ensureColumn(db, "dat_audit_result", "game_name", "TEXT NOT NULL DEFAULT ''",
                              origin) &&
                 ensureColumn(db, "dat_audit_result", "mia", "INTEGER NOT NULL DEFAULT 0",
                              origin) &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_dat_audit_result_source "
                             "ON dat_audit_result(profile_id, source_name, status)",
                             origin, "idx_dat_audit_result_source") &&
                 ensureIndex(db,
                             "CREATE INDEX IF NOT EXISTS idx_dat_audit_result_game "
                             "ON dat_audit_result(profile_id, source_name, game_name)",
                             origin, "idx_dat_audit_result_game");
        })) {
      return;
    }
    mutableVersion = 22; // read by the v23 block below
  }

  if (mutableVersion < 23) {
    // v23: per-collection quarantine folder on the audit profile. Remembers the
    // Fix dialog's quarantine target per profile so each collection keeps its
    // own, falling back to the global default when empty. Pre-v23 profiles
    // default to '' and use the global default.
    if (!runBlock(db, 23, origin, [&]() -> bool {
          return ensureColumn(db, "dat_audit_profile", "quarantine_root",
                              "TEXT NOT NULL DEFAULT ''", origin);
        })) {
      return;
    }
    // Final block: stamping the in-memory tracker is a dead store (no later
    // block reads it) — kept so adding a v24 block stays a pure copy-paste.
    mutableVersion = 23; // NOLINT(clang-analyzer-deadcode.DeadStores)
  }
}

} // namespace DbMigrations
