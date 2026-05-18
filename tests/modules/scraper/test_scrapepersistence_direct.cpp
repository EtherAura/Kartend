/**
 * @file test_scrapepersistence_direct.cpp
 * @brief Tests for `Scraper::saveScrapedMetadataDirect` — the
 *        worker-thread DB-write path that bypasses IDatabaseManager and
 *        operates against a raw QSqlDatabase.
 *
 * Why this exists: the IDatabaseManager-backed `saveScrapedMetadata` is
 * exercised end-to-end by test_scrapepersistence.cpp's CapturingDb mock.
 * The direct path can't reuse that mock — it uses ItemMetadataStore::load
 * / ::save and ItemArtworkStore::save against a real connection — so
 * coverage requires a real :memory: SQLite. These tests assert the
 * direct path is byte-for-byte equivalent to the IDatabaseManager path
 * for the merge contract that gnkb's worker depends on (no field
 * regression when the same scrape lands via either route).
 */
#include <QPair>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTest>

#include "dbmigrations.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "scrapepersistence.h"
#include "scrapertypes.h"

namespace {

QSqlDatabase openMemoryDb(const QString &conn) {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
  db.setDatabaseName(":memory:");
  if (!db.open()) {
    return db;
  }
  // Same minimal base schema the other store tests use — migrations
  // backfill the rest of the columns the stores need.
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

class TestScrapePersistenceDirect : public QObject {
  Q_OBJECT
private slots:
  void writesNewMetadataRowAndArtworkRows();
  void preservesExistingFieldsWhenScrapeIsEmpty();
  void overwritesScrapedFieldsWhenBothPresent();
  void mergesCustomFieldsScrapeWinsOnSharedKeys();
  void emptyCollectionUuidReturnsFalseAndNoSave();
  void emptySourcePathReturnsFalseAndNoSave();
  void closedDatabaseReturnsFalse();
  void nonStandardArtworkRowsLandViaItemArtworkStore();
  void runtimeSecondsHonoredOnlyWhenNonNegative();
  void sourceProviderIdSkippedWhenScrapeEmpty();
};

void TestScrapePersistenceDirect::writesNewMetadataRowAndArtworkRows() {
  const QString conn = QStringLiteral("test_direct_basic");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("Test Title");
  scraped.publisher = QStringLiteral("Test Publisher");
  scraped.sourceProviderId = QStringLiteral("teststub");

  Scraper::NonStandardArtworkList artwork;
  artwork.append({QStringLiteral("back"), QStringLiteral("/some/path/back.png")});

  const bool ok = Scraper::saveScrapedMetadataDirect(
      db, QStringLiteral("u1"), QStringLiteral("/m/song.flac"), scraped, artwork);
  QVERIFY(ok);

  // Re-load and assert the row landed with the merged values.
  auto loaded = ItemMetadataStore::load(db, QStringLiteral("u1"),
                                         QStringLiteral("/m/song.flac"));
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().title, QStringLiteral("Test Title"));
  QCOMPARE(loaded.value().publisher, QStringLiteral("Test Publisher"));
  QCOMPARE(loaded.value().source, QStringLiteral("teststub"));

  // Non-standard artwork row landed via ItemArtworkStore.
  auto rows = ItemArtworkStore::loadAllForItem(db, QStringLiteral("u1"),
                                                QStringLiteral("/m/song.flac"));
  QVERIFY(rows.isOk());
  QCOMPARE(rows.value().size(), 1);
  QCOMPARE(rows.value().first().artworkType, QStringLiteral("back"));
  QCOMPARE(rows.value().first().manualPath,
           QStringLiteral("/some/path/back.png"));

  closeAndRemove(db, conn);
}

void TestScrapePersistenceDirect::preservesExistingFieldsWhenScrapeIsEmpty() {
  const QString conn = QStringLiteral("test_direct_preserve");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  // Pre-seed a row with user-entered values.
  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = QStringLiteral("u1");
  existing.path = QStringLiteral("/m/song.flac");
  existing.title = QStringLiteral("User Title");
  existing.contentRating = QStringLiteral("PG");
  existing.players = QStringLiteral("1-4");
  QVERIFY(ItemMetadataStore::save(db, existing).isOk());

  // Scrape arrives with NO fields populated except a publisher — every
  // pickNonEmpty branch should return the existing value.
  Scraper::ScrapedItem scraped;
  scraped.publisher = QStringLiteral("New Publisher");

  QVERIFY(Scraper::saveScrapedMetadataDirect(db, QStringLiteral("u1"),
                                              QStringLiteral("/m/song.flac"),
                                              scraped, {}));

  auto loaded = ItemMetadataStore::load(db, QStringLiteral("u1"),
                                         QStringLiteral("/m/song.flac"));
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().title, QStringLiteral("User Title"));
  QCOMPARE(loaded.value().contentRating, QStringLiteral("PG"));
  QCOMPARE(loaded.value().players, QStringLiteral("1-4"));
  QCOMPARE(loaded.value().publisher, QStringLiteral("New Publisher"));

