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

QTEST_MAIN(TestDatAuditRunController)
#include "test_datauditruncontroller.moc"
