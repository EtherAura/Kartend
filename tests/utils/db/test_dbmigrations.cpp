// Tests for DbMigrations::applySchemaMigrations.
//
// Uses an in-memory SQLite database — no filesystem, no Qt event loop.
// Verifies idempotency, version progression, and column/index creation.

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTest>

#include "dbmigrations.h"

namespace {

// Each test gets its own connection name so they don't share state.
auto openMemoryDb(const QString &connName) -> QSqlDatabase {
  auto db = QSqlDatabase::addDatabase("QSQLITE", connName);
  db.setDatabaseName(":memory:");
  if (!db.open()) {
    qWarning("Failed to open in-memory db: %s", qPrintable(db.lastError().text()));
  }
  return db;
}

auto closeAndRemove(QSqlDatabase &db, const QString &connName) -> void {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connName);
}

// Mimic the minimal pre-migration schema that v1+ migrations expect to find
// already in place (the "v0" schema seen by older builds).
auto createBaseSchema(QSqlDatabase &db) -> void {
  QSqlQuery q(db);
  QVERIFY(q.exec("CREATE TABLE collections ("
                 "id INTEGER PRIMARY KEY, "
                 "name TEXT NOT NULL"
                 ")"));
  QVERIFY(q.exec("CREATE TABLE items ("
                 "id INTEGER PRIMARY KEY, "
                 "name TEXT NOT NULL, "
                 "path TEXT NOT NULL, "
                 "last_modified TEXT"
                 ")"));
}

auto getUserVersion(QSqlDatabase &db) -> int {
  QSqlQuery q(db);
  if (q.exec("PRAGMA user_version") && q.next()) {
    return q.value(0).toInt();
  }
  return -1;
}

auto tableHasColumn(QSqlDatabase &db, const QString &table, const QString &column) -> bool {
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

auto indexExists(QSqlDatabase &db, const QString &indexName) -> bool {
  QSqlQuery q(db);
  q.prepare("SELECT name FROM sqlite_master WHERE type='index' AND name=?");
  q.addBindValue(indexName);
  return q.exec() && q.next();
}

auto tableExists(QSqlDatabase &db, const QString &tableName) -> bool {
  QSqlQuery q(db);
  q.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?");
  q.addBindValue(tableName);
  return q.exec() && q.next();
}

auto triggerExists(QSqlDatabase &db, const QString &triggerName) -> bool {
  QSqlQuery q(db);
  q.prepare("SELECT name FROM sqlite_master WHERE type='trigger' AND name=?");
  q.addBindValue(triggerName);
  return q.exec() && q.next();
}

} // namespace

class TestDbMigrations : public QObject {
  Q_OBJECT
private slots:
  void noopOnClosedDb();
  void appliesToCurrentVersion();
  void isIdempotent();
  void v1AddsCollectionColumns();
  void v1AddsItemColumns();
  void v1AddsUniqueItemsIndex();
  void v1DeduplicatesBeforeUniqueIndex();
  void v1DeduplicateMergesUsageData();
  void v2AddsPerformanceIndexes();
  void v3AddsMetaTable();
  void v4AddsFileSizeColumnAndIndex();
  void v5AddsItemMetadataTable();
  void v6AddsItemArtworkTable();
  void v7AddsUsageStatsColumnAndIndexes();
  void v8AddsLauncherIndexColumn();
  void v9AddsLaunchHistoryTable();
  void v10AddsPlaylistTables();
  void v11AddsSmartPlaylistColumns();
  void v12AddsDateAddedColumn();
  void v12LeavesExistingRowsAsUnknownDate();
  void v13AddsRelPathColumn();
  void v14AddsCurationColumns();
  void v15AddsStateFlagColumns();
  void v16AddsFileHashCacheTable();
  void v17AddsDatAuditProfileTables();
  void v18DropsEagerFtsSyncTriggers();
  void v19AddsDatAuditProfileCollectionIndex();
  void v20DropsInertMergeModeColumn();
  void v21AddsDatLibraryProvenanceTable();
  void v22AddsAuditResultIdentityColumns();
  void v23AddsProfileQuarantineRootColumn();
  void v24AddsZipIndexColumns();
  void v24ClearsPopulatedMemberCache();
  void v25AddsProfileCategoryColumn();
  void preservesExistingDataAcrossUpgrade();
  void failedBlockRollsBackAndKeepsVersion();
  void newerSchemaVersionIsLeftUntouched();
};

void TestDbMigrations::noopOnClosedDb() {
  // A closed database must be a hard no-op (no crash, no version bump).
  QSqlDatabase db; // default-constructed -> not open
  DbMigrations::applySchemaMigrations(db, "test");
  // No assertions needed beyond "did not crash".
  QVERIFY(!db.isOpen());
}

