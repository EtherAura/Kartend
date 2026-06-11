// Large-collection paging test for QueryManager::fetchItemsRange
// (Kartend-1yev5): ordering/paging was previously only exercised at small N
// (a handful of scanned files), so an OFFSET/LIMIT or ORDER BY regression
// that only shows at scale — e.g. a page boundary off-by-one or an unstable
// sort under the deferred sorted-items cache — had no coverage.
//
// The 50k items rows are seeded straight into the real SQLite file through
// the inspector connection (one transaction, no filesystem scan), which keeps
// the suite fast while still driving the production fetchItemsRange SQL.
// fetchItemsRange never re-scans (it assumes ensureCollectionScanned ran
// during fetchItemCount), so the seeded rows are exactly what it pages over.

#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
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

namespace {
constexpr int kItemCount = 50'000;

QString itemName(int i) {
  return QStringLiteral("item%1").arg(i, 5, 10, QChar('0'));
}
} // namespace

class TestQueryManagerFetchRangePaging : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void orderingAndPagingHoldAt50kRows();
};

void TestQueryManagerFetchRangePaging::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-fetchrange-paging"));
}

void TestQueryManagerFetchRangePaging::orderingAndPagingHoldAt50kRows() {
  QTemporaryDir mediaDir;
  QVERIFY2(mediaDir.isValid(), "Failed to create temporary media directory");
  const QDir dir(mediaDir.path());

  CollectionConfig coll;
  coll.name = QStringLiteral("PagingColl");
  coll.mediaDirectory = mediaDir.path();
  coll.folderBrowsing.includeContentSubfolders = false;
  coll.extensions = {QStringLiteral("bin")};

  QList<CollectionConfig> allCollections{coll};

  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.config = coll;
  // Name sort maps to `ORDER BY name COLLATE NOCASE` on both the slow
  // (OFFSET/LIMIT) path and the sorted-items cache path; the zero-padded
  // names make that order fully deterministic.
  ctx.sortMode = SortMode::NameAscending;

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
                                     QStringLiteral("test_qm_fetchrange_paging_inspect"));
  QVERIFY2(inspector.isOpen(), "Failed to open inspector database");
  QSqlDatabase inspectDb = inspector.db();

  const QString uuid = CollectionUtils::computeCollectionUuid(coll.name, coll.mediaDirectory);

  // Clean slate, then seed the collection row + 50k item rows in a single
  // transaction (sub-second on SQLite; a real filesystem scan at this size
  // would dominate the suite's runtime for no extra coverage).
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM items")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM collections")));

    QSqlQuery col(inspectDb);
    col.prepare(
        QStringLiteral("INSERT INTO collections (name, uuid, last_scanned) VALUES (?, ?, ?)"));
    col.addBindValue(coll.name);
    col.addBindValue(uuid);
    col.addBindValue(QStringLiteral("2026-01-01T00:00:00Z"));
    QVERIFY2(col.exec(), qPrintable(col.lastError().text()));
    const qint64 collectionId = col.lastInsertId().toLongLong();

    QVERIFY(inspectDb.transaction());
    QSqlQuery ins(inspectDb);
    QVERIFY(ins.prepare(
        QStringLiteral("INSERT INTO items (collection_id, collection_uuid, path, rel_path, "
                       "name, last_modified) VALUES (?, ?, ?, ?, ?, ?)")));
    for (int i = 0; i < kItemCount; ++i) {
      const QString fileName = itemName(i) + QStringLiteral(".bin");
      ins.addBindValue(collectionId);
      ins.addBindValue(uuid);
      ins.addBindValue(dir.absoluteFilePath(fileName));
      ins.addBindValue(fileName);
      ins.addBindValue(itemName(i));
      ins.addBindValue(QStringLiteral("2026-01-01T00:00:00Z"));
      QVERIFY2(ins.exec(), qPrintable(ins.lastError().text()));
    }
    QVERIFY(inspectDb.commit());
  }

  // WorkerSignalSpy (not QSignalSpy): qm lives on the worker thread, and
  // Qt 6.4's QSignalSpy connects DirectConnection with no locking, so it
  // would mutate its list on the worker thread while this thread reads it.
  WorkerSignalSpy rangeSpy(qm, &QueryManager::itemsRangeLoaded);
  QVERIFY(rangeSpy.isValid());

  // Fetch one page through the production path and return its file paths.
  // Pages are requested sequentially, so each response is the spy's sole
  // pending entry.
  const auto fetchPage = [&](int offset, int limit) -> QStringList {
    QMetaObject::invokeMethod(
        qm,
        [qm, ctx, allCollections, offset, limit]() {
          qm->fetchItemsRange(ctx, allCollections, offset, limit, QString());
        },
        Qt::QueuedConnection);
    if (!rangeSpy.wait(10'000)) {
      return {};
    }
    const QList<QVariant> args = rangeSpy.takeFirst();
    if (args.at(0).toInt() != offset) {
      return {};
    }
    return args.at(1).toStringList();
  };

  const auto expectedPath = [&](int i) { return dir.absoluteFilePath(itemName(i) + ".bin"); };

  // First page: starts at the very first name, full and in order.
  const QStringList first = fetchPage(0, 100);
  QCOMPARE(first.size(), 100);
  QCOMPARE(first.first(), expectedPath(0));
  QCOMPARE(first.last(), expectedPath(99));

  // Deep-offset page: OFFSET arithmetic must hold mid-collection. By now the
  // deferred sorted-items cache may have been built for this 50k collection,
  // so this also pins slow-path/cache-path ordering consistency.
  const QStringList mid = fetchPage(25'000, 100);
  QCOMPARE(mid.size(), 100);
  QCOMPARE(mid.first(), expectedPath(25'000));
  QCOMPARE(mid.last(), expectedPath(25'099));

  // Page-boundary seam: the row right at a boundary must appear exactly once
  // across adjacent pages (the classic OFFSET off-by-one).
  const QStringList seam = fetchPage(99, 2);
  QCOMPARE(seam, (QStringList{expectedPath(99), expectedPath(100)}));

  // Final page: a limit overshooting the tail returns exactly the remainder.
  const QStringList tail = fetchPage(kItemCount - 50, 100);
  QCOMPARE(tail.size(), 50);
  QCOMPARE(tail.first(), expectedPath(kItemCount - 50));
  QCOMPARE(tail.last(), expectedPath(kItemCount - 1));
}

QTEST_MAIN(TestQueryManagerFetchRangePaging)

#include "test_querymanager_fetchrange_paging.moc"
