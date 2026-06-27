// Real-SQLite test for DatAuditProfileController (Kartend-n1hpy.3). The
// insert-vs-update persist DECISION and the pre-write DatRef metadata refresh
// were lifted off DatAuditAuditPage (a QWidget) into this plain controller, so
// they round-trip through an actual on-disk media.db without constructing the
// widget. Same sandbox harness as test_datauditprofilestore.cpp: the store
// derives its path from AppDataLocation (redirected to a per-test sandbox), and
// a setup connection builds the production schema via DatabaseSchema::createTables,
// exactly as the app does. No DB mocking.

#include <QFile>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "../../support/testsandbox.h"
#include "databaseschema.h"
#include "datauditprofile.h"
#include "datauditprofilecontroller.h"
#include "datauditprofilestore.h"

using DatAuditProfile::DatRef;
using DatAuditProfile::Profile;

namespace {
QString appDataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
} // namespace

class TestDatAuditProfileController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();

  void persistInsertsAssignsIdAndRoundTrips();
  void persistUpdatesExistingProfile();
  void refreshDatRefMetadataStampsExistingFileOnly();
  void removeAndLoadByCollectionUuidForwardToStore();

private:
  static Profile sampleProfile();
};

void TestDatAuditProfileController::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-datauditprofilecontroller"));
}

void TestDatAuditProfileController::init() {
  // Build the production schema at the sandbox media.db through a throwaway
  // setup connection (the same DatabaseSchema::createTables path the app runs),
  // then drop it so the store opens its own per-call connections to the file.
  const QString conn = QStringLiteral("setup_datauditprofilecontroller");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));
    DatabaseSchema::applyConnectionPragmas(db);
    DatabaseSchema::createTables(db);
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
}

void TestDatAuditProfileController::cleanup() {
  QFile::remove(appDataDir() + QStringLiteral("/media.db"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-wal"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-shm"));
}

Profile TestDatAuditProfileController::sampleProfile() {
  Profile p;
  p.name = QStringLiteral("Reference Audit");
  p.collectionUuid = QStringLiteral("uuid-123");
  p.scanRoots = {QStringLiteral("/media/setA")};
  return p;
}

void TestDatAuditProfileController::persistInsertsAssignsIdAndRoundTrips() {
  DatAuditProfileStore store;
  DatAuditProfileController controller(store);

  Profile p = sampleProfile();
  QCOMPARE(p.id, qint64(-1)); // "(unsaved)" — persist should INSERT.

  auto res = controller.persist(p);
  QVERIFY2(res.isOk(), qPrintable(res.isError() ? res.error().message : QString()));
  QVERIFY(res.value() > 0);
  QCOMPARE(p.id, res.value()); // id assigned back into the passed-in profile.

  // list() / load() forwards see the inserted row.
  auto all = controller.list();
  QVERIFY(all.isOk());
  QCOMPARE(all.value().size(), qsizetype(1));
  QCOMPARE(all.value().first().id, p.id);

  auto loaded = controller.load(p.id);
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().has_value());
  QCOMPARE(loaded.value()->name, QStringLiteral("Reference Audit"));
}

void TestDatAuditProfileController::persistUpdatesExistingProfile() {
  DatAuditProfileStore store;
  DatAuditProfileController controller(store);

  Profile p = sampleProfile();
  QVERIFY(controller.persist(p).isOk());
  const qint64 id = p.id;

  // Second persist of the same (id >= 0) profile must UPDATE, not insert.
  p.name = QStringLiteral("Renamed");
  auto upd = controller.persist(p);
  QVERIFY2(upd.isOk(), qPrintable(upd.isError() ? upd.error().message : QString()));
  QCOMPARE(upd.value(), id); // same id back, no new row.
  QCOMPARE(p.id, id);

  QCOMPARE(controller.list().value().size(), qsizetype(1));
  auto loaded = controller.load(id);
  QVERIFY(loaded.value().has_value());
  QCOMPARE(loaded.value()->name, QStringLiteral("Renamed"));
}

void TestDatAuditProfileController::refreshDatRefMetadataStampsExistingFileOnly() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString datPath = tmp.filePath(QStringLiteral("reference.dat"));
  {
    QFile f(datPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("clrmamepro ( )\n");
  }

  DatRef existing;
  existing.path = datPath;
  DatRef missing;
  missing.path = tmp.filePath(QStringLiteral("does-not-exist.dat"));
  QList<DatRef> dats{existing, missing};

  DatAuditProfileController::refreshDatRefMetadata(dats);

  QVERIFY(dats[0].mtimeMs > 0);         // existing file gets a stat-derived mtime.
  QCOMPARE(dats[1].mtimeMs, qint64(0)); // missing path left untouched.
  // dialect / recordCount stay 0 with no DatCache ingest — refresh never parses.
  QCOMPARE(dats[0].dialect, 0);
  QCOMPARE(dats[0].recordCount, 0);
}

void TestDatAuditProfileController::removeAndLoadByCollectionUuidForwardToStore() {
  DatAuditProfileStore store;
  DatAuditProfileController controller(store);

  Profile p = sampleProfile(); // collectionUuid "uuid-123"
  QVERIFY(controller.persist(p).isOk());

  auto linked = controller.loadByCollectionUuid(QStringLiteral("uuid-123"));
  QVERIFY(linked.isOk());
  QVERIFY(linked.value().has_value());
  QCOMPARE(linked.value()->id, p.id);

  QVERIFY(controller.remove(p.id).isOk());
  QVERIFY(controller.list().value().isEmpty());
}

QTEST_MAIN(TestDatAuditProfileController)
#include "test_datauditprofilecontroller.moc"
