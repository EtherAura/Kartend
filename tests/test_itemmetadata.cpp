// Tests for ItemMetadataStore::load / save / remove against an in-memory
// SQLite database with the v5 schema applied via DbMigrations.
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

#include "dbmigrations.h"
#include "itemmetadata.h"

using ItemMetadataStore::ItemMetadata;

namespace {

QSqlDatabase openMemoryDb(const QString &conn) {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
  db.setDatabaseName(":memory:");
  if (!db.open()) {
    return db;
  }
  // Mirror the minimum base schema the migrations expect to find. The v5
  // migration only adds a brand-new table, but earlier migrations touch
  // collections/items so we provide stub tables.
  QSqlQuery q(db);
  q.exec("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)");
  q.exec("CREATE TABLE items (id INTEGER PRIMARY KEY AUTOINCREMENT, "
         "name TEXT, path TEXT, last_modified TEXT)");
  DbMigrations::applySchemaMigrations(db, "test");
  return db;
}

void closeAndRemove(QSqlDatabase &db, const QString &conn) {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(conn);
}

} // namespace

class TestItemMetadata : public QObject {
  Q_OBJECT
private slots:
  void isEmptyOnDefaultConstructed();
  void isEmptyIgnoresKeys();
  void isNotEmptyForAnyPopulatedField();
  void loadReturnsEmptyForMissingRow();
  void saveAndLoadRoundTrip();
  void saveOverwritesExistingRow();
  void distinguishesByCollectionUuid();
  void removeDeletesRow();
  void runtimeSecondsNullableRoundTrip();
  void saveRejectsEmptyPath();
};

void TestItemMetadata::isEmptyOnDefaultConstructed() {
  ItemMetadata m;
  QVERIFY(m.isEmpty());
}

void TestItemMetadata::isEmptyIgnoresKeys() {
  // collectionUuid / path / source / updatedAt are bookkeeping, not display
  // content, so they should not flip isEmpty(). Otherwise the sidebar would
  // render an empty Details section just because we asked the DB for keys.
  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/some/file.rom";
  m.source = "user";
  m.updatedAt = "2026-01-01T00:00:00Z";
  QVERIFY(m.isEmpty());
}

void TestItemMetadata::isNotEmptyForAnyPopulatedField() {
  for (auto &mutate : std::initializer_list<std::function<void(ItemMetadata &)>>{
           [](ItemMetadata &m) { m.title = "T"; },
           [](ItemMetadata &m) { m.description = "D"; },
           [](ItemMetadata &m) { m.genre = "G"; },
           [](ItemMetadata &m) { m.developer = "Dev"; },
           [](ItemMetadata &m) { m.publisher = "Pub"; },
           [](ItemMetadata &m) { m.releaseDate = "2026"; },
           [](ItemMetadata &m) { m.contentRating = "T"; },
           [](ItemMetadata &m) { m.players = "1-2"; },
           [](ItemMetadata &m) { m.runtimeSeconds = 0; },
           [](ItemMetadata &m) { m.tags = "a"; },
           [](ItemMetadata &m) { m.customFields = "{}"; },
           [](ItemMetadata &m) { m.manualPath = "/x"; },
       }) {
    ItemMetadata m;
    mutate(m);
    QVERIFY(!m.isEmpty());
  }
}

void TestItemMetadata::loadReturnsEmptyForMissingRow() {
  const QString conn = "im_load_missing";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  auto result = ItemMetadataStore::load(db, "uuid-1", "/missing");
  QVERIFY(result.isOk());
  ItemMetadata m = result.value();
  QCOMPARE(m.collectionUuid, QStringLiteral("uuid-1"));
  QCOMPARE(m.path, QStringLiteral("/missing"));
  QVERIFY(m.isEmpty());

  closeAndRemove(db, conn);
}

