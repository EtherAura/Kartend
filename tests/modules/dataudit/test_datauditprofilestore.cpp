// Real-SQLite test for DatAuditProfileStore (Kartend-ahf3d stage 2). The profile
// / result-snapshot / DAT-library-provenance SQLite access was extracted off
// DatAuditDialog into a connection-owning store, so it round-trips through an
// actual on-disk media.db without instantiating the QDialog. The store derives
// its path from AppDataLocation (redirected to a per-test sandbox here); a setup
// connection builds the production schema via DatabaseSchema::createTables,
// exactly as the app does. No DB mocking.

#include <QFile>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QTest>

#include "../../support/testsandbox.h"
#include "databaseschema.h"
#include "datauditprofile.h"
#include "datauditprofilestore.h"
#include "datlibrarystate.h"

using DatAuditProfile::DatRef;
using DatAuditProfile::FixMode;
using DatAuditProfile::Profile;
using DatAuditProfile::ResultRow;

namespace {
QString appDataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
} // namespace

class TestDatAuditProfileStore : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();

  void insertLoadListRoundTrip();
  void updateRewritesProfile();
  void removeDeletesProfile();
  void loadMissingReturnsEmptyOptional();
  void loadByCollectionUuidFindsLinked();
  void snapshotResultsRoundTrip();
  void touchLastScanStampsProfile();
  void provenanceRoundTrip();

private:
  static Profile sampleProfile();
};

void TestDatAuditProfileStore::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-datauditprofilestore"));
}

void TestDatAuditProfileStore::init() {
  // Build the production schema at the sandbox media.db through a throwaway setup
  // connection (the same DatabaseSchema::createTables path the app runs), then
  // drop it so the store opens its own per-call connections to the file.
  const QString conn = QStringLiteral("setup_datauditprofilestore");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));
    DatabaseSchema::applyConnectionPragmas(db);
    DatabaseSchema::createTables(db);
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
}

void TestDatAuditProfileStore::cleanup() {
  QFile::remove(appDataDir() + QStringLiteral("/media.db"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-wal"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-shm"));
}

Profile TestDatAuditProfileStore::sampleProfile() {
  Profile p;
  p.name = QStringLiteral("Reference Audit");
  p.collectionUuid = QStringLiteral("uuid-123");
  p.scanRoots = {QStringLiteral("/media/setA"), QStringLiteral("/media/setB")};
  p.regionPrefs = {QStringLiteral("USA"), QStringLiteral("World")};
  p.onePerGame = true;
  p.ignoreRules = {QStringLiteral("*.txt")};
  p.fixMode = FixMode::ManagedOutput;
  p.managedOutputRoot = QStringLiteral("/sorted");
  p.quarantineRoot = QStringLiteral("/quarantine");
  p.category = QStringLiteral("Reference");
  p.detectedLayout = QStringLiteral("archive_per_item");
  p.layoutConfirmed = true;
  p.mergeMode = QStringLiteral("merged");
  DatRef d;
  d.path = QStringLiteral("/dats/reference.dat");
  d.mtimeMs = 111;
  d.dialect = 1;
  d.recordCount = 5000;
  p.dats = {d};
  return p;
}

void TestDatAuditProfileStore::insertLoadListRoundTrip() {
  DatAuditProfileStore store;
  const Profile p = sampleProfile();

  auto ins = store.insertProfile(p);
  QVERIFY2(ins.isOk(), qPrintable(ins.isError() ? ins.error().message : QString()));
  const qint64 id = ins.value();
  QVERIFY(id > 0);

  auto loaded = store.loadProfile(id);
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().has_value());
  QCOMPARE(loaded.value()->name, p.name);
  QCOMPARE(loaded.value()->scanRoots, p.scanRoots);
  QCOMPARE(loaded.value()->dats.size(), qsizetype(1));
  QCOMPARE(loaded.value()->dats.first().path, QStringLiteral("/dats/reference.dat"));

  auto all = store.listProfiles();
  QVERIFY(all.isOk());
  QCOMPARE(all.value().size(), qsizetype(1));
  QCOMPARE(all.value().first().id, id);
}

