// Real-engine test for DatAuditRunController (Kartend-ahf3d stage 3). The
// off-thread audit-run orchestration was extracted off DatAuditDialog into a
// QObject that owns the QFutureWatcher + cancel flag and the worker that opens
// the hash-cache DB. This drives a real audit (real DAT, real scan dir, real
// hash-cache media.db under a per-test sandbox) and asserts the finished()
// signal delivers the engine's output back on the caller's thread — the
// threading + signal contract that was previously untestable because it was
// welded to the dialog. The audit engine's row taxonomy is covered separately by
// test_datauditrunner. No DB mocking.

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "../../support/testsandbox.h"
#include "databaseschema.h"
#include "datauditprofile.h" // DatAuditProfile::insert / load / loadProfileResultRows
#include "datauditruncontroller.h"
#include "datauditrunner.h" // DatAudit::AuditOutput

namespace {

constexpr const char *kLogiqxDat = R"xml(<?xml version="1.0"?>
<datafile>
  <header><name>Test</name></header>
  <game name="Alpha">
    <rom name="Alpha.bin" size="5" crc="deadbeef"
         md5="11111111111111111111111111111111"
         sha1="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"/>
  </game>
</datafile>)xml";

QString appDataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

} // namespace

class TestDatAuditRunController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanup();

  void startRunsOffThreadAndDeliversFinished();
  void persistsSnapshotOnWorkerBeforeFinished();

private:
  static QString writeDat(const QTemporaryDir &dir);
};

void TestDatAuditRunController::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-datauditruncontroller"));
}

void TestDatAuditRunController::cleanup() {
  QFile::remove(appDataDir() + QStringLiteral("/media.db"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-wal"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-shm"));
}

QString TestDatAuditRunController::writeDat(const QTemporaryDir &dir) {
  const QString path = dir.filePath(QStringLiteral("test.dat"));
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return {};
  }
  f.write(kLogiqxDat);
  return path;
}

void TestDatAuditRunController::startRunsOffThreadAndDeliversFinished() {
  // Build the hash-cache media.db schema so the worker's optional cache
  // connection opens against a real DB (the run still works without it, but this
  // exercises the production path).
  const QString conn = QStringLiteral("setup_datauditruncontroller");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));
    DatabaseSchema::applyConnectionPragmas(db);
    DatabaseSchema::createTables(db);
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString datPath = writeDat(dir);
  QVERIFY(!datPath.isEmpty());
  const QString scanDir = dir.filePath(QStringLiteral("roms"));
  QVERIFY(QDir().mkpath(scanDir));

  DatAuditRunController ctrl;
  DatAudit::AuditOutput captured;
  bool done = false;
  connect(&ctrl, &DatAuditRunController::finished, &ctrl,
          [&captured, &done](const DatAudit::AuditOutput &o) {
            captured = o;
            done = true;
          });

  DatAuditRunController::Request req;
  req.datPaths = {datPath};
  req.scanRoots = {scanDir};
  ctrl.start(req);

  // finished() is delivered via the QFutureWatcher on this (event-loop) thread;
  // QTRY spins the loop until the worker returns.
  QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
  QVERIFY(!ctrl.isRunning());
  QVERIFY(!captured.cancelled);
  // The DAT is valid XML, so the catalogue built without a load failure — i.e.
  // the worker really processed the injected inputs and handed the result back.
  QVERIFY(captured.failedDats.isEmpty());
}

void TestDatAuditRunController::persistsSnapshotOnWorkerBeforeFinished() {
  // Kartend-h7xnr.5: the result snapshot for a saved profile is written by the
  // WORKER (on its own app-DB connection), and snapshotPersisted is delivered
  // strictly before finished() with the rows already queryable — the ordering
  // contract the audit page's view-only finish handler now relies on.
  const QString conn = QStringLiteral("setup_datauditrun_persist");
  qint64 profileId = -1;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));
    DatabaseSchema::applyConnectionPragmas(db);
    DatabaseSchema::createTables(db);
    DatAuditProfile::Profile p;
    p.name = QStringLiteral("persist-test");
    const auto inserted = DatAuditProfile::insert(db, p);
    QVERIFY(inserted.isOk());
    profileId = inserted.value();
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
  QVERIFY(profileId >= 0);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString datPath = writeDat(dir);
  QVERIFY(!datPath.isEmpty());
  const QString scanDir = dir.filePath(QStringLiteral("roms"));
  QVERIFY(QDir().mkpath(scanDir));

  DatAuditRunController ctrl;
  bool done = false;
  bool persistedSeen = false;
  bool persistedBeforeFinished = false;
  qint64 signalledProfileId = -1;
  qint64 stampedMs = 0;
  QList<DatAuditProfile::ResultRow> rowsAtSignal;
  connect(&ctrl, &DatAuditRunController::snapshotPersisted, &ctrl, [&](qint64 id, qint64 whenMs) {
    persistedSeen = true;
    persistedBeforeFinished = !done;
    signalledProfileId = id;
    stampedMs = whenMs;
    // The signal's contract: the snapshot is committed, so the rows
    // must be queryable the moment it is delivered.
    const QString readConn = QStringLiteral("read_datauditrun_persist");
    {
      QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), readConn);
      if (DatabaseSchema::openConnection(db, appDataDir())) {
        if (auto r = DatAuditProfile::loadProfileResultRows(db, id); r.isOk()) {
          rowsAtSignal = r.value();
        }
      }
      db.close();
    }
    QSqlDatabase::removeDatabase(readConn);
  });
  connect(&ctrl, &DatAuditRunController::finished, &ctrl,
          [&done](const DatAudit::AuditOutput &) { done = true; });

  DatAuditRunController::Request req;
  req.datPaths = {datPath};
  req.scanRoots = {scanDir};
  req.persistProfileId = profileId;
  ctrl.start(req);

  QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
  QVERIFY(persistedSeen);
  QVERIFY(persistedBeforeFinished);
  QCOMPARE(signalledProfileId, profileId);
  QVERIFY(stampedMs > 0);
  // Empty scan dir + one-ROM DAT: exactly one entry-only (Missing) row.
  QCOMPARE(rowsAtSignal.size(), 1);
  QVERIFY(rowsAtSignal.first().entryKey.startsWith(QStringLiteral("entry:")));
  QVERIFY(rowsAtSignal.first().filePath.isEmpty());

  // The last-scan stamp landed in the same worker-side persist.
  const QString verifyConn = QStringLiteral("verify_datauditrun_persist");
  qint64 lastScanAtMs = 0;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));
    const auto loaded = DatAuditProfile::load(db, profileId);
    QVERIFY(loaded.isOk() && loaded.value().has_value());
    lastScanAtMs = loaded.value()->lastScanAtMs;
    db.close();
  }
  QSqlDatabase::removeDatabase(verifyConn);
  QCOMPARE(lastScanAtMs, stampedMs);
}

QTEST_MAIN(TestDatAuditRunController)
#include "test_datauditruncontroller.moc"
