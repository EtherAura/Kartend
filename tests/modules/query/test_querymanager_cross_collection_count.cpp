// Regression test for Kartend-oyi2: when multiple collections contain a file
// with the same relative path (e.g. "foo.bin" under both subA and subB), the
// parent count with queryIncludeAllCollections (the same uuid-IN expansion
// driven by showAllSubcollectionItems) must not collapse them. (uuid, path)
// is the item identity, so the count is the total number of distinct files.

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
#include "querymanager.h"
#include "sessionmanager.h"
#include "../../support/workersignalspy.h"

class TestQueryManagerCrossCollectionCount : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void duplicateNamedFilesCountSeparately();
};

void TestQueryManagerCrossCollectionCount::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-cross-collection-count"));
}

void TestQueryManagerCrossCollectionCount::duplicateNamedFilesCountSeparately() {
  QTemporaryDir rootDir;
  QVERIFY2(rootDir.isValid(), "Failed to create temporary root directory");

  // Two distinct collections, each with a file named "shared.bin". The
  // relative path strings collide, but the absolute paths are different.
  const QString subAPath = rootDir.filePath(QStringLiteral("subA"));
  const QString subBPath = rootDir.filePath(QStringLiteral("subB"));
  QVERIFY(QDir(rootDir.path()).mkpath(QStringLiteral("subA")));
  QVERIFY(QDir(rootDir.path()).mkpath(QStringLiteral("subB")));

  for (const QString &dirPath : {subAPath, subBPath}) {
    QFile f(QDir(dirPath).filePath(QStringLiteral("shared.bin")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
  }

  CollectionConfig collA;
  collA.name = QStringLiteral("CrossA");
  collA.mediaDirectory = subAPath;
  collA.folderBrowsing.includeContentSubfolders = false;
  collA.extensions = {QStringLiteral("bin")};

  CollectionConfig collB;
  collB.name = QStringLiteral("CrossB");
  collB.mediaDirectory = subBPath;
  collB.folderBrowsing.includeContentSubfolders = false;
  collB.extensions = {QStringLiteral("bin")};

  QList<CollectionConfig> allCollections{collA, collB};

  // Driving the query with queryIncludeAllCollections=true expands the
  // uuid-IN filter to span both collections, mirroring the SQL the parent
  // view uses with showAllSubcollectionItems.
  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.config = collA;
  ctx.queryIncludeAllCollections = true;

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

  const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dbFilePath = QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
  KartendTest::InspectorDb inspector(dbFilePath,
                                     QStringLiteral("test_querymanager_cross_count_inspect"));
  QVERIFY2(inspector.isOpen(), "Failed to open inspector database");
  QSqlDatabase inspectDb = inspector.db();

  // Clean slate so prior test runs don't pollute the count.
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM items")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM collections")));
  }

  // Scan both collections so each gets its own row in items.
  // WorkerSignalSpy (not QSignalSpy): qm lives on the worker thread, and
  // Qt 6.4's QSignalSpy connects DirectConnection with no locking, so it
  // would mutate its list on the worker thread while this thread reads it.
  WorkerSignalSpy scanCompletedSpy(qm, &QueryManager::collectionScanCompleted);
  QVERIFY(scanCompletedSpy.isValid());

  for (int i = 0; i < allCollections.size(); ++i) {
    CollectionContext scanCtx;
    scanCtx.currentIndex = i;
    scanCtx.config = allCollections[i];
    QMetaObject::invokeMethod(
        qm,
        [qm, scanCtx, allCollections]() { qm->ensureScannedForContext(scanCtx, allCollections); },
        Qt::QueuedConnection);
  }
  QTRY_COMPARE_WITH_TIMEOUT(scanCompletedSpy.count(), 2, 10'000);

  // Sanity: items table now has exactly 2 rows (one per collection).
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);
  }

  // Now ask QueryManager for the count via the production path. With the
  // pre-fix SQL (COUNT(DISTINCT path)) this collapses to 1; with the fix it
  // counts each (uuid, path) row, yielding 2.
  // WorkerSignalSpy (not QSignalSpy): see the threading note above.
  WorkerSignalSpy itemCountSpy(qm, &QueryManager::itemCountLoaded);
  QVERIFY(itemCountSpy.isValid());

  QMetaObject::invokeMethod(
      qm, [qm, ctx, allCollections]() { qm->fetchItemCount(ctx, allCollections, QString()); },
      Qt::QueuedConnection);

  QVERIFY2(itemCountSpy.wait(10'000), "Timed out waiting for itemCountLoaded");
  QCOMPARE(itemCountSpy.takeFirst().at(0).toInt(), 2);
}

QTEST_MAIN(TestQueryManagerCrossCollectionCount)

#include "test_querymanager_cross_collection_count.moc"
