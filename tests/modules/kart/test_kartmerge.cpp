#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "dbmigrations.h"
#include "itemmetadata.h"
#include "kartmanifest.h"
#include "kartmerge.h"

class TestKartMerge : public QObject {
  Q_OBJECT

private slots:
  void testPreferExistingFillsFromIncomingWhenEmpty();
  void testPreferIncomingFallsBackToExistingWhenIncomingEmpty();
  void testRuntimeSentinelFallback();
  void testLauncherIndexSentinelFallback();
  void testPathAndUuidPreservation();

  // persistImportedMetadata
  void testPersistWritesNewItem();
  void testPersistSkipsEmptyMetadata();
  void testPersistResolverInvokedOnCollision();
  void testPersistOverwriteModeReplacesRow();
  void testPersistMergeModeAppliesPolicy();
  void testPersistSkipModeKeepsExisting();
  void testPersistApplyToAllSuppressesFurtherPrompts();
};

namespace {

QSqlDatabase openMemoryDb(const QString &conn) {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
  db.setDatabaseName(":memory:");
  if (!db.open()) qFatal("openMemoryDb: failed");
  QSqlQuery q(db);
  q.exec("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)");
  q.exec("CREATE TABLE items (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, path TEXT, "
         "last_modified TEXT)");
  DbMigrations::applySchemaMigrations(db, "test");
  return db;
}

void closeMemoryDb(QSqlDatabase &db, const QString &conn) {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(conn);
}

KartManifest::Manifest manifestWithSingleItem(const ItemMetadataStore::ItemMetadata &meta,
                                              const QString &mediaPath = "media/x.bin") {
  KartManifest::Manifest m;
  m.uuid = "u";
  m.name = "n";
  KartManifest::Item it;
  it.mediaPath = mediaPath;
  it.title = meta.title;
  it.metadata = meta;
  m.items.append(it);
  return m;
}

} // namespace

void TestKartMerge::testPreferExistingFillsFromIncomingWhenEmpty() {
  ItemMetadataStore::ItemMetadata existing;
  existing.title = "Original";
  existing.description = "";
  existing.genre = "RPG";
  ItemMetadataStore::ItemMetadata incoming;
  incoming.title = "From Kart";
  incoming.description = "Filled in from Kart";
  incoming.genre = "Action";

  kart::MergePolicy p;
  auto out = kart::mergeItemMetadata(existing, incoming, p);
  QCOMPARE(out.title, QString("Original"));
  QCOMPARE(out.description, QString("Filled in from Kart"));
  QCOMPARE(out.genre, QString("RPG"));
}

void TestKartMerge::testPreferIncomingFallsBackToExistingWhenIncomingEmpty() {
  ItemMetadataStore::ItemMetadata existing;
  existing.title = "Original";
  existing.developer = "Old Dev";
  ItemMetadataStore::ItemMetadata incoming;
  incoming.title = "Kart Title";
  incoming.developer = "";

  kart::MergePolicy p;
  p.preferIncomingTitle = true;
  p.preferIncomingDeveloper = true;
  auto out = kart::mergeItemMetadata(existing, incoming, p);
  QCOMPARE(out.title, QString("Kart Title"));
  QCOMPARE(out.developer, QString("Old Dev"));
}

void TestKartMerge::testRuntimeSentinelFallback() {
  ItemMetadataStore::ItemMetadata existing;
  existing.runtimeSeconds = -1;
  ItemMetadataStore::ItemMetadata incoming;
  incoming.runtimeSeconds = 3600;

  kart::MergePolicy p;
  auto out = kart::mergeItemMetadata(existing, incoming, p);
  QCOMPARE(out.runtimeSeconds, 3600);
}

void TestKartMerge::testLauncherIndexSentinelFallback() {
  ItemMetadataStore::ItemMetadata existing;
  existing.launcherIndex = 2;
  ItemMetadataStore::ItemMetadata incoming;
  incoming.launcherIndex = -1;

  kart::MergePolicy p;
  p.preferIncomingLauncherIndex = true;
  auto out = kart::mergeItemMetadata(existing, incoming, p);
  QCOMPARE(out.launcherIndex, 2);
}

void TestKartMerge::testPathAndUuidPreservation() {
  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = "uuid-existing";
  existing.path = "media/x.bin";
  ItemMetadataStore::ItemMetadata incoming;
  incoming.collectionUuid = "uuid-incoming";
  incoming.path = "media/x.bin";

  kart::MergePolicy p;
  auto out = kart::mergeItemMetadata(existing, incoming, p);
  QCOMPARE(out.collectionUuid, QString("uuid-existing"));
  QCOMPARE(out.path, QString("media/x.bin"));
}

