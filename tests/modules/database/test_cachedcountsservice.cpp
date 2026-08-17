// Unit tests for CachedCountsService stale-completion handling.
//
// CachedCountsService debounces requestUpdate() and emits dispatchToWorker
// once the timer fires. Each dispatch carries a monotonic generation token;
// when the worker reports back via onWorkerComputed, the service must drop
// completions whose generation does not match the latest dispatch so that
// stale results never overwrite a fresher SessionManager state.

#include "cachedcountsservice.h"
#include "collection/collectionconfig.h"
#include "isessionmanager.h"

#include <QObject>
#include <QSignalSpy>
#include <QTest>

namespace {

// No-op ISessionManager that lets us pass a non-null pointer to
// CachedCountsService without touching disk. The stale-drop path returns
// before any of these methods are called.
class NoopSessionManager final : public ISessionManager {
public:
  void initialize() override {}
  void saveToDisk() override { ++m_saveCount; }
  void saveToDiskForShutdown() override {}
  [[nodiscard]] QByteArray snapshotSessionJsonBytesForShutdown() const override { return {}; }

  void setLastSelected(const QString & /*collectionName*/, int /*index*/,
                       const QString & /*title*/) override {}
  [[nodiscard]] int getLastSelectedIndex(const QString & /*collectionName*/) const override {
    return -1;
  }
  [[nodiscard]] qint64 getGlobalItemCount() const override { return 0; }
  void setGlobalItemCount(qint64 /*count*/) override { ++m_setGlobalCount; }
  void setCollectionCounts(const CollectionConfig & /*collection*/,
                           const QList<CollectionConfig> & /*allCollections*/, qint64 /*itemCount*/,
                           qint64 /*recursiveCount*/) override {}
  [[nodiscard]] bool getCollectionCounts(const CollectionConfig & /*collection*/,
                                         const QList<CollectionConfig> & /*allCollections*/,
                                         qint64 &itemCount, qint64 &recursiveCount) const override {
    itemCount = 0;
    recursiveCount = 0;
    return false;
  }
  void setCachedViewport(const QString & /*collectionKey*/, int /*startIndex*/, int /*totalItems*/,
                         const QStringList & /*filePaths*/,
                         const QHash<QString, QString> & /*fileNames*/,
                         const QHash<QString, QString> & /*artworkPaths*/) override {}
  [[nodiscard]] CachedViewport getCachedViewport(const QString & /*collectionKey*/) const override {
    return {};
  }
  void clearStaleCollections(const QList<CollectionConfig> & /*currentCollections*/) override {}
  void setCollectionTreeExpandedKeys(const QStringList & /*keys*/) override {}
  QStringList collectionTreeExpandedKeys() const override { return {}; }

  int m_saveCount = 0;
  int m_setGlobalCount = 0;
};

} // namespace

class TestCachedCountsService : public QObject {
  Q_OBJECT

private slots:
  void testDebounceCollapsesIntoSingleDispatch();
  void testGenerationTokenMonotonic();
  void testStaleCompletionDropped();
  void testLatestCompletionApplied();
};

// Two back-to-back requestUpdate() calls inside the debounce window must
// collapse into a single worker dispatch carrying the latest payload.
void TestCachedCountsService::testDebounceCollapsesIntoSingleDispatch() {
  NoopSessionManager session;
  CachedCountsService service(&session, /*debounceMs=*/10);

  QSignalSpy dispatchSpy(&service, &CachedCountsService::dispatchToWorker);

  CollectionConfig a;
  a.name = "a";
  CollectionConfig b;
  b.name = "b";

  service.requestUpdate({a}, {QStringLiteral("a")});
  service.requestUpdate({a, b}, {QStringLiteral("a"), QStringLiteral("b")});

  QVERIFY(dispatchSpy.wait(500));
  QCOMPARE(dispatchSpy.count(), 1);
  const auto &args = dispatchSpy.first();
  const auto uuids = args.at(1).value<QStringList>();
  QCOMPARE(uuids, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

// Successive dispatches must carry strictly increasing generation tokens.
void TestCachedCountsService::testGenerationTokenMonotonic() {
  NoopSessionManager session;
  CachedCountsService service(&session, /*debounceMs=*/5);

  QSignalSpy dispatchSpy(&service, &CachedCountsService::dispatchToWorker);

  CollectionConfig a;
  a.name = "a";

  service.requestUpdate({a}, {QStringLiteral("a")});
  QVERIFY(dispatchSpy.wait(500));
  QCOMPARE(dispatchSpy.count(), 1);
  const quint64 firstGen = dispatchSpy.at(0).at(0).value<quint64>();

  service.requestUpdate({a}, {QStringLiteral("a")});
  QVERIFY(dispatchSpy.wait(500));
  QCOMPARE(dispatchSpy.count(), 2);
  const quint64 secondGen = dispatchSpy.at(1).at(0).value<quint64>();

  QVERIFY2(secondGen > firstGen, "Generation tokens must be monotonically increasing");
}

// Simulates the regression: a worker completion arrives with an older
// generation after a newer dispatch has been issued. The service must drop
// the stale result silently (no updated() signal, no SessionManager writes).
void TestCachedCountsService::testStaleCompletionDropped() {
  NoopSessionManager session;
  CachedCountsService service(&session, /*debounceMs=*/5);

  QSignalSpy dispatchSpy(&service, &CachedCountsService::dispatchToWorker);
  QSignalSpy updatedSpy(&service, &CachedCountsService::updated);

  CollectionConfig a;
  a.name = "a";

  service.requestUpdate({a}, {QStringLiteral("a")});
  QVERIFY(dispatchSpy.wait(500));
  const quint64 firstGen = dispatchSpy.at(0).at(0).value<quint64>();

  // Issue a newer dispatch so firstGen is now stale from the service's POV.
  service.requestUpdate({a}, {QStringLiteral("a")});
  QVERIFY(dispatchSpy.wait(500));
  const quint64 secondGen = dispatchSpy.at(1).at(0).value<quint64>();
  QVERIFY(secondGen > firstGen);

  // Now deliver the late completion belonging to firstGen.
  service.onWorkerComputed(firstGen, /*globalCount=*/123, {{QStringLiteral("a"), 999}});

  QCOMPARE(updatedSpy.count(), 0);
  QCOMPARE(session.m_setGlobalCount, 0);
  QCOMPARE(session.m_saveCount, 0);
}

// The matching completion is applied: updated() fires and SessionManager
// sees both the global count and a persist call.
void TestCachedCountsService::testLatestCompletionApplied() {
  NoopSessionManager session;
  CachedCountsService service(&session, /*debounceMs=*/5);

  QSignalSpy dispatchSpy(&service, &CachedCountsService::dispatchToWorker);
  QSignalSpy updatedSpy(&service, &CachedCountsService::updated);

  CollectionConfig a;
  a.name = "a";

  service.requestUpdate({a}, {QStringLiteral("a")});
  QVERIFY(dispatchSpy.wait(500));
  const quint64 gen = dispatchSpy.at(0).at(0).value<quint64>();

  service.onWorkerComputed(gen, /*globalCount=*/42, {{QStringLiteral("a"), 7}});

  QCOMPARE(updatedSpy.count(), 1);
  QCOMPARE(session.m_setGlobalCount, 1);
  QCOMPARE(session.m_saveCount, 1);
}

QTEST_MAIN(TestCachedCountsService)
#include "test_cachedcountsservice.moc"
