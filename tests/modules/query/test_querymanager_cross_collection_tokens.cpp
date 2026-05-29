// Verifies (Kartend-jb6d) that the structured-token filters introduced in
// Kartend-9qv2 (played:, tag:, missing:artwork, favorite:) apply not only
// in the CurrentCollection search mode but also in CurrentAndSubcollections
// (queryIncludeDescendants = true) and AllCollections
// (queryIncludeAllCollections = true).
//
// Both cross-collection modes funnel through the same fetchItemCountImpl /
// fetchItemsRange path that 9qv2 instrumented, so the token clauses
// already apply by construction. This test exists to lock that behaviour
// in: if a future refactor splits the cross-mode fetches into a
// dedicated SQL path, the test catches the regression by counting only
// played items across two collections.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <utility>

#include "collection/collectioncontext.h"
#include "querymanager.h"
#include "sessionmanager.h"
#include "workersignalspy.h"

class TestQueryManagerCrossCollectionTokens : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void playedTokenFiltersAcrossAllCollections();
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

void TestQueryManagerCrossCollectionTokens::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("Kartend"));
  QCoreApplication::setApplicationName(QStringLiteral("kartend-test-cross-collection-tokens"));
}

static auto openInspectorDb(const QString &dbFilePath) -> QSqlDatabase {
  const QString connectionName = QStringLiteral("test_querymanager_cross_tokens_inspect");
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

void TestQueryManagerCrossCollectionTokens::playedTokenFiltersAcrossAllCollections() {
  QTemporaryDir rootDir;
  QVERIFY2(rootDir.isValid(), "Failed to create temporary root directory");

  // Two collections, two files each (played.bin + unplayed.bin). After
  // the scan we'll bump play_count on the played.bin row in BOTH
  // collections so `played:true` should count exactly 2 across the union.
  const QString subAPath = rootDir.filePath(QStringLiteral("subA"));
  const QString subBPath = rootDir.filePath(QStringLiteral("subB"));
  QVERIFY(QDir(rootDir.path()).mkpath(QStringLiteral("subA")));
  QVERIFY(QDir(rootDir.path()).mkpath(QStringLiteral("subB")));
  for (const QString &dirPath : {subAPath, subBPath}) {
    for (const QString &fileName : {QStringLiteral("played.bin"), QStringLiteral("unplayed.bin")}) {
      QFile f(QDir(dirPath).filePath(fileName));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("x");
      f.close();
    }
  }

  CollectionConfig collA;
  collA.name = QStringLiteral("TokA");
  collA.mediaDirectory = subAPath;
  collA.extensions = {QStringLiteral("bin")};

  CollectionConfig collB;
  collB.name = QStringLiteral("TokB");
  collB.mediaDirectory = subBPath;
  collB.extensions = {QStringLiteral("bin")};

  QList<CollectionConfig> allCollections{collA, collB};

  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.config = collA;
  ctx.queryIncludeAllCollections = true;

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

  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM items")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM collections")));
  }

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

  // 4 items total across both collections.
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 4);
  }

  // Mark the played.bin row in each collection as played. Bumping play_count
  // is the canonical signal the `played:true` token watches. Match by
  // absolute path suffix instead of `name` because the scan stores the
  // basename-without-extension (or similar variant) in `items.name`; the
  // path column always carries the absolute filesystem path verbatim.
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("UPDATE items SET play_count = 5 "
                                  "WHERE path LIKE '%/played.bin'")));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items WHERE play_count > 0")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);
  }

  // queryIncludeAllCollections = true expands the uuid filter to span
  // both collections; the played:true token must clip the result to the
  // 2 items we bumped above.
  WorkerSignalSpy itemCountSpy(qm, &QueryManager::itemCountLoaded);
  QVERIFY(itemCountSpy.isValid());

  QMetaObject::invokeMethod(
      qm,
      [qm, ctx, allCollections]() {
        qm->fetchItemCount(ctx, allCollections, QStringLiteral("played:true"));
      },
      Qt::QueuedConnection);

  QVERIFY2(itemCountSpy.wait(10'000), "Timed out waiting for itemCountLoaded");
  QCOMPARE(itemCountSpy.takeFirst().at(0).toInt(), 2);

  // Negative-case sanity: without the token, the same context returns the
  // full 4-item set. This proves the count delta isn't coming from the
  // uuid-IN clause alone — the token genuinely filters.
  QMetaObject::invokeMethod(
      qm, [qm, ctx, allCollections]() { qm->fetchItemCount(ctx, allCollections, QString()); },
      Qt::QueuedConnection);
  QVERIFY2(itemCountSpy.wait(10'000), "Timed out waiting for itemCountLoaded (no token)");
  QCOMPARE(itemCountSpy.takeFirst().at(0).toInt(), 4);
}

QTEST_MAIN(TestQueryManagerCrossCollectionTokens)

#include "test_querymanager_cross_collection_tokens.moc"
