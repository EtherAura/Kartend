// Test for Kartend-h7xnr.6: the per-item state-flags read for the grid's
// pinned/hidden/continue-later badges runs on the query worker
// (QueryManager::fetchItemStateFlagsForCollection) instead of the
// main-thread connection, and reports its result via itemStateFlagsLoaded.
//
//   fetchReportsOnlyFlaggedRowsForRequestedUuid
//     Rows written on another connection (the app writes flag toggles on the
//     main-thread connection) must be visible to the worker query; only rows
//     with at least one flag set come back, scoped to the requested uuid,
//     and the uuid is echoed so the receiver can drop stale replies.
//   fetchEmitsEmptyListsForEmptyUuid
//     An empty uuid still produces a (empty) reply so the receiver never
//     hangs waiting for the signal.

#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QStringList>
#include <QTest>
#include <QThread>

#include "../../support/inspectordb.h"
#include "../../support/scopeexit.h"
#include "../../support/testsandbox.h"
#include "itemmetadata.h"
#include "querymanager.h"
#include "sessionmanager.h"
#include "../../support/workersignalspy.h"

class TestQueryManagerStateFlags : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void fetchReportsOnlyFlaggedRowsForRequestedUuid();
  void fetchEmitsEmptyListsForEmptyUuid();
};

void TestQueryManagerStateFlags::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-stateflags"));
}

void TestQueryManagerStateFlags::fetchReportsOnlyFlaggedRowsForRequestedUuid() {
  SessionManager sessionManager;
  auto *qm = new QueryManager(&sessionManager);
  QThread worker;
  qm->moveToThread(&worker);
  worker.start();

  const auto workerCleanup = KartendTest::makeScopeExit([&]() {
    if (qm) {
      qm->requestCancelScan();
      qm->deleteLater();
    }
    if (worker.isRunning()) {
      worker.quit();
      if (!worker.wait(10'000)) {
        worker.terminate();
        worker.wait(10'000);
      }
    }
  });

  QMetaObject::invokeMethod(qm, &QueryManager::initDatabase, Qt::BlockingQueuedConnection);

  // Seed item_metadata through a second connection onto the same file —
  // mirrors production, where flag toggles commit on the main-thread
  // connection while the worker reads on its own.
  const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dbFilePath = QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
  KartendTest::InspectorDb inspector(dbFilePath, QStringLiteral("test_qm_stateflags_seed"));
  QVERIFY2(inspector.isOpen(), "Failed to open seeding database connection");
  QSqlDatabase seedDb = inspector.db();

  const QString uuid = QStringLiteral("uuid-flags");
  const QString otherUuid = QStringLiteral("uuid-other");

  const auto seed = [&](const QString &collectionUuid, const QString &path, bool pinned,
                        bool hidden, bool continueLater) {
    ItemMetadataStore::ItemMetadata m;
    m.collectionUuid = collectionUuid;
    m.path = path;
    m.isPinned = pinned;
    m.isHidden = hidden;
    m.continueLater = continueLater;
    QVERIFY2(ItemMetadataStore::save(seedDb, m).isOk(),
             qPrintable("Failed to seed item_metadata row for " + path));
  };
  seed(uuid, QStringLiteral("/media/pinned.bin"), true, false, false);
  seed(uuid, QStringLiteral("/media/hidden-and-later.bin"), false, true, true);
  seed(uuid, QStringLiteral("/media/unflagged.bin"), false, false, false);
  seed(otherUuid, QStringLiteral("/media/foreign-pin.bin"), true, false, false);

  // WorkerSignalSpy (not QSignalSpy): qm lives on the worker thread, and
  // Qt 6.4's QSignalSpy connects DirectConnection with no locking.
  WorkerSignalSpy flagsSpy(qm, &QueryManager::itemStateFlagsLoaded);
  QVERIFY(flagsSpy.isValid());

  QMetaObject::invokeMethod(
      qm, [qm, uuid]() { qm->fetchItemStateFlagsForCollection(uuid); }, Qt::QueuedConnection);
  QVERIFY2(flagsSpy.wait(10'000), "Timed out waiting for itemStateFlagsLoaded");

  const QList<QVariant> args = flagsSpy.takeFirst();
  QCOMPARE(args.at(0).toString(), uuid);
  QCOMPARE(args.at(1).toStringList(), QStringList{QStringLiteral("/media/pinned.bin")});
  QCOMPARE(args.at(2).toStringList(), QStringList{QStringLiteral("/media/hidden-and-later.bin")});
  QCOMPARE(args.at(3).toStringList(), QStringList{QStringLiteral("/media/hidden-and-later.bin")});
}

void TestQueryManagerStateFlags::fetchEmitsEmptyListsForEmptyUuid() {
  SessionManager sessionManager;
  auto *qm = new QueryManager(&sessionManager);
  QThread worker;
  qm->moveToThread(&worker);
  worker.start();

  const auto workerCleanup = KartendTest::makeScopeExit([&]() {
    if (qm) {
      qm->requestCancelScan();
      qm->deleteLater();
    }
    if (worker.isRunning()) {
      worker.quit();
      if (!worker.wait(10'000)) {
        worker.terminate();
        worker.wait(10'000);
      }
    }
  });

  QMetaObject::invokeMethod(qm, &QueryManager::initDatabase, Qt::BlockingQueuedConnection);

  WorkerSignalSpy flagsSpy(qm, &QueryManager::itemStateFlagsLoaded);
  QVERIFY(flagsSpy.isValid());

  QMetaObject::invokeMethod(
      qm, [qm]() { qm->fetchItemStateFlagsForCollection(QString()); }, Qt::QueuedConnection);
  QVERIFY2(flagsSpy.wait(10'000), "Timed out waiting for itemStateFlagsLoaded (empty uuid)");

  const QList<QVariant> args = flagsSpy.takeFirst();
  QVERIFY(args.at(0).toString().isEmpty());
  QVERIFY(args.at(1).toStringList().isEmpty());
  QVERIFY(args.at(2).toStringList().isEmpty());
  QVERIFY(args.at(3).toStringList().isEmpty());
}

QTEST_MAIN(TestQueryManagerStateFlags)

#include "test_querymanager_stateflags.moc"