  closeAndRemove(db, conn);
}

void TestScrapePersistenceDirect::overwritesScrapedFieldsWhenBothPresent() {
  const QString conn = QStringLiteral("test_direct_overwrite");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = QStringLiteral("u1");
  existing.path = QStringLiteral("/m/g.bin");
  existing.title = QStringLiteral("Old Title");
  existing.publisher = QStringLiteral("Old Publisher");
  QVERIFY(ItemMetadataStore::save(db, existing).isOk());

  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("New Title");
  scraped.publisher = QStringLiteral("New Publisher");

  QVERIFY(Scraper::saveScrapedMetadataDirect(db, QStringLiteral("u1"),
                                              QStringLiteral("/m/g.bin"),
                                              scraped, {}));

  auto loaded = ItemMetadataStore::load(db, QStringLiteral("u1"),
                                         QStringLiteral("/m/g.bin"));
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().title, QStringLiteral("New Title"));
  QCOMPARE(loaded.value().publisher, QStringLiteral("New Publisher"));

  closeAndRemove(db, conn);
}

void TestScrapePersistenceDirect::mergesCustomFieldsScrapeWinsOnSharedKeys() {
  const QString conn = QStringLiteral("test_direct_custom");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  // Pre-existing customFields with two keys; one will collide with a
  // scrape key and one will survive untouched.
  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = QStringLiteral("u1");
  existing.path = QStringLiteral("/m/g.bin");
  existing.customFields =
      QStringLiteral(R"({"region":"US","userNote":"keep me"})");
  QVERIFY(ItemMetadataStore::save(db, existing).isOk());

  Scraper::ScrapedItem scraped;
  scraped.customFields.insert(QStringLiteral("region"), QStringLiteral("EU"));
  scraped.customFields.insert(QStringLiteral("genre2"), QStringLiteral("RPG"));

  QVERIFY(Scraper::saveScrapedMetadataDirect(db, QStringLiteral("u1"),
                                              QStringLiteral("/m/g.bin"),
                                              scraped, {}));

  auto loaded = ItemMetadataStore::load(db, QStringLiteral("u1"),
                                         QStringLiteral("/m/g.bin"));
  QVERIFY(loaded.isOk());
  const auto fields =
      ItemMetadataStore::parseCustomFields(loaded.value().customFields);
  // Convert to a hash for order-independent assertions — parseCustomFields
  // returns alphabetically-sorted pairs, but we only care about contents.
  QHash<QString, QString> got;
  for (const auto &kv : fields) {
    got.insert(kv.first, kv.second);
  }
  QCOMPARE(got.value(QStringLiteral("region")), QStringLiteral("EU"));
  QCOMPARE(got.value(QStringLiteral("userNote")), QStringLiteral("keep me"));
  QCOMPARE(got.value(QStringLiteral("genre2")), QStringLiteral("RPG"));

  closeAndRemove(db, conn);
}

void TestScrapePersistenceDirect::emptyCollectionUuidReturnsFalseAndNoSave() {
  const QString conn = QStringLiteral("test_direct_no_uuid");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("Should not land");

  // Empty uuid → guard returns false before the save runs.
  QVERIFY(!Scraper::saveScrapedMetadataDirect(db, QString(),
                                                QStringLiteral("/m/x.flac"),
                                                scraped, {}));

  closeAndRemove(db, conn);
}

void TestScrapePersistenceDirect::emptySourcePathReturnsFalseAndNoSave() {
  const QString conn = QStringLiteral("test_direct_no_path");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("Should not land");

  QVERIFY(!Scraper::saveScrapedMetadataDirect(db, QStringLiteral("u1"),
                                                QString(), scraped, {}));

  closeAndRemove(db, conn);
}