void TestDbMigrations::appliesToCurrentVersion() {
  const QString conn = "test_apply";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createBaseSchema(db);

  QCOMPARE(getUserVersion(db), 0);
  DbMigrations::applySchemaMigrations(db, "test");
  // Must equal CURRENT_SCHEMA_VERSION in dbmigrations.cpp.
  QCOMPARE(getUserVersion(db), 25);

  closeAndRemove(db, conn);
}

void TestDbMigrations::isIdempotent() {
  const QString conn = "test_idempotent";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createBaseSchema(db);

  DbMigrations::applySchemaMigrations(db, "test");
  const int firstVersion = getUserVersion(db);

  // Re-applying must not change version, throw, or duplicate columns.
  DbMigrations::applySchemaMigrations(db, "test");
  DbMigrations::applySchemaMigrations(db, "test");
  QCOMPARE(getUserVersion(db), firstVersion);

  closeAndRemove(db, conn);
}

void TestDbMigrations::v1AddsCollectionColumns() {
  const QString conn = "test_v1_collections";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QVERIFY(tableHasColumn(db, "collections", "ext_signature"));
  QVERIFY(tableHasColumn(db, "collections", "uuid"));
  QVERIFY(tableHasColumn(db, "collections", "dir_signature"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v1AddsItemColumns() {
  const QString conn = "test_v1_items";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QVERIFY(tableHasColumn(db, "items", "collection_uuid"));
  QVERIFY(tableHasColumn(db, "items", "artwork_path"));
  QVERIFY(tableHasColumn(db, "items", "play_count"));
  QVERIFY(tableHasColumn(db, "items", "last_played"));
  QVERIFY(tableHasColumn(db, "items", "rating"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v1AddsUniqueItemsIndex() {
  const QString conn = "test_v1_unique";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QVERIFY(indexExists(db, "uniq_items_uuid_path"));

  // Verify the constraint is real: insert duplicates should fail.
  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO items (name, path, collection_uuid) "
                 "VALUES ('a', '/p', 'u1')"));
  // Second insert with same (collection_uuid, path) must be rejected.
  const bool dupAccepted = q.exec("INSERT INTO items (name, path, "
                                  "collection_uuid) VALUES ('b', '/p', 'u1')");
  QVERIFY(!dupAccepted);

  closeAndRemove(db, conn);
}

void TestDbMigrations::v1DeduplicatesBeforeUniqueIndex() {
  // Pre-seed the items table with duplicates and confirm migration
  // de-duplicates them so the unique index can be created.
  const QString conn = "test_v1_dedupe";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);

  // Add the collection_uuid column manually so we can pre-populate duplicates
  // before migrations run (mimicking a partially-upgraded older DB).
  QSqlQuery q(db);
  QVERIFY(q.exec("ALTER TABLE items ADD COLUMN collection_uuid TEXT DEFAULT ''"));
  QVERIFY(q.exec("INSERT INTO items (name, path, collection_uuid) "
                 "VALUES ('a', '/dup', 'u1')"));
  QVERIFY(q.exec("INSERT INTO items (name, path, collection_uuid) "
                 "VALUES ('a-copy', '/dup', 'u1')"));
  QVERIFY(q.exec("INSERT INTO items (name, path, collection_uuid) "
                 "VALUES ('b', '/uniq', 'u1')"));

  DbMigrations::applySchemaMigrations(db, "test");

  QVERIFY(indexExists(db, "uniq_items_uuid_path"));

  // After dedupe: 2 rows total (one for /dup, one for /uniq).
  QVERIFY(q.exec("SELECT COUNT(*) FROM items WHERE collection_uuid='u1'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);

  closeAndRemove(db, conn);
}

void TestDbMigrations::v1DeduplicateMergesUsageData() {
  // Regression for Kartend-l793: the dedup must not silently drop usage/rating
  // data carried by a later duplicate when the surviving (min rowid) row lacks
  // it. Seed the survivor with LOW values and a duplicate with HIGH values, and
  // confirm the survivor ends up with the merged (MAX) values.
  const QString conn = "test_v1_dedupe_merge";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);

  QSqlQuery q(db);
  // Pre-add the v1 user-data columns so duplicates can carry diverging values
  // before migrations run (mimicking an older DB that already had them).
  QVERIFY(q.exec("ALTER TABLE items ADD COLUMN collection_uuid TEXT DEFAULT ''"));
  QVERIFY(q.exec("ALTER TABLE items ADD COLUMN artwork_path TEXT"));
  QVERIFY(q.exec("ALTER TABLE items ADD COLUMN play_count INTEGER DEFAULT 0"));
  QVERIFY(q.exec("ALTER TABLE items ADD COLUMN last_played TEXT"));
  QVERIFY(q.exec("ALTER TABLE items ADD COLUMN rating INTEGER DEFAULT 0"));

  // Survivor (min rowid) is the empty first-inserted row; the duplicate carries
  // the real usage data that the old code would have dropped.
  QVERIFY(q.exec("INSERT INTO items (name, path, collection_uuid, play_count, last_played, rating, "
                 "artwork_path) VALUES ('a', '/dup', 'u1', 0, NULL, 0, NULL)"));
  QVERIFY(q.exec("INSERT INTO items (name, path, collection_uuid, play_count, last_played, rating, "
                 "artwork_path) VALUES ('a-copy', '/dup', 'u1', 7, '2026-05-30T10:00:00Z', 4, "
                 "'/art/a.png')"));
  QVERIFY(q.exec("INSERT INTO items (name, path, collection_uuid, play_count) "
                 "VALUES ('b', '/uniq', 'u1', 3)"));

  DbMigrations::applySchemaMigrations(db, "test");

  QVERIFY(indexExists(db, "uniq_items_uuid_path"));

  // One row survives for /dup, and it carries the merged usage data.
  QVERIFY(q.exec("SELECT play_count, last_played, rating, artwork_path FROM items "
                 "WHERE collection_uuid='u1' AND path='/dup'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 7);
  QCOMPARE(q.value(1).toString(), QStringLiteral("2026-05-30T10:00:00Z"));
  QCOMPARE(q.value(2).toInt(), 4);
  QCOMPARE(q.value(3).toString(), QStringLiteral("/art/a.png"));
  QVERIFY(!q.next()); // exactly one /dup row remains

  // The unique row is untouched.
  QVERIFY(q.exec("SELECT play_count FROM items WHERE collection_uuid='u1' AND path='/uniq'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 3);

  closeAndRemove(db, conn);
}

void TestDbMigrations::v2AddsPerformanceIndexes() {
  const QString conn = "test_v2_indexes";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QVERIFY(indexExists(db, "idx_collections_uuid"));
  QVERIFY(indexExists(db, "idx_items_collection_uuid"));
  QVERIFY(indexExists(db, "idx_items_name"));
  QVERIFY(indexExists(db, "idx_items_uuid_name"));
  QVERIFY(indexExists(db, "idx_items_covering"));
  QVERIFY(indexExists(db, "idx_items_uuid_last_modified"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v3AddsMetaTable() {
  // v3 attempts to create FTS5 + a meta table. FTS5 may or may not be
  // available depending on the SQLite build; the meta table should exist
  // when FTS5 succeeds. We assert version reaches 3 either way (FTS failure
  // does not block the version bump because v3 logic only emits warnings).
  const QString conn = "test_v3_meta";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);

  // If FTS5 is available, the meta table should also exist.
  if (tableExists(db, "items_fts")) {
    QVERIFY(tableExists(db, "meta"));
  }

  closeAndRemove(db, conn);
}

void TestDbMigrations::v4AddsFileSizeColumnAndIndex() {
  const QString conn = "test_v4_file_size";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "items", "file_size"));
  QVERIFY(indexExists(db, "idx_items_uuid_file_size"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v5AddsItemMetadataTable() {
  const QString conn = "test_v5_item_metadata";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableExists(db, "item_metadata"));
  // Required scraper-facing columns and feature-reserved columns.
  QVERIFY(tableHasColumn(db, "item_metadata", "collection_uuid"));
  QVERIFY(tableHasColumn(db, "item_metadata", "path"));
  QVERIFY(tableHasColumn(db, "item_metadata", "description"));
  QVERIFY(tableHasColumn(db, "item_metadata", "genre"));
  QVERIFY(tableHasColumn(db, "item_metadata", "runtime_seconds"));
  QVERIFY(tableHasColumn(db, "item_metadata", "custom_fields"));
  QVERIFY(tableHasColumn(db, "item_metadata", "manual_path"));
  QVERIFY(indexExists(db, "idx_item_metadata_uuid_path"));

  // Unique (collection_uuid, path) constraint must reject duplicates.
  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO item_metadata (collection_uuid, path, updated_at) "
                 "VALUES ('u1', '/m/1', '2026-01-01')"));
  const bool dupAccepted = q.exec("INSERT INTO item_metadata (collection_uuid, path, updated_at) "
                                  "VALUES ('u1', '/m/1', '2026-01-02')");
  QVERIFY(!dupAccepted);

  closeAndRemove(db, conn);
}

void TestDbMigrations::v6AddsItemArtworkTable() {
  const QString conn = "test_v6_item_artwork";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableExists(db, "item_artwork"));
  QVERIFY(tableHasColumn(db, "item_artwork", "collection_uuid"));
  QVERIFY(tableHasColumn(db, "item_artwork", "path"));
  QVERIFY(tableHasColumn(db, "item_artwork", "artwork_type"));
  QVERIFY(tableHasColumn(db, "item_artwork", "manual_path"));
  QVERIFY(tableHasColumn(db, "item_artwork", "updated_at"));
  QVERIFY(indexExists(db, "idx_item_artwork_uuid_path"));

  // Unique (collection_uuid, path, artwork_type) constraint must reject
  // duplicates while still allowing different types for the same item.
  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO item_artwork (collection_uuid, path, "
                 "artwork_type, updated_at) VALUES ('u1', '/i/1', 'box', "
                 "'2026-01-01')"));
  // Same (uuid, path, type) -> rejected.
  const bool dupAccepted = q.exec("INSERT INTO item_artwork (collection_uuid, path, "
                                  "artwork_type, updated_at) VALUES ('u1', "
                                  "'/i/1', 'box', '2026-01-02')");
  QVERIFY(!dupAccepted);
  // Same (uuid, path) but different type -> allowed.
  QVERIFY(q.exec("INSERT INTO item_artwork (collection_uuid, path, "
                 "artwork_type, updated_at) VALUES ('u1', '/i/1', "
                 "'screenshot', '2026-01-02')"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v7AddsUsageStatsColumnAndIndexes() {
  const QString conn = "test_v7_usage";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  // Cumulative play-time column added in v7.
  QVERIFY(tableHasColumn(db, "items", "total_play_seconds"));
  // Indexes used by the Most-played / Recently-played dialog tabs.
  QVERIFY(indexExists(db, "idx_items_last_played"));
  QVERIFY(indexExists(db, "idx_items_play_count"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v8AddsLauncherIndexColumn() {
  const QString conn = "test_v8_launcher";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  // Per-item launcher override column added in v8.
  QVERIFY(tableHasColumn(db, "item_metadata", "launcher_index"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v9AddsLaunchHistoryTable() {
  const QString conn = "test_v9_history";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  // Append-only history table added in v9.
  QVERIFY(tableExists(db, "launch_history"));
  QVERIFY(tableHasColumn(db, "launch_history", "id"));
  QVERIFY(tableHasColumn(db, "launch_history", "collection_uuid"));
  QVERIFY(tableHasColumn(db, "launch_history", "path"));
  QVERIFY(tableHasColumn(db, "launch_history", "name"));
  QVERIFY(tableHasColumn(db, "launch_history", "launched_at"));
  QVERIFY(indexExists(db, "idx_launch_history_launched_at"));
  QVERIFY(indexExists(db, "idx_launch_history_uuid_path"));

  // The table is intentionally append-only: duplicate (uuid, path) rows
  // must succeed because the dialog needs to show repeated launches as
  // distinct entries.
  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO launch_history (collection_uuid, path, name, "
                 "launched_at) VALUES ('u1', '/g/1', 'Game', "
                 "'2026-05-02T12:00:00Z')"));
  QVERIFY(q.exec("INSERT INTO launch_history (collection_uuid, path, name, "
                 "launched_at) VALUES ('u1', '/g/1', 'Game', "
                 "'2026-05-02T12:01:00Z')"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v10AddsPlaylistTables() {
  // playlists + playlist_items added in v10. Both tables and
  // their lookup indexes must exist after migration; the FK cascade on
  // playlist_items.playlist_id is exercised end-to-end in
  // test_playlistmanager.cpp's deletePlaylist_cascadesItems().
  const QString conn = "test_v10_playlists";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableExists(db, "playlists"));
  QVERIFY(tableHasColumn(db, "playlists", "id"));
  QVERIFY(tableHasColumn(db, "playlists", "name"));
  QVERIFY(tableHasColumn(db, "playlists", "icon"));
  QVERIFY(tableHasColumn(db, "playlists", "parent_collection_uuid"));
  QVERIFY(tableHasColumn(db, "playlists", "reserved_kind"));
  QVERIFY(tableHasColumn(db, "playlists", "created_at"));
  QVERIFY(tableHasColumn(db, "playlists", "updated_at"));

  QVERIFY(tableExists(db, "playlist_items"));
  QVERIFY(tableHasColumn(db, "playlist_items", "playlist_id"));
  QVERIFY(tableHasColumn(db, "playlist_items", "position"));
  QVERIFY(tableHasColumn(db, "playlist_items", "source_collection_uuid"));
  QVERIFY(tableHasColumn(db, "playlist_items", "source_path"));
  QVERIFY(tableHasColumn(db, "playlist_items", "added_at"));

  QVERIFY(indexExists(db, "idx_playlist_items_lookup"));
  QVERIFY(indexExists(db, "idx_playlist_items_playlist"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v12AddsDateAddedColumn() {
  // date_added column + idx_items_date_added must exist after migration,
  // and rows inserted post-migration default to 0 so the by-date-added
  // evaluator can distinguish them from backfilled rows.
  const QString conn = "test_v12_date_added";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "items", "date_added"));
  QVERIFY(indexExists(db, "idx_items_date_added"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v12LeavesExistingRowsAsUnknownDate() {
  // v12 deliberately doesn't backfill date_added from last_modified —
  // file mtime != when-user-added-to-library, so any backfill would lie.
  // Existing rows must come out with date_added=0 ("unknown date"),
  // which the by-date-added evaluator excludes from recency-window
  // queries. New rows inserted after migration get a real epoch from
  // the scanner.
  const QString conn = "test_v12_unknown_date";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);

  QSqlQuery insert(db);
  QVERIFY(insert.exec("INSERT INTO items (name, path, last_modified) "
                      "VALUES ('Existing', '/existing.mp4', '2020-01-01T00:00:00Z')"));

  DbMigrations::applySchemaMigrations(db, "test");

  QSqlQuery sel(db);
  QVERIFY(sel.exec("SELECT date_added FROM items WHERE path = '/existing.mp4'"));
  QVERIFY(sel.next());
  QCOMPARE(sel.value(0).toLongLong(), qint64(0));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v13AddsRelPathColumn() {
  // v13 adds items.rel_path (the media-dir-relative path) so items.path can
  // store the ABSOLUTE path app-wide. The migration is purely additive — it
  // only adds the column; QueryManager::maybeAbsolutizeItemPaths rewrites
  // existing rows on the next load. Existing rows must survive the ALTER
  // TABLE with rel_path defaulting to NULL.
  const QString conn = "test_v13_rel_path";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);

  QSqlQuery insert(db);
  QVERIFY(insert.exec("INSERT INTO items (name, path, last_modified) "
                      "VALUES ('Existing', 'sub/clip.mp4', '2020-01-01T00:00:00Z')"));

  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "items", "rel_path"));

  // The pre-existing row survives and its rel_path defaults to NULL — the
  // reconcile (not this migration) is what backfills it.
  QSqlQuery sel(db);
  QVERIFY(sel.exec("SELECT path, rel_path FROM items WHERE name = 'Existing'"));
  QVERIFY(sel.next());
  QCOMPARE(sel.value(0).toString(), QStringLiteral("sub/clip.mp4"));
  QVERIFY(sel.value(1).isNull());

  closeAndRemove(db, conn);
}

void TestDbMigrations::v14AddsCurationColumns() {
  // v14 adds per-item curation fields on item_metadata: notes (free-form
  // text), rating (0-10 internal NULLable), and source_url. Existing v13
  // rows must survive the ALTER TABLE with NULL defaults.
  const QString conn = "test_v14_curation";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "item_metadata", "notes"));
  QVERIFY(tableHasColumn(db, "item_metadata", "rating"));
  QVERIFY(tableHasColumn(db, "item_metadata", "source_url"));

  // An existing row inserted without the new columns must come back with
  // all three as NULL so load() maps them to "unset" sentinels.
  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO item_metadata (collection_uuid, path, updated_at) "
                 "VALUES ('u1', '/m/curated', '2026-01-01')"));
  QVERIFY(q.exec("SELECT notes, rating, source_url FROM item_metadata "
                 "WHERE collection_uuid='u1' AND path='/m/curated'"));
  QVERIFY(q.next());
  QVERIFY(q.value(0).isNull());
  QVERIFY(q.value(1).isNull());
  QVERIFY(q.value(2).isNull());

  closeAndRemove(db, conn);
}

void TestDbMigrations::v15AddsStateFlagColumns() {
  // v15 adds boolean per-item state flags on item_metadata: is_pinned,
  // is_hidden, continue_later. All default to 0 via NOT NULL DEFAULT 0 so
  // existing rows that predate this migration come out as "off" without
  // any backfill SQL.
  const QString conn = "test_v15_state_flags";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "item_metadata", "is_pinned"));
  QVERIFY(tableHasColumn(db, "item_metadata", "is_hidden"));
  QVERIFY(tableHasColumn(db, "item_metadata", "continue_later"));

  // A row inserted without the flag columns must default to 0 for all three
  // — the load() helper relies on that to map to bool false.
  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO item_metadata (collection_uuid, path, updated_at) "
                 "VALUES ('u1', '/m/flagged', '2026-01-01')"));
  QVERIFY(q.exec("SELECT is_pinned, is_hidden, continue_later FROM item_metadata "
                 "WHERE collection_uuid='u1' AND path='/m/flagged'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
  QCOMPARE(q.value(1).toInt(), 0);
  QCOMPARE(q.value(2).toInt(), 0);

  closeAndRemove(db, conn);
}

void TestDbMigrations::v16AddsFileHashCacheTable() {
  // v16 adds the file_hash_cache table (path PK + size/mtime validation key +
  // crc/md5/sha1). Backs DAT auditing + scraper hash reuse.
  const QString conn = "test_v16_filehashcache";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableExists(db, "file_hash_cache"));
  QVERIFY(tableHasColumn(db, "file_hash_cache", "path"));
  QVERIFY(tableHasColumn(db, "file_hash_cache", "file_size"));
  QVERIFY(tableHasColumn(db, "file_hash_cache", "mtime_unix_ms"));
  QVERIFY(tableHasColumn(db, "file_hash_cache", "crc"));
  QVERIFY(tableHasColumn(db, "file_hash_cache", "md5"));
  QVERIFY(tableHasColumn(db, "file_hash_cache", "sha1"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v17AddsDatAuditProfileTables() {
  // v17 adds the DAT-audit profile cluster: dat_audit_profile (+ list-valued
  // JSON columns), the ordered dat_audit_profile_dat child table, and the
  // dat_audit_result snapshot table.
  const QString conn = "test_v17_datauditprofile";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableExists(db, "dat_audit_profile"));
  QVERIFY(tableExists(db, "dat_audit_profile_dat"));
  QVERIFY(tableExists(db, "dat_audit_result"));
  QVERIFY(tableHasColumn(db, "dat_audit_profile", "collection_uuid"));
  QVERIFY(tableHasColumn(db, "dat_audit_profile", "fix_mode"));
  QVERIFY(tableHasColumn(db, "dat_audit_profile", "managed_output_root"));
  QVERIFY(tableHasColumn(db, "dat_audit_profile_dat", "position"));
  QVERIFY(indexExists(db, "idx_dat_audit_result_profile"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v11AddsSmartPlaylistColumns() {
  // is_smart + smart_filter columns added in v11. Both must be additive
  // so existing v10 playlist rows survive the upgrade with default
  // values (is_smart=0, smart_filter='').
  const QString conn = "test_v11_smart";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "playlists", "is_smart"));
  QVERIFY(tableHasColumn(db, "playlists", "smart_filter"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v18DropsEagerFtsSyncTriggers() {
  // v18 (Kartend-4i5e4): after the full ladder, the items_fts sync triggers
  // must NOT exist — v3 creates them eagerly (alongside an EMPTY
  // external-content index, where their FTS 'delete' commands could corrupt
  // the index), and v18 drops them again. They are re-created at runtime by
  // QueryManager::ensureItemsFtsReady, atomically with the one-shot rebuild.
  const QString conn = "test_v18_fts_triggers";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(!triggerExists(db, "items_fts_ai"));
  QVERIFY(!triggerExists(db, "items_fts_ad"));
  QVERIFY(!triggerExists(db, "items_fts_au"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v19AddsDatAuditProfileCollectionIndex() {
  // v19 (Kartend-m6qsb.1): the collection_uuid lookup index backing
  // DatAuditProfile::loadByCollectionUuid. Plain (non-unique) index — '' means
  // "not linked" and repeats freely.
  const QString conn = "test_v19_dat_audit_collection_idx";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(indexExists(db, "idx_dat_audit_profile_collection"));
  // Folder-structure probe persistence (Kartend-m6qsb.6) ships in the same
  // version block.
  QVERIFY(tableHasColumn(db, "dat_audit_profile", "detected_layout"));
  QVERIFY(tableHasColumn(db, "dat_audit_profile", "layout_confirmed"));
  // Archive-member hash cache (Kartend-m6qsb.7), keyed by container+member.
  QVERIFY(tableExists(db, "archive_member_hash_cache"));
  QVERIFY(tableHasColumn(db, "archive_member_hash_cache", "container_mtime_unix_ms"));
  QVERIFY(tableHasColumn(db, "archive_member_hash_cache", "member_path"));
  // DAT-library dismissal state (Kartend-m6qsb.5).
  QVERIFY(tableExists(db, "dat_library_dismissal"));
  QVERIFY(tableHasColumn(db, "dat_library_dismissal", "mtime_unix_ms"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v20DropsInertMergeModeColumn() {
  // v20 (Kartend-m6qsb.13): the inert merge_mode column is dropped.
  // Fresh DB: v17 creates the column, v20 drops it, so it never survives to the
  // current schema while the rest of the profile table stays intact.
  {
    const QString conn = "test_v20_fresh";
    auto db = openMemoryDb(conn);
    createBaseSchema(db);
    DbMigrations::applySchemaMigrations(db, "test");
    QCOMPARE(getUserVersion(db), 25);
    QVERIFY(!tableHasColumn(db, "dat_audit_profile", "merge_mode"));
    QVERIFY(tableHasColumn(db, "dat_audit_profile", "collection_uuid"));
    QVERIFY(tableHasColumn(db, "dat_audit_profile", "detected_layout"));
    closeAndRemove(db, conn);
  }
  // Upgrade path: a v19 database with a profile row carrying merge_mode data.
  // The column must drop and the row's other data survive.
  {
    const QString conn = "test_v20_upgrade";
    auto db = openMemoryDb(conn);
    createBaseSchema(db);
    QSqlQuery q(db);
    QVERIFY(q.exec("CREATE TABLE dat_audit_profile (id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
                   "merge_mode TEXT NOT NULL DEFAULT 'split')"));
    QVERIFY(
        q.exec("INSERT INTO dat_audit_profile (name, merge_mode) VALUES ('Keep me', 'merged')"));
    // A real v19 database also has dat_audit_result (created in the v19 block);
    // include it so the later v22 migration's ALTER TABLE has a table to extend.
    QVERIFY(q.exec("CREATE TABLE dat_audit_result ("
                   "profile_id INTEGER NOT NULL, entry_key TEXT NOT NULL, status INTEGER NOT NULL, "
                   "file_path TEXT, detail TEXT, PRIMARY KEY (profile_id, entry_key))"));
    QVERIFY(q.exec("PRAGMA user_version = 19"));
    DbMigrations::applySchemaMigrations(db, "test");
    QCOMPARE(getUserVersion(db), 25);
    QVERIFY(!tableHasColumn(db, "dat_audit_profile", "merge_mode"));
    QVERIFY(q.exec("SELECT name FROM dat_audit_profile WHERE name = 'Keep me'"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("Keep me"));
    closeAndRemove(db, conn);
  }
}

void TestDbMigrations::v21AddsDatLibraryProvenanceTable() {
  // v21 (Kartend-m6qsb.26): the dat_library_provenance table backing the
  // "check for updates" feature.
  const QString conn = "test_v21_provenance";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableExists(db, "dat_library_provenance"));
  QVERIFY(tableHasColumn(db, "dat_library_provenance", "canonical_path"));
  QVERIFY(tableHasColumn(db, "dat_library_provenance", "source"));
  QVERIFY(tableHasColumn(db, "dat_library_provenance", "slug"));
  QVERIFY(tableHasColumn(db, "dat_library_provenance", "system_id"));
  QVERIFY(tableHasColumn(db, "dat_library_provenance", "version"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v22AddsAuditResultIdentityColumns() {
  // v22: each persisted audit-result row carries the source DAT + game it
  // belongs to plus the MIA flag, so the browser's per-source/per-game rollups
  // are simple grouped queries.
  const QString conn = "test_v22_audit_result_identity";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "dat_audit_result", "source_name"));
  QVERIFY(tableHasColumn(db, "dat_audit_result", "game_name"));
  QVERIFY(tableHasColumn(db, "dat_audit_result", "mia"));
  QVERIFY(indexExists(db, "idx_dat_audit_result_source"));
  QVERIFY(indexExists(db, "idx_dat_audit_result_game"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v23AddsProfileQuarantineRootColumn() {
  // v23: per-collection quarantine folder remembered on each audit profile.
  const QString conn = "test_v23_profile_quarantine_root";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "dat_audit_profile", "quarantine_root"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v24AddsZipIndexColumns() {
  // v24 (Kartend-7iqhl.4): the archive member index lands on both the persisted
  // result snapshot and the member hash cache so the browser can show ZipIndex.
  const QString conn = "test_v24_zip_index";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "dat_audit_result", "zip_index"));
  QVERIFY(tableHasColumn(db, "archive_member_hash_cache", "zip_index"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v24ClearsPopulatedMemberCache() {
  // v24 clears archive_member_hash_cache so warm pre-v24 rows (which carry no
  // real zip_index) get re-hashed instead of persisting -1 (Kartend-7iqhl.4).
  const QString conn = "test_v24_clear_cache";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  QSqlQuery q(db);
  // A real pre-v24 DB has both tables (created in earlier blocks); mirror the
  // minimum the v24 block touches, with one stale member row already cached.
  QVERIFY(q.exec("CREATE TABLE dat_audit_result ("
                 "profile_id INTEGER NOT NULL, entry_key TEXT NOT NULL, status INTEGER NOT NULL, "
                 "file_path TEXT, detail TEXT, PRIMARY KEY (profile_id, entry_key))"));
  QVERIFY(q.exec("CREATE TABLE archive_member_hash_cache ("
                 "container_path TEXT NOT NULL, member_path TEXT NOT NULL)"));
  // The later v25 block ALTERs dat_audit_profile, so it must exist too.
  QVERIFY(q.exec("CREATE TABLE dat_audit_profile (id INTEGER PRIMARY KEY, name TEXT NOT NULL)"));
  QVERIFY(q.exec("INSERT INTO archive_member_hash_cache (container_path, member_path) "
                 "VALUES ('/x/set.zip', 'a.bin')"));
  QVERIFY(q.exec("PRAGMA user_version = 23"));

  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "archive_member_hash_cache", "zip_index"));
  // The stale row is gone — the next audit re-hashes and stores real indices.
  QVERIFY(q.exec("SELECT COUNT(*) FROM archive_member_hash_cache"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);

  closeAndRemove(db, conn);
}

void TestDbMigrations::v25AddsProfileCategoryColumn() {
  // v25 (Kartend-7iqhl.5): optional grouping label on each audit profile.
  const QString conn = "test_v25_profile_category";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 25);
  QVERIFY(tableHasColumn(db, "dat_audit_profile", "category"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::preservesExistingDataAcrossUpgrade() {
  // Data inserted before migration must survive ALTER TABLE / CREATE INDEX.
  const QString conn = "test_preserve";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);

  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO collections (name) VALUES ('Movies')"));
  QVERIFY(q.exec("INSERT INTO items (name, path, last_modified) "
                 "VALUES ('m1', '/m/1', '2025-01-01')"));

  DbMigrations::applySchemaMigrations(db, "test");

  QVERIFY(q.exec("SELECT name FROM collections WHERE id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QStringLiteral("Movies"));

  QVERIFY(q.exec("SELECT name, path FROM items WHERE id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QStringLiteral("m1"));
  QCOMPARE(q.value(1).toString(), QStringLiteral("/m/1"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::failedBlockRollsBackAndKeepsVersion() {
  // Kartend-l28zj / Kartend-o58ur: each migration block runs in a transaction
  // and only stamps user_version on success. Omit the `collections` table so
  // v1's first `ALTER TABLE collections ADD COLUMN` fails; the whole v1 block
  // must roll back (no items columns added) and user_version must stay 0 so the
  // migration retries on the next launch instead of permanently skewing the
  // schema past the version marker.
  const QString conn = "test_failed_block";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  QSqlQuery q(db);
  QVERIFY(q.exec("CREATE TABLE items ("
                 "id INTEGER PRIMARY KEY, "
                 "name TEXT NOT NULL, "
                 "path TEXT NOT NULL, "
                 "last_modified TEXT"
                 ")"));
  // Deliberately no `collections` table.

  DbMigrations::applySchemaMigrations(db, "test");

  // Version not advanced; v1's partial work (items columns) rolled back.
  QCOMPARE(getUserVersion(db), 0);
  QVERIFY(!tableHasColumn(db, "items", "collection_uuid"));
  QVERIFY(!tableHasColumn(db, "items", "play_count"));
  QVERIFY(!indexExists(db, "uniq_items_uuid_path"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::newerSchemaVersionIsLeftUntouched() {
  // Kartend-vmvzq: a database written by a newer build (user_version greater
  // than CURRENT_SCHEMA_VERSION) must be left alone — no migrations run and the
  // version is preserved, so an older build can't corrupt a newer schema.
  const QString conn = "test_future_version";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);

  QSqlQuery q(db);
  QVERIFY(q.exec("PRAGMA user_version = 999"));

  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 999);
  // No migration block ran against the future-versioned DB.
  QVERIFY(!tableHasColumn(db, "items", "collection_uuid"));

  closeAndRemove(db, conn);
}

QTEST_MAIN(TestDbMigrations)
#include "test_dbmigrations.moc"
