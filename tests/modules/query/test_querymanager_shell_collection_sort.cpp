// Regression test: when a shell collection aggregates items from several
// subcollections (the showAllSubcollectionItems / queryIncludeAllCollections
// path used by the synthetic Home view), a Name sort must order items purely
// by their display name.
//
// items.path is absolute, so building the sorted-items cache with `ORDER BY
// path` grouped items by their owning subcollection's directory, surfacing as
// a collection-then-name order instead of a flat name order. The slow path
// (QueryHelpers::orderByForSortMode) already sorts by `name`; this test drives
// the precomputed-cache fast path so the two stay consistent.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <utility>

#include "collectionutils.h"
#include "querymanager.h"
#include "sessionmanager.h"
#include "uiconstants.h"
#include "workersignalspy.h"

class TestQueryManagerShellCollectionSort : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cachedNameSortIsFlatAcrossSubcollections();
};

template <typename Func> class ScopeExit {
public:
  explicit ScopeExit(Func &&func) : m_func(std::forward<Func>(func)) {}
  ~ScopeExit() { m_func(); }

  ScopeExit(const ScopeExit &) = delete;
  ScopeExit &operator=(const ScopeExit &) = delete;

private:
  Func m_func;
};

template <typename Func> auto makeScopeExit(Func &&func) -> ScopeExit<Func> {
  return ScopeExit<Func>(std::forward<Func>(func));
}

void TestQueryManagerShellCollectionSort::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("Kartend"));
  QCoreApplication::setApplicationName(QStringLiteral("kartend-test-shell-collection-sort"));
}

static auto openInspectorDb(const QString &dbFilePath) -> QSqlDatabase {
  const QString connectionName = QStringLiteral("test_querymanager_shell_sort_inspect");
  if (QSqlDatabase::contains(connectionName)) {
    QSqlDatabase::removeDatabase(connectionName);
  }
  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
  db.setDatabaseName(dbFilePath);
  if (!db.open()) {
    return {};
  }
  return db;
}

void TestQueryManagerShellCollectionSort::cachedNameSortIsFlatAcrossSubcollections() {
  QTemporaryDir rootDir;
  QVERIFY2(rootDir.isValid(), "Failed to create temporary root directory");

  // Twelve subcollections, directories coll00..coll11 (which sort in that
  // order as path prefixes). Each holds one file whose name is the *reverse*
  // of the directory order: coll00 holds "11.bin", coll11 holds "00.bin". A
  // path sort therefore yields 11,10,...,00 while a name sort yields
  // 00,01,...,11 — the two orders are fully reversed, so the bug is
  // unambiguous. Twelve collections also clears the >=10-uuid bar that gates
  // the deferred cache build.
  constexpr int kCollectionCount = 12;
  QList<CollectionConfig> allCollections;
  QStringList expectedNames;
  for (int i = 0; i < kCollectionCount; ++i) {
    const QString dirName = QStringLiteral("coll%1").arg(i, 2, 10, QChar('0'));
    QVERIFY(QDir(rootDir.path()).mkpath(dirName));
    const QString dirPath = rootDir.filePath(dirName);

    const QString fileName =
        QStringLiteral("%1.bin").arg(kCollectionCount - 1 - i, 2, 10, QChar('0'));
    QFile f(QDir(dirPath).filePath(fileName));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    CollectionConfig coll;
    coll.name = dirName;
    coll.mediaDirectory = dirPath;
    coll.includeContentSubfolders = false;
    coll.extensions = {QStringLiteral("bin")};
    allCollections.append(coll);

    expectedNames.append(fileName);
  }
  // Flat name order: 00.bin, 01.bin, ..., 11.bin.
  expectedNames.sort();

  // queryIncludeAllCollections expands the uuid-IN filter to span every
  // collection, mirroring the SQL the shell-collection view issues with
  // showAllSubcollectionItems.
  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.config = allCollections.first();
  ctx.queryIncludeAllCollections = true;
  ctx.sortMode = SortMode::NameAscending;

  SessionManager sessionManager;
  auto *qm = new QueryManager(&sessionManager);
  QThread worker;
  qm->moveToThread(&worker);
  worker.start();

  const auto workerCleanup = makeScopeExit([&]() {
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
  QSqlDatabase inspectDb = openInspectorDb(dbFilePath);
  QVERIFY2(inspectDb.isValid() && inspectDb.isOpen(), "Failed to open inspector database");

  // Clean slate so prior test runs don't pollute the result set.
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM items")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM collections")));
  }

  // Scan every collection so each file gets its own row in items.
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
  QTRY_COMPARE_WITH_TIMEOUT(scanCompletedSpy.count(), kCollectionCount, 15'000);

  // Sanity: every file landed in the items table.
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), kCollectionCount);
  }

  // WorkerSignalSpy (not QSignalSpy): see the threading note above.
  WorkerSignalSpy rangeSpy(qm, &QueryManager::itemsRangeLoaded);
  QVERIFY(rangeSpy.isValid());

  // First fetch with an offset past PRECOMPUTE_SORT_THRESHOLD: this schedules
  // the deferred sorted-items cache build (>=10 uuids) and returns nothing.
  QMetaObject::invokeMethod(
      qm,
      [qm, ctx, allCollections]() {
        qm->fetchItemsRange(ctx, allCollections,
                            /*offset=*/UIConstants::Database::PRECOMPUTE_SORT_THRESHOLD,
                            /*limit=*/100, QString());
      },
      Qt::QueuedConnection);
  QVERIFY2(rangeSpy.wait(10'000), "Timed out waiting for the priming itemsRangeLoaded");

  // Second fetch from offset 0: the deferred build has run by now, so this
  // request is served from the precomputed cache fast path.
  QMetaObject::invokeMethod(
      qm,
      [qm, ctx, allCollections]() {
        qm->fetchItemsRange(ctx, allCollections, /*offset=*/0, /*limit=*/100, QString());
      },
      Qt::QueuedConnection);
  QTRY_COMPARE_WITH_TIMEOUT(rangeSpy.count(), 2, 10'000);

  const QList<QVariant> args = rangeSpy.at(1);
  QCOMPARE(args.at(0).toInt(), 0);
  const QStringList filePaths = args.at(1).toStringList();
  QCOMPARE(filePaths.size(), kCollectionCount);

  // itemsRangeLoaded must echo the request's CollectionContext::currentIndex
  // as the trailing requestedCollectionIndex argument. itemsRangeLoaded is a
  // shared signal, so a consumer with several concurrent fetchItemsRange
  // calls in flight (e.g. the Scraper dialog cascade-checking a parent)
  // relies on this to route each result to the collection that asked for it
  // — without it the first result was consumed by every waiting handler.
  QCOMPARE(args.at(6).toInt(), ctx.currentIndex);

  // The visible order is the basename order of the returned paths. With the
  // pre-fix path sort this is 11.bin, 10.bin, ..., 00.bin (one subcollection
  // directory after another); a flat name sort interleaves them alphabetically.
  QStringList orderedNames;
  orderedNames.reserve(filePaths.size());
  for (const QString &path : filePaths) {
    orderedNames.append(QFileInfo(path).fileName());
  }

  QCOMPARE(orderedNames, expectedNames);
}

QTEST_MAIN(TestQueryManagerShellCollectionSort)

#include "test_querymanager_shell_collection_sort.moc"
