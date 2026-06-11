#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include "../../support/inspectordb.h"
#include "../../support/scopeexit.h"
#include "../../support/testsandbox.h"
#include "collection/collectioncontext.h"
#include "collection/typehelpers.h"
#include "querymanager.h"
#include "sessionmanager.h"
#include "workersignalspy.h"

class TestQueryManagerCancelScan : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void testCancelledScanDoesNotMutateDatabase();
};

void TestQueryManagerCancelScan::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-querymanager"));
}

void TestQueryManagerCancelScan::testCancelledScanDoesNotMutateDatabase() {
  QTemporaryDir mediaDir;
  QVERIFY2(mediaDir.isValid(), "Failed to create temporary media directory");

  // Create enough files that a scan would meaningfully do work.
  // The scan will be cancelled immediately on scanStarting.
  const QDir dir(mediaDir.path());
  constexpr int fileCount = 1500;
  for (int i = 0; i < fileCount; ++i) {
    const QString fileName = QStringLiteral("file_%1.bin").arg(i, 6, 10, QChar('0'));
    QFile f(dir.filePath(fileName));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
  }

  // Build a minimal collection config/context.
  CollectionConfig collection;
  collection.name = QStringLiteral("TestCollection");
  collection.mediaDirectory = mediaDir.path();
  collection.folderBrowsing.includeContentSubfolders = false;
  collection.extensions = {QStringLiteral("bin")};

  QList<CollectionConfig> allCollections;
  allCollections.append(collection);

  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.config = collection;

  // Spin up QueryManager on a dedicated worker thread (matches production threading).
  SessionManager sessionManager;
  auto *qm = new QueryManager(&sessionManager);
  QThread worker;
  qm->moveToThread(&worker);
  worker.start();

  // Ensure the worker thread is always stopped before leaving this test function,
  // even when QVERIFY/QCOMPARE early-returns on failure.
  const auto workerCleanup = KartendTest::makeScopeExit([&]() {
    if (qm) {
      // Best-effort cancellation to avoid long-running scans on failure paths.
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

  // Initialize the database on the worker thread.
  QMetaObject::invokeMethod(qm, &QueryManager::initDatabase, Qt::BlockingQueuedConnection);

  // Open a separate inspector connection on the test thread (Qt SQL connections are per-thread).
  const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dbFilePath = QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
  KartendTest::InspectorDb inspector(dbFilePath, QStringLiteral("test_querymanager_inspect"));
  QVERIFY2(inspector.isOpen(), "Failed to open inspector database");
  QSqlDatabase inspectDb = inspector.db();

  // Seed DB with a row that would be deleted on a successful scan apply.
  // If cancellation is truly non-mutating, this row must remain.
  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM items")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM collections")));

    q.prepare(QStringLiteral(
        "INSERT INTO collections (id, name, last_scanned, ext_signature, uuid, dir_signature) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(1);
    q.addBindValue(collection.name);
    q.addBindValue(QDateTime::fromSecsSinceEpoch(0).toString(Qt::ISODate));
    q.addBindValue(QString());
    q.addBindValue(uuid);
    q.addBindValue(QString());
    QVERIFY(q.exec());

    q.prepare(QStringLiteral("INSERT INTO items (collection_id, collection_uuid, path, name, "
                             "last_modified, play_count, rating) "
                             "VALUES (?, ?, ?, ?, ?, 0, 0)"));
    q.addBindValue(1);
    q.addBindValue(uuid);
    q.addBindValue(QStringLiteral("ghost.bin"));
    q.addBindValue(QStringLiteral("ghost"));
    q.addBindValue(QDateTime::fromSecsSinceEpoch(0).toString(Qt::ISODate));
    QVERIFY(q.exec());
  }

  // WorkerSignalSpy (not QSignalSpy): qm lives on the worker thread, and
  // Qt 6.4's QSignalSpy connects DirectConnection with no locking, so it
  // would mutate its list on the worker thread while this thread reads it.
  WorkerSignalSpy itemCountSpy(qm, &QueryManager::itemCountLoaded);
  QVERIFY(itemCountSpy.isValid());

  // Cancel immediately when scan starts.
  QObject::connect(
      qm, &QueryManager::scanStarting, qm, [qm](const QString &, int) { qm->requestCancelScan(); },
      Qt::DirectConnection);

  // Trigger the scan via the normal public slot, but invoke it via a functor to
  // avoid queued-connection metatype requirements for CollectionContext.
  QMetaObject::invokeMethod(
      qm, [qm, ctx, allCollections]() { qm->fetchItemCount(ctx, allCollections, QString()); },
      Qt::QueuedConnection);

  // Wait for fetchItemCount() to complete.
  QVERIFY2(itemCountSpy.wait(10000), "Timed out waiting for itemCountLoaded");

  // Database must remain unchanged (count should reflect the seeded item).
  QCOMPARE(itemCountSpy.takeFirst().at(0).toInt(), 1);

  {
    QSqlQuery q(inspectDb);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM items WHERE collection_uuid = ?"));
    q.addBindValue(uuid);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    q.prepare(QStringLiteral("SELECT path FROM items WHERE collection_uuid = ?"));
    q.addBindValue(uuid);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("ghost.bin"));

    q.prepare(QStringLiteral("SELECT last_scanned, dir_signature FROM collections WHERE uuid = ?"));
    q.addBindValue(uuid);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QDateTime::fromSecsSinceEpoch(0).toString(Qt::ISODate));
    QCOMPARE(q.value(1).toString(), QString());
  }

  // Cleanup handled by scope-exit guard.
}

QTEST_MAIN(TestQueryManagerCancelScan)

#include "test_querymanager_cancel_scan.moc"
