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
    qWarning("Failed to open in-memory db: %s",
             qPrintable(db.lastError().text()));
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

auto tableHasColumn(QSqlDatabase &db, const QString &table,
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
  void v2AddsPerformanceIndexes();
  void v3AddsMetaTable();
  void v4AddsFileSizeColumnAndIndex();
  void v5AddsItemMetadataTable();
  void v6AddsItemArtworkTable();
  void v7AddsUsageStatsColumnAndIndexes();
  void v8AddsLauncherIndexColumn();
  void v9AddsLaunchHistoryTable();
  void v10AddsPlaylistTables();
  void preservesExistingDataAcrossUpgrade();
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
  // Current schema version is 10 (per dbmigrations.cpp).
  QCOMPARE(getUserVersion(db), 10);

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

  QCOMPARE(getUserVersion(db), 10);

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

  QCOMPARE(getUserVersion(db), 10);
  QVERIFY(tableHasColumn(db, "items", "file_size"));
  QVERIFY(indexExists(db, "idx_items_uuid_file_size"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v5AddsItemMetadataTable() {
  const QString conn = "test_v5_item_metadata";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 10);
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
  const bool dupAccepted =
      q.exec("INSERT INTO item_metadata (collection_uuid, path, updated_at) "
             "VALUES ('u1', '/m/1', '2026-01-02')");
  QVERIFY(!dupAccepted);

  closeAndRemove(db, conn);
}

void TestDbMigrations::v6AddsItemArtworkTable() {
  const QString conn = "test_v6_item_artwork";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 10);
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

  QCOMPARE(getUserVersion(db), 10);
  // Cumulative play-time column added in v7 (Kartend-7vi).
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

  QCOMPARE(getUserVersion(db), 10);
  // Per-item launcher override column added in v8 (Kartend-dnx4).
  QVERIFY(tableHasColumn(db, "item_metadata", "launcher_index"));

  closeAndRemove(db, conn);
}

void TestDbMigrations::v9AddsLaunchHistoryTable() {
  const QString conn = "test_v9_history";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 10);
  // Append-only history table added in v9 (Kartend-fse).
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
  // Kartend-vlm7: playlists + playlist_items added in v10. Both tables and
  // their lookup indexes must exist after migration; the FK cascade on
  // playlist_items.playlist_id is exercised end-to-end in
  // test_playlistmanager.cpp's deletePlaylist_cascadesItems().
  const QString conn = "test_v10_playlists";
  auto db = openMemoryDb(conn);
  createBaseSchema(db);
  DbMigrations::applySchemaMigrations(db, "test");

  QCOMPARE(getUserVersion(db), 10);
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

QTEST_MAIN(TestDbMigrations)
#include "test_dbmigrations.moc"