void TestKartMerge::testPersistWritesNewItem() {
  const QString conn = "kart-persist-write";
  auto db = openMemoryDb(conn);

  ItemMetadataStore::ItemMetadata meta;
  meta.title = "Sonic";
  meta.description = "Platformer";
  meta.runtimeSeconds = 7200;
  auto manifest = manifestWithSingleItem(meta);

  QTemporaryDir destRoot;
  auto res = kart::persistImportedMetadata(db, manifest, destRoot.path(), "uuid-1", {});
  QVERIFY2(res.isOk(), qPrintable(res.error().message));
  QCOMPARE(res.value().written, 1);

  const QString resolved = QDir(destRoot.path()).filePath("media/x.bin");
  auto loaded = ItemMetadataStore::load(db, "uuid-1", resolved);
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().title, QString("Sonic"));
  QCOMPARE(loaded.value().runtimeSeconds, 7200);

  closeMemoryDb(db, conn);
}

void TestKartMerge::testPersistSkipsEmptyMetadata() {
  const QString conn = "kart-persist-empty";
  auto db = openMemoryDb(conn);

  ItemMetadataStore::ItemMetadata empty;
  auto manifest = manifestWithSingleItem(empty);

  QTemporaryDir destRoot;
  auto res = kart::persistImportedMetadata(db, manifest, destRoot.path(), "uuid-1", {});
  QVERIFY2(res.isOk(), qPrintable(res.error().message));
  QCOMPARE(res.value().written, 0);
  QCOMPARE(res.value().skipped, 1);

  closeMemoryDb(db, conn);
}

void TestKartMerge::testPersistResolverInvokedOnCollision() {
  const QString conn = "kart-persist-collide";
  auto db = openMemoryDb(conn);
  QTemporaryDir destRoot;
  const QString resolved = QDir(destRoot.path()).filePath("media/x.bin");

  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = "uuid-1";
  existing.path = resolved;
  existing.title = "Existing";
  QVERIFY(ItemMetadataStore::save(db, existing).isOk());

  ItemMetadataStore::ItemMetadata incoming;
  incoming.title = "Incoming";
  auto manifest = manifestWithSingleItem(incoming);

  int callCount = 0;
  QString seenPath, seenExistingTitle, seenIncomingTitle;
  auto resolver = [&](const QString &path, const ItemMetadataStore::ItemMetadata &ex,
                      const ItemMetadataStore::ItemMetadata &inc) {
    ++callCount;
    seenPath = path;
    seenExistingTitle = ex.title;
    seenIncomingTitle = inc.title;
    kart::ConflictResolution r;
    r.choice = kart::MergeChoice::Skip;
    return r;
  };
  auto res = kart::persistImportedMetadata(db, manifest, destRoot.path(), "uuid-1", resolver);
  QVERIFY2(res.isOk(), qPrintable(res.error().message));
  QCOMPARE(callCount, 1);
  QCOMPARE(seenPath, resolved);
  QCOMPARE(seenExistingTitle, QString("Existing"));
  QCOMPARE(seenIncomingTitle, QString("Incoming"));
  QCOMPARE(res.value().skipped, 1);

  closeMemoryDb(db, conn);
}

void TestKartMerge::testPersistOverwriteModeReplacesRow() {
  const QString conn = "kart-persist-overwrite";
  auto db = openMemoryDb(conn);
  QTemporaryDir destRoot;
  const QString resolved = QDir(destRoot.path()).filePath("media/x.bin");

  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = "u";
  existing.path = resolved;
  existing.title = "Old";
  existing.description = "Old desc";
  QVERIFY(ItemMetadataStore::save(db, existing).isOk());

  ItemMetadataStore::ItemMetadata incoming;
  incoming.title = "New";
  auto manifest = manifestWithSingleItem(incoming);

  auto resolver = [](const QString &, const ItemMetadataStore::ItemMetadata &,
                     const ItemMetadataStore::ItemMetadata &) {
    kart::ConflictResolution r;
    r.choice = kart::MergeChoice::Overwrite;
    return r;
  };
  auto res = kart::persistImportedMetadata(db, manifest, destRoot.path(), "u", resolver);
  QVERIFY2(res.isOk(), qPrintable(res.error().message));
  QCOMPARE(res.value().overwritten, 1);

  auto loaded = ItemMetadataStore::load(db, "u", resolved);
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().title, QString("New"));
  QVERIFY(loaded.value().description.isEmpty());

  closeMemoryDb(db, conn);
}

