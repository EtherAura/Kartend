// Kartend-zur26: metadata sidecars are written on scrape but were never read
// back, so item_metadata was the only store of scraped text. Losing media.db —
// corruption, a fresh install pointed at the same library, moving the library
// to another machine — meant re-scraping everything, while the answers sat in
// JSON next to the artwork, which the scan re-discovers by itself.
//
// These cases drive ScanMetadata::hydrateFromSidecars against a real migrated
// SQLite (the project's no-DB-mocking rule) and real sidecar files, since the
// pass is precisely a filesystem-to-database bridge and mocking either half
// would test nothing.
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "../../support/migrateddb.h"
#include "itemmetadata.h"
#include "scanmetadata.h"

namespace {

const QString kUuid = QStringLiteral("collection-uuid-1");

/// items.collection_id is a foreign key, so the parent row has to exist before
/// any item does.
void insertCollection(QSqlDatabase &db) {
  QSqlQuery q(db);
  QVERIFY(q.prepare(QStringLiteral(
      "INSERT INTO collections (id, name, last_scanned, ext_signature, uuid, dir_signature) "
      "VALUES (?, ?, ?, ?, ?, ?)")));
  q.addBindValue(1);
  q.addBindValue(QStringLiteral("Films"));
  q.addBindValue(QDateTime::fromSecsSinceEpoch(0).toString(Qt::ISODate));
  q.addBindValue(QString());
  q.addBindValue(kUuid);
  q.addBindValue(QString());
  QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

void insertItem(QSqlDatabase &db, const QString &relPath) {
  QSqlQuery q(db);
  QVERIFY(q.prepare(QStringLiteral(
      "INSERT INTO items (collection_id, collection_uuid, path, name, last_modified, "
      "play_count, rating) VALUES (?, ?, ?, ?, ?, 0, 0)")));
  q.addBindValue(1);
  q.addBindValue(kUuid);
  q.addBindValue(relPath);
  q.addBindValue(QFileInfo(relPath).completeBaseName());
  q.addBindValue(0);
  QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

/// Writes a sidecar in the exact shape writeMetadataSidecar produces: flat
/// string fields, runtimeSeconds as a number, tags as an inline array and
/// customFields as an object.
void writeSidecar(const QString &artworkDir, const QString &baseName, const QByteArray &json) {
  const QString dir = QDir(artworkDir).filePath(QStringLiteral("metadata"));
  QVERIFY(QDir().mkpath(dir));
  QFile f(QDir(dir).filePath(baseName + QStringLiteral(".json")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  QCOMPARE(f.write(json), qint64(json.size()));
}

QByteArray fullSidecar() {
  return QByteArrayLiteral(R"({
    "title": "Recovered Title",
    "description": "A description that survived the database.",
    "genre": "Documentary",
    "developer": "Some Studio",
    "publisher": "Some Publisher",
    "releaseDate": "1998-04-01",
    "contentRating": "PG",
    "players": "1-2",
    "source": "screenscraper",
    "runtimeSeconds": 5400,
    "tags": ["restored", "sidecar"],
    "customFields": {"shelf": "A3"}
  })");
}

} // namespace

class TestScanMetadata : public QObject {
  Q_OBJECT
private slots:
  void hydratesAnItemThatHasNoMetadataRow();
  void neverOverwritesAnExistingRow();
  void skipsItemsWithNoSidecarAndCollectionsWithNoSidecarDirectory();
  void ignoresAMalformedOrTitlelessSidecar();
};

// The headline case: the library is intact on disk, the database is not.
void TestScanMetadata::hydratesAnItemThatHasNoMetadataRow() {
  KartendTest::MigratedDb db;
  QTemporaryDir artwork;
  QVERIFY(artwork.isValid());
  QSqlDatabase conn = db.database();
  insertCollection(conn);
  insertItem(conn, QStringLiteral("Some Film (1998).mkv"));
  writeSidecar(artwork.path(), QStringLiteral("Some Film (1998)"), fullSidecar());

  int txnDepth = 0;
  QCOMPARE(ScanMetadata::hydrateFromSidecars(conn, txnDepth, artwork.path(), kUuid), 1);

  auto loaded = ItemMetadataStore::load(conn, kUuid, QStringLiteral("Some Film (1998).mkv"));
  QVERIFY(!loaded.isError());
  const auto row = loaded.value();
  QCOMPARE(row.title, QStringLiteral("Recovered Title"));
  QCOMPARE(row.description, QStringLiteral("A description that survived the database."));
  QCOMPARE(row.genre, QStringLiteral("Documentary"));
  QCOMPARE(row.developer, QStringLiteral("Some Studio"));
  QCOMPARE(row.publisher, QStringLiteral("Some Publisher"));
  QCOMPARE(row.releaseDate, QStringLiteral("1998-04-01"));
  QCOMPARE(row.contentRating, QStringLiteral("PG"));
  QCOMPARE(row.players, QStringLiteral("1-2"));
  QCOMPARE(row.source, QStringLiteral("screenscraper"));
  QCOMPARE(row.runtimeSeconds, 5400);
  // tags / customFields round-trip through the same parsers the rest of the
  // app reads them with, rather than being compared as raw JSON text.
  QCOMPARE(ItemMetadataStore::parseTags(row.tags),
           QStringList({QStringLiteral("restored"), QStringLiteral("sidecar")}));
  const auto custom = ItemMetadataStore::parseCustomFields(row.customFields);
  QCOMPARE(custom.size(), 1);
  QCOMPARE(custom.first().first, QStringLiteral("shelf"));
  QCOMPARE(custom.first().second, QStringLiteral("A3"));

  // Idempotent: the row now exists, so a second pass finds no candidates.
  QCOMPARE(ScanMetadata::hydrateFromSidecars(conn, txnDepth, artwork.path(), kUuid), 0);
}

// The precedence rule, and the one that matters most: a sidecar is a stale
// snapshot of some past scrape, while the row may since have been edited by
// hand. An existing row is never touched — not even to fill a blank field.
void TestScanMetadata::neverOverwritesAnExistingRow() {
  KartendTest::MigratedDb db;
  QTemporaryDir artwork;
  QVERIFY(artwork.isValid());
  QSqlDatabase conn = db.database();
  insertCollection(conn);
  insertItem(conn, QStringLiteral("Some Film (1998).mkv"));
  writeSidecar(artwork.path(), QStringLiteral("Some Film (1998)"), fullSidecar());

  // A row the user has edited: a title they corrected, notes they wrote, and
  // a description they deliberately left blank.
  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = kUuid;
  existing.path = QStringLiteral("Some Film (1998).mkv");
  existing.title = QStringLiteral("The Title I Typed");
  existing.notes = QStringLiteral("my own notes");
  existing.rating = 9;
  auto saved = ItemMetadataStore::save(conn, existing);
  QVERIFY(!saved.isError());

  int txnDepth = 0;
  QCOMPARE(ScanMetadata::hydrateFromSidecars(conn, txnDepth, artwork.path(), kUuid), 0);

  auto loaded = ItemMetadataStore::load(conn, kUuid, QStringLiteral("Some Film (1998).mkv"));
  QVERIFY(!loaded.isError());
  QCOMPARE(loaded.value().title, QStringLiteral("The Title I Typed"));
  QCOMPARE(loaded.value().notes, QStringLiteral("my own notes"));
  QCOMPARE(loaded.value().rating, 9);
  QVERIFY2(loaded.value().description.isEmpty(),
           "a blank field on an existing row is the user's choice, not a gap to fill");
}

void TestScanMetadata::skipsItemsWithNoSidecarAndCollectionsWithNoSidecarDirectory() {
  KartendTest::MigratedDb db;
  QTemporaryDir artwork;
  QVERIFY(artwork.isValid());
  QSqlDatabase conn = db.database();
  insertCollection(conn);
  insertItem(conn, QStringLiteral("Covered.mkv"));
  insertItem(conn, QStringLiteral("Uncovered.mkv"));

  // No metadata directory at all — the common case for a collection that was
  // never scraped. Must cost nothing and write nothing.
  int txnDepth = 0;
  QCOMPARE(ScanMetadata::hydrateFromSidecars(conn, txnDepth, artwork.path(), kUuid), 0);
  QCOMPARE(ScanMetadata::hydrateFromSidecars(conn, txnDepth, QString(), kUuid), 0);

  // One sidecar, two items: only the matching item is hydrated.
  writeSidecar(artwork.path(), QStringLiteral("Covered"), fullSidecar());
  QCOMPARE(ScanMetadata::hydrateFromSidecars(conn, txnDepth, artwork.path(), kUuid), 1);
  QVERIFY(
      !ItemMetadataStore::load(conn, kUuid, QStringLiteral("Covered.mkv")).value().title.isEmpty());
  QVERIFY(ItemMetadataStore::load(conn, kUuid, QStringLiteral("Uncovered.mkv"))
              .value()
              .title.isEmpty());
}

// The sidecar directory is an ordinary user-writable folder, so a .json in it
// is not necessarily one Kartend wrote.
void TestScanMetadata::ignoresAMalformedOrTitlelessSidecar() {
  KartendTest::MigratedDb db;
  QTemporaryDir artwork;
  QVERIFY(artwork.isValid());
  QSqlDatabase conn = db.database();
  insertCollection(conn);
  insertItem(conn, QStringLiteral("Broken.mkv"));
  insertItem(conn, QStringLiteral("Titleless.mkv"));

  writeSidecar(artwork.path(), QStringLiteral("Broken"), QByteArrayLiteral("{not json at all"));
  // The writer skips an item with no title, so a titleless sidecar is not a
  // record we produced — and a row with no title is worse than no row.
  writeSidecar(artwork.path(), QStringLiteral("Titleless"),
               QByteArrayLiteral(R"({"genre": "Documentary"})"));

  int txnDepth = 0;
  QCOMPARE(ScanMetadata::hydrateFromSidecars(conn, txnDepth, artwork.path(), kUuid), 0);
  QVERIFY(
      ItemMetadataStore::load(conn, kUuid, QStringLiteral("Broken.mkv")).value().title.isEmpty());
  QVERIFY(ItemMetadataStore::load(conn, kUuid, QStringLiteral("Titleless.mkv"))
              .value()
              .genre.isEmpty());
}

QTEST_GUILESS_MAIN(TestScanMetadata)
#include "test_scanmetadata.moc"