void TestItemMetadata::saveAndLoadRoundTrip() {
  const QString conn = "im_round_trip";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/games/sonic.bin";
  m.title = "Sonic the Hedgehog";
  m.description = "Run fast, collect rings.";
  m.genre = "Platformer";
  m.developer = "Sonic Team";
  m.publisher = "Sega";
  m.releaseDate = "1991-06-23";
  m.contentRating = "E";
  m.players = "1";
  m.runtimeSeconds = 120;
  m.tags = "[\"classic\",\"mascot\"]";
  m.customFields = "{\"shelf\":\"A1\"}";
  m.manualPath = "/manuals/sonic.pdf";
  m.source = "user";

  auto saved = ItemMetadataStore::save(db, m);
  QVERIFY(saved.isOk());
  QVERIFY(saved.value());

  auto loaded = ItemMetadataStore::load(db, "uuid-1", "/games/sonic.bin");
  QVERIFY(loaded.isOk());
  ItemMetadata r = loaded.value();
  QCOMPARE(r.title, m.title);
  QCOMPARE(r.description, m.description);
  QCOMPARE(r.genre, m.genre);
  QCOMPARE(r.developer, m.developer);
  QCOMPARE(r.publisher, m.publisher);
  QCOMPARE(r.releaseDate, m.releaseDate);
  QCOMPARE(r.contentRating, m.contentRating);
  QCOMPARE(r.players, m.players);
  QCOMPARE(r.runtimeSeconds, 120);
  QCOMPARE(r.tags, m.tags);
  QCOMPARE(r.customFields, m.customFields);
  QCOMPARE(r.manualPath, m.manualPath);
  QCOMPARE(r.source, m.source);
  QVERIFY(!r.updatedAt.isEmpty()); // save() stamps current UTC ISO time

  closeAndRemove(db, conn);
}

void TestItemMetadata::saveOverwritesExistingRow() {
  const QString conn = "im_upsert";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.title = "First";
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  m.title = "Second";
  m.genre = "Action";
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  auto loaded = ItemMetadataStore::load(db, "uuid-1", "/p").value();
  QCOMPARE(loaded.title, QStringLiteral("Second"));
  QCOMPARE(loaded.genre, QStringLiteral("Action"));

  // Only one row should exist for the (uuid, path) pair.
  QSqlQuery q(db);
  QVERIFY(q.exec("SELECT COUNT(*) FROM item_metadata"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  closeAndRemove(db, conn);
}

void TestItemMetadata::distinguishesByCollectionUuid() {
  const QString conn = "im_distinct_uuid";
  auto db = openMemoryDb(conn);

  ItemMetadata a;
  a.collectionUuid = "uuid-A";
  a.path = "/shared";
  a.title = "A title";
  QVERIFY(ItemMetadataStore::save(db, a).isOk());

  ItemMetadata b;
  b.collectionUuid = "uuid-B";
  b.path = "/shared";
  b.title = "B title";
  QVERIFY(ItemMetadataStore::save(db, b).isOk());

  QCOMPARE(ItemMetadataStore::load(db, "uuid-A", "/shared").value().title,
           QStringLiteral("A title"));
  QCOMPARE(ItemMetadataStore::load(db, "uuid-B", "/shared").value().title,
           QStringLiteral("B title"));

  closeAndRemove(db, conn);
}

void TestItemMetadata::removeDeletesRow() {
  const QString conn = "im_remove";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.title = "T";
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  auto removed = ItemMetadataStore::remove(db, "uuid-1", "/p");
  QVERIFY(removed.isOk());

  // Removing a non-existent row must still succeed.
  auto removedAgain = ItemMetadataStore::remove(db, "uuid-1", "/p");
  QVERIFY(removedAgain.isOk());

  QVERIFY(ItemMetadataStore::load(db, "uuid-1", "/p").value().isEmpty());

  closeAndRemove(db, conn);
}

void TestItemMetadata::runtimeSecondsNullableRoundTrip() {
  const QString conn = "im_runtime_null";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.title = "T";
  // runtimeSeconds defaults to -1 ("unset"); save() must persist NULL.
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  auto loaded = ItemMetadataStore::load(db, "uuid-1", "/p").value();
  QCOMPARE(loaded.runtimeSeconds, -1);

  // Now set a real value and confirm it round-trips.
  m.runtimeSeconds = 0;
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QCOMPARE(ItemMetadataStore::load(db, "uuid-1", "/p").value().runtimeSeconds, 0);

  closeAndRemove(db, conn);
}

void TestItemMetadata::saveRejectsEmptyPath() {
  const QString conn = "im_empty_path";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1"; // path intentionally empty
  auto result = ItemMetadataStore::save(db, m);
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);

  closeAndRemove(db, conn);
}

QTEST_MAIN(TestItemMetadata)
#include "test_itemmetadata.moc"