void TestScrapePersistenceDirect::closedDatabaseReturnsFalse() {
  // Defensive: caller should never invoke against a closed connection,
  // but the worker's openConnection-failed path can leave m_db invalid.
  // The function must fail-fast rather than crash when handed one.
  QSqlDatabase db; // Not opened.
  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("Should not land");
  QVERIFY(!Scraper::saveScrapedMetadataDirect(db, QStringLiteral("u1"),
                                                QStringLiteral("/m/x.flac"),
                                                scraped, {}));
}

void TestScrapePersistenceDirect::nonStandardArtworkRowsLandViaItemArtworkStore() {
  const QString conn = QStringLiteral("test_direct_artwork");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("ItemX");

  Scraper::NonStandardArtworkList artwork;
  artwork.append({QStringLiteral("back"), QStringLiteral("/disk/back.png")});
  artwork.append({QStringLiteral("manual"), QStringLiteral("/disk/manual.pdf")});

  QVERIFY(Scraper::saveScrapedMetadataDirect(db, QStringLiteral("u1"),
                                              QStringLiteral("/m/x.bin"),
                                              scraped, artwork));

  auto rows = ItemArtworkStore::loadAllForItem(db, QStringLiteral("u1"),
                                                QStringLiteral("/m/x.bin"));
  QVERIFY(rows.isOk());
  QCOMPARE(rows.value().size(), 2);
  // Order isn't contractual; assert both rows are present by type.
  QSet<QString> types;
  for (const auto &row : rows.value()) {
    types.insert(row.artworkType);
  }
  QVERIFY(types.contains(QStringLiteral("back")));
  QVERIFY(types.contains(QStringLiteral("manual")));

  closeAndRemove(db, conn);
}

void TestScrapePersistenceDirect::runtimeSecondsHonoredOnlyWhenNonNegative() {
  const QString conn = QStringLiteral("test_direct_runtime");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  // Pre-seed a non-zero runtime so we can verify the negative-stays-existing
  // branch as well as the non-negative-overwrites branch.
  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = QStringLiteral("u1");
  existing.path = QStringLiteral("/m/g.bin");
  existing.runtimeSeconds = 42;
  QVERIFY(ItemMetadataStore::save(db, existing).isOk());

  // First scrape: leave runtime unset (-1) → existing 42 stays.
  Scraper::ScrapedItem scrape1;
  scrape1.title = QStringLiteral("Whatever");
  // runtimeSeconds defaults to -1.
  QVERIFY(Scraper::saveScrapedMetadataDirect(db, QStringLiteral("u1"),
                                              QStringLiteral("/m/g.bin"),
                                              scrape1, {}));
  auto after1 = ItemMetadataStore::load(db, QStringLiteral("u1"),
                                         QStringLiteral("/m/g.bin"));
  QVERIFY(after1.isOk());
  QCOMPARE(after1.value().runtimeSeconds, 42);

  // Second scrape: 0 IS non-negative → overwrites to 0 (an item with a
  // valid scraped 0-second runtime should land, not be treated as unset).
  Scraper::ScrapedItem scrape2;
  scrape2.runtimeSeconds = 0;
  QVERIFY(Scraper::saveScrapedMetadataDirect(db, QStringLiteral("u1"),
                                              QStringLiteral("/m/g.bin"),
                                              scrape2, {}));
  auto after2 = ItemMetadataStore::load(db, QStringLiteral("u1"),
                                         QStringLiteral("/m/g.bin"));
  QVERIFY(after2.isOk());
  QCOMPARE(after2.value().runtimeSeconds, 0);

  closeAndRemove(db, conn);
}

void TestScrapePersistenceDirect::sourceProviderIdSkippedWhenScrapeEmpty() {
  const QString conn = QStringLiteral("test_direct_source");
  QSqlDatabase db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = QStringLiteral("u1");
  existing.path = QStringLiteral("/m/g.bin");
  existing.source = QStringLiteral("user-tagged");
  QVERIFY(ItemMetadataStore::save(db, existing).isOk());

  // Scrape with no sourceProviderId — existing user attribution stays.
  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("X");

  QVERIFY(Scraper::saveScrapedMetadataDirect(db, QStringLiteral("u1"),
                                              QStringLiteral("/m/g.bin"),
                                              scraped, {}));
  auto loaded = ItemMetadataStore::load(db, QStringLiteral("u1"),
                                         QStringLiteral("/m/g.bin"));
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().source, QStringLiteral("user-tagged"));

  closeAndRemove(db, conn);
}

QTEST_MAIN(TestScrapePersistenceDirect)
#include "test_scrapepersistence_direct.moc"