void TestDatAuditProfileStore::updateRewritesProfile() {
  DatAuditProfileStore store;
  Profile p = sampleProfile();
  auto ins = store.insertProfile(p);
  QVERIFY(ins.isOk());
  p.id = ins.value();
  p.name = QStringLiteral("Renamed");
  p.onePerGame = false;

  auto upd = store.updateProfile(p);
  QVERIFY2(upd.isOk(), qPrintable(upd.isError() ? upd.error().message : QString()));

  auto loaded = store.loadProfile(p.id);
  QVERIFY(loaded.value().has_value());
  QCOMPARE(loaded.value()->name, QStringLiteral("Renamed"));
  QCOMPARE(loaded.value()->onePerGame, false);
}

void TestDatAuditProfileStore::removeDeletesProfile() {
  DatAuditProfileStore store;
  auto ins = store.insertProfile(sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  auto rem = store.removeProfile(id);
  QVERIFY2(rem.isOk(), qPrintable(rem.isError() ? rem.error().message : QString()));

  auto loaded = store.loadProfile(id);
  QVERIFY(loaded.isOk());
  QVERIFY(!loaded.value().has_value());
  QVERIFY(store.listProfiles().value().isEmpty());
}

void TestDatAuditProfileStore::loadMissingReturnsEmptyOptional() {
  DatAuditProfileStore store;
  auto loaded = store.loadProfile(424242);
  QVERIFY(loaded.isOk()); // a missing row is not an error
  QVERIFY(!loaded.value().has_value());
}

void TestDatAuditProfileStore::loadByCollectionUuidFindsLinked() {
  DatAuditProfileStore store;
  auto ins = store.insertProfile(sampleProfile()); // collectionUuid "uuid-123"
  QVERIFY(ins.isOk());

  auto linked = store.loadProfileByCollectionUuid(QStringLiteral("uuid-123"));
  QVERIFY(linked.isOk());
  QVERIFY(linked.value().has_value());
  QCOMPARE(linked.value()->id, ins.value());

  auto none = store.loadProfileByCollectionUuid(QStringLiteral("no-such-uuid"));
  QVERIFY(none.isOk());
  QVERIFY(!none.value().has_value());
}

void TestDatAuditProfileStore::snapshotResultsRoundTrip() {
  DatAuditProfileStore store;
  auto ins = store.insertProfile(sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  ResultRow r1;
  r1.entryKey = QStringLiteral("entry-a");
  r1.status = 2;
  r1.filePath = QStringLiteral("/media/setA/a.zip");
  r1.sourceName = QStringLiteral("Reference Set");
  r1.gameName = QStringLiteral("Entry A");
  r1.zipIndex = 0;
  ResultRow r2;
  r2.entryKey = QStringLiteral("entry-b");
  r2.status = 1;
  r2.gameName = QStringLiteral("Entry B");
  r2.mia = true;

  auto saved = store.replaceResults(id, {r1, r2});
  QVERIFY2(saved.isOk(), qPrintable(saved.isError() ? saved.error().message : QString()));

  auto rows = store.loadProfileResultRows(id);
  QVERIFY(rows.isOk());
  QCOMPARE(rows.value().size(), qsizetype(2));
}

void TestDatAuditProfileStore::touchLastScanStampsProfile() {
  DatAuditProfileStore store;
  auto ins = store.insertProfile(sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  auto stamped = store.touchLastScan(id, 1700000000123LL);
  QVERIFY2(stamped.isOk(), qPrintable(stamped.isError() ? stamped.error().message : QString()));

  auto loaded = store.loadProfile(id);
  QVERIFY(loaded.value().has_value());
  QCOMPARE(loaded.value()->lastScanAtMs, 1700000000123LL);
}

void TestDatAuditProfileStore::provenanceRoundTrip() {
  DatAuditProfileStore store;
  DatLibraryState::Provenance pr;
  pr.canonicalPath = QStringLiteral("/lib/reference.dat");
  pr.source = QStringLiteral("nointro");
  pr.systemId = 4;
  pr.version = QStringLiteral("2026-06-01");
  pr.updatedAtMs = 1700000000000LL;

  store.recordProvenance(pr); // void; logs on error

  const auto all = store.loadAllProvenance();
  QCOMPARE(all.size(), qsizetype(1));
  QCOMPARE(all.first().canonicalPath, pr.canonicalPath);
  QCOMPARE(all.first().source, QStringLiteral("nointro"));
  QCOMPARE(all.first().version, QStringLiteral("2026-06-01"));
}

QTEST_MAIN(TestDatAuditProfileStore)
#include "test_datauditprofilestore.moc"