void TestKartMerge::testPersistMergeModeAppliesPolicy() {
  const QString conn = "kart-persist-merge";
  auto db = openMemoryDb(conn);
  QTemporaryDir destRoot;
  const QString resolved = QDir(destRoot.path()).filePath("media/x.bin");

  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = "u";
  existing.path = resolved;
  existing.title = "Existing Title";
  existing.description = "";
  QVERIFY(ItemMetadataStore::save(db, existing).isOk());

  ItemMetadataStore::ItemMetadata incoming;
  incoming.title = "Incoming Title";
  incoming.description = "From Kart";
  auto manifest = manifestWithSingleItem(incoming);

  auto resolver = [](const QString &, const ItemMetadataStore::ItemMetadata &,
                     const ItemMetadataStore::ItemMetadata &) {
    kart::ConflictResolution r;
    r.choice = kart::MergeChoice::Merge;
    r.policy.preferIncomingTitle = false;
    r.policy.preferIncomingDescription = true;
    return r;
  };
  auto res = kart::persistImportedMetadata(db, manifest, destRoot.path(), "u", resolver);
  QVERIFY2(res.isOk(), qPrintable(res.error().message));
  QCOMPARE(res.value().merged, 1);

  auto loaded = ItemMetadataStore::load(db, "u", resolved);
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().title, QString("Existing Title"));
  QCOMPARE(loaded.value().description, QString("From Kart"));

  closeMemoryDb(db, conn);
}

void TestKartMerge::testPersistSkipModeKeepsExisting() {
  const QString conn = "kart-persist-skip";
  auto db = openMemoryDb(conn);
  QTemporaryDir destRoot;
  const QString resolved = QDir(destRoot.path()).filePath("media/x.bin");

  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = "u";
  existing.path = resolved;
  existing.title = "Keep Me";
  QVERIFY(ItemMetadataStore::save(db, existing).isOk());

  ItemMetadataStore::ItemMetadata incoming;
  incoming.title = "Lose Me";
  auto manifest = manifestWithSingleItem(incoming);

  auto resolver = [](const QString &, const ItemMetadataStore::ItemMetadata &,
                     const ItemMetadataStore::ItemMetadata &) {
    kart::ConflictResolution r;
    r.choice = kart::MergeChoice::Skip;
    return r;
  };
  auto res = kart::persistImportedMetadata(db, manifest, destRoot.path(), "u", resolver);
  QVERIFY2(res.isOk(), qPrintable(res.error().message));
  QCOMPARE(res.value().skipped, 1);

  auto loaded = ItemMetadataStore::load(db, "u", resolved);
  QCOMPARE(loaded.value().title, QString("Keep Me"));

  closeMemoryDb(db, conn);
}

void TestKartMerge::testPersistApplyToAllSuppressesFurtherPrompts() {
  const QString conn = "kart-persist-apply-all";
  auto db = openMemoryDb(conn);
  QTemporaryDir destRoot;

  KartManifest::Manifest manifest;
  manifest.uuid = "u";
  manifest.name = "n";
  for (int i = 0; i < 3; ++i) {
    ItemMetadataStore::ItemMetadata existing;
    existing.collectionUuid = "u";
    existing.path = QDir(destRoot.path()).filePath(QString("media/x%1.bin").arg(i));
    existing.title = QString("Old %1").arg(i);
    QVERIFY(ItemMetadataStore::save(db, existing).isOk());

    KartManifest::Item it;
    it.mediaPath = QString("media/x%1.bin").arg(i);
    it.metadata.title = QString("New %1").arg(i);
    manifest.items.append(it);
  }

  int callCount = 0;
  auto resolver = [&](const QString &, const ItemMetadataStore::ItemMetadata &,
                      const ItemMetadataStore::ItemMetadata &) {
    ++callCount;
    kart::ConflictResolution r;
    r.choice = kart::MergeChoice::Overwrite;
    r.applyToAll = true;
    return r;
  };
  auto res = kart::persistImportedMetadata(db, manifest, destRoot.path(), "u", resolver);
  QVERIFY2(res.isOk(), qPrintable(res.error().message));
  QCOMPARE(callCount, 1);
  QCOMPARE(res.value().overwritten, 3);

  for (int i = 0; i < 3; ++i) {
    auto loaded = ItemMetadataStore::load(
        db, "u", QDir(destRoot.path()).filePath(QString("media/x%1.bin").arg(i)));
    QCOMPARE(loaded.value().title, QString("New %1").arg(i));
  }

  closeMemoryDb(db, conn);
}

QTEST_MAIN(TestKartMerge)
#include "test_kartmerge.moc"
