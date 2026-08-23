// Tests for EntityMetadataStore::load / loadForCollection / save / remove
// against an in-memory SQLite database with the full schema applied via
// DbMigrations (the entity_metadata table lands in v27). Same harness as
// test_itemmetadata.cpp — real database, no mocking (docs/dev/testing.md).
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QThread>

#include "dbmigrations.h"
#include "entitymetadata.h"

using EntityMetadataStore::EntityMetadata;

namespace {

QSqlDatabase openMemoryDb(const QString &conn) {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
  db.setDatabaseName(":memory:");
  if (!db.open()) {
    return db;
  }
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

EntityMetadata sampleCollectionRow() {
  EntityMetadata m;
  m.entityType = EntityMetadataStore::kTypeCollection;
  m.entityIdentity = QStringLiteral("uuid-osg");
  m.collectionUuid = QStringLiteral("uuid-osg");
  m.title = QStringLiteral("Open Source Games");
  m.description = QStringLiteral("A shelf of open-source classics.");
  m.customFields = QStringLiteral("{\"manufacturer\":\"Various\"}");
  m.source = QStringLiteral("wikipedia");
  return m;
}

} // namespace

class TestEntityMetadata : public QObject {
  Q_OBJECT

private slots:
  void saveRoundTripsThroughLoad();
  void saveUpsertsOnTheKeyTriple();
  void missingRowLoadsEmptyButKeyed();
  void loadForCollectionPrefersCollectionRowsOverNewerPlatformRows();
  void removeDeletesExactlyTheKeyedRow();
};

void TestEntityMetadata::saveRoundTripsThroughLoad() {
  const QString conn = QStringLiteral("entitymeta-roundtrip");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  const EntityMetadata saved = sampleCollectionRow();
  QVERIFY(EntityMetadataStore::save(db, saved).isOk());

  auto loaded =
      EntityMetadataStore::load(db, saved.entityType, saved.entityIdentity, saved.collectionUuid);
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().title, saved.title);
  QCOMPARE(loaded.value().description, saved.description);
  QCOMPARE(loaded.value().customFields, saved.customFields);
  QCOMPARE(loaded.value().source, saved.source);
  QVERIFY(!loaded.value().updatedAt.isEmpty());
  QVERIFY(!loaded.value().isEmpty());

  closeAndRemove(db, conn);
}

void TestEntityMetadata::saveUpsertsOnTheKeyTriple() {
  const QString conn = QStringLiteral("entitymeta-upsert");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  EntityMetadata row = sampleCollectionRow();
  QVERIFY(EntityMetadataStore::save(db, row).isOk());
  row.description = QStringLiteral("Rescraped, better description.");
  row.source = QStringLiteral("screenscraper");
  QVERIFY(EntityMetadataStore::save(db, row).isOk());

  // One row, carrying the rescrape's values.
  QSqlQuery count(db);
  QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM entity_metadata")));
  QVERIFY(count.next());
  QCOMPARE(count.value(0).toInt(), 1);

  auto loaded =
      EntityMetadataStore::load(db, row.entityType, row.entityIdentity, row.collectionUuid);
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().description, QStringLiteral("Rescraped, better description."));
  QCOMPARE(loaded.value().source, QStringLiteral("screenscraper"));

  closeAndRemove(db, conn);
}

void TestEntityMetadata::missingRowLoadsEmptyButKeyed() {
  const QString conn = QStringLiteral("entitymeta-missing");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  auto loaded = EntityMetadataStore::load(db, EntityMetadataStore::kTypePlatform,
                                          QStringLiteral("75"), QStringLiteral("uuid-x"));
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().isEmpty());
  QCOMPARE(loaded.value().entityType, QString::fromLatin1(EntityMetadataStore::kTypePlatform));
  QCOMPARE(loaded.value().collectionUuid, QStringLiteral("uuid-x"));

  auto forCollection = EntityMetadataStore::loadForCollection(db, QStringLiteral("uuid-x"));
  QVERIFY(forCollection.isOk());
  QVERIFY(forCollection.value().isEmpty());
  QCOMPARE(forCollection.value().collectionUuid, QStringLiteral("uuid-x"));

  closeAndRemove(db, conn);
}

void TestEntityMetadata::loadForCollectionPrefersCollectionRowsOverNewerPlatformRows() {
  const QString conn = QStringLiteral("entitymeta-priority");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  // Collection row first, then a NEWER platform row for the same
  // collection (updated_at has one-second ISO resolution — force distinct
  // timestamps rather than sleeping through a boundary).
  EntityMetadata franchise = sampleCollectionRow();
  QVERIFY(EntityMetadataStore::save(db, franchise).isOk());
  QSqlQuery backdate(db);
  QVERIFY(
      backdate.exec(QStringLiteral("UPDATE entity_metadata SET updated_at = '2020-01-01T00:00:00Z'"
                                   " WHERE entity_type = 'collection'")));

  EntityMetadata platform;
  platform.entityType = EntityMetadataStore::kTypePlatform;
  platform.entityIdentity = QStringLiteral("75");
  platform.collectionUuid = franchise.collectionUuid;
  platform.title = QStringLiteral("Sega Mega Drive");
  platform.description = QStringLiteral("Console boilerplate.");
  QVERIFY(EntityMetadataStore::save(db, platform).isOk());

  auto best = EntityMetadataStore::loadForCollection(db, franchise.collectionUuid);
  QVERIFY(best.isOk());
  // The (older) collection-typed row wins over the newer platform row.
  QCOMPARE(best.value().entityType, QString::fromLatin1(EntityMetadataStore::kTypeCollection));
  QCOMPARE(best.value().title, franchise.title);

  closeAndRemove(db, conn);
}

void TestEntityMetadata::removeDeletesExactlyTheKeyedRow() {
  const QString conn = QStringLiteral("entitymeta-remove");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  EntityMetadata a = sampleCollectionRow();
  EntityMetadata b = sampleCollectionRow();
  b.entityType = EntityMetadataStore::kTypePlatform;
  b.entityIdentity = QStringLiteral("75");
  QVERIFY(EntityMetadataStore::save(db, a).isOk());
  QVERIFY(EntityMetadataStore::save(db, b).isOk());

  QVERIFY(EntityMetadataStore::remove(db, a.entityType, a.entityIdentity, a.collectionUuid).isOk());

  auto goneA = EntityMetadataStore::load(db, a.entityType, a.entityIdentity, a.collectionUuid);
  QVERIFY(goneA.isOk());
  QVERIFY(goneA.value().isEmpty());
  auto keptB = EntityMetadataStore::load(db, b.entityType, b.entityIdentity, b.collectionUuid);
  QVERIFY(keptB.isOk());
  QCOMPARE(keptB.value().title, b.title);

  // Removing an absent row still succeeds.
  QVERIFY(EntityMetadataStore::remove(db, a.entityType, a.entityIdentity, a.collectionUuid).isOk());

  closeAndRemove(db, conn);
}

QTEST_MAIN(TestEntityMetadata)
#include "test_entitymetadata.moc"
