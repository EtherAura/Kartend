// Regression test for Kartend-6r4g2: the sorted-items range cache must not
// survive a rescan that changed an existing collection's item set. The cache's
// validity hash keys on (collection uuids, filter, sortMode) — NOT item
// contents — so after a rescan adds files to the same collection the hash still
// matches and a stale cache would keep serving the pre-rescan page set.
// QueryManager::invalidateQueryCaches() (wired to scan completion in
// DatabaseManager) must drop it so the next fetchItemsRange rebuilds from fresh
// data. Random sort is used because it forces a synchronous sorted-cache build
// regardless of collection size, so the cache is guaranteed to be in play.

#include <QCoreApplication>
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

class TestQueryManagerCacheInvalidation : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void rescanInvalidatesSortedRangeCache();
};

void TestQueryManagerCacheInvalidation::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-cache-invalidation"));
}

void TestQueryManagerCacheInvalidation::rescanInvalidatesSortedRangeCache() {
  QTemporaryDir mediaDir;
  QVERIFY2(mediaDir.isValid(), "Failed to create temporary media directory");

  const auto writeFiles = [&](int start, int count) {
    for (int i = start; i < start + count; ++i) {
      QFile f(
          QDir(mediaDir.path()).filePath(QStringLiteral("game_%1.bin").arg(i, 4, 10, QChar('0'))));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("x");
      f.close();
    }
  };
  writeFiles(0, 5);

  CollectionConfig coll;
  coll.name = QStringLiteral("CacheColl");
  coll.mediaDirectory = mediaDir.path();
  coll.folderBrowsing.includeContentSubfolders = false;
  coll.extensions = {QStringLiteral("bin")};

  QList<CollectionConfig> allCollections{coll};

  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.config = coll;
  // Random sort forces a synchronous sorted-items cache build on fetchItemsRange
  // regardless of collection size, so the cache is guaranteed populated.
  ctx.sortMode = SortMode::Random;

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
  KartendTest::InspectorDb inspector(
      dbFilePath, QStringLiteral("test_querymanager_cache_invalidation_inspect"));
  QVERIFY2(inspector.isOpen(), "Failed to open inspector database");
  QSqlDatabase inspectDb = inspector.db();
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM items")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM collections")));
  }

  WorkerSignalSpy scanCompletedSpy(qm, &QueryManager::collectionScanCompleted);
  QVERIFY(scanCompletedSpy.isValid());

  const auto scan = [&]() {
    QMetaObject::invokeMethod(
        qm, [qm, ctx, allCollections]() { qm->ensureScannedForContext(ctx, allCollections); },
        Qt::QueuedConnection);
  };

  // First scan: 5 files -> 5 rows.
  scan();
  QTRY_COMPARE_WITH_TIMEOUT(scanCompletedSpy.count(), 1, 10'000);
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 5);
  }

  // Populate the sorted range cache (Random -> synchronous build).
  const auto fetchRangeCount = [&]() -> int {
    WorkerSignalSpy rangeSpy(qm, &QueryManager::itemsRangeLoaded);
    if (!rangeSpy.isValid()) {
      return -1;
    }
    QMetaObject::invokeMethod(
        qm,
        [qm, ctx, allCollections]() {
          qm->fetchItemsRange(ctx, allCollections, /*offset=*/0, /*limit=*/100, QString());
        },
        Qt::QueuedConnection);
    if (!rangeSpy.wait(10'000)) {
      return -1;
    }
    // itemsRangeLoaded(offset, filePaths, ...): filePaths is arg index 1.
    return rangeSpy.takeFirst().at(1).toStringList().size();
  };

  QCOMPARE(fetchRangeCount(), 5);

  // Add 2 files and rescan -> 7 rows on disk + in the DB.
  writeFiles(5, 2);
  scan();
  QTRY_COMPARE_WITH_TIMEOUT(scanCompletedSpy.count(), 2, 10'000);
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 7);
  }

  // Drop the query caches (this is what DatabaseManager wires to scan
  // completion). Without it the sorted cache — whose hash still matches since
  // the collection uuid is unchanged — would serve the stale 5-item page set.
  QMetaObject::invokeMethod(qm, &QueryManager::invalidateQueryCaches, Qt::BlockingQueuedConnection);

  // The rebuilt cache reflects the new item set.
  QCOMPARE(fetchRangeCount(), 7);
}

QTEST_MAIN(TestQueryManagerCacheInvalidation)

#include "test_querymanager_cache_invalidation.moc"
