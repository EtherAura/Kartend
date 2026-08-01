/**
 * @file test_artworkmanager.cpp
 * @brief Unit tests for ArtworkManager static helpers, lifecycle safety,
 *        cancellation, pending-queue dedup, and QPointer-based widget tracking.
 *
 * These tests focus on the surfaces that can be exercised without driving the
 * full QtConcurrent worker pipeline: static image processing, cancellation
 * idempotence, addPendingArtwork dedup, clearPendingArtworkForWidget, and
 * the QPointer race during clearWidgetReferences after widget deletion.
 */

#include "applicationcontext.h"
#include "artworkloaddispatcher.h"
#include "artworkmanager.h"
#include "artworkutils.h"
#include "cachemanager.h"
#include "icachemanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"

#include <atomic>
#include <memory>
#include <QApplication>
#include <QDateTime>
#include <QPixmap>
#include <QPointer>
#include <QSemaphore>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

class TestArtworkManager : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Static helpers -----------------------------------------------------------
  void testCreateProcessedArtwork_nullPixmap();
  void testCreateProcessedArtwork_validPixmap();
  void testFindArtworkForFile_missing();
  void testFindArtworkForFile_resolvesFromFrontSubdir();
  void testFindArtworkForFile_fallsBackThroughCoverSubdirs();
  void testFindArtworkForFile_prefersFrontOverScreenshot();

  // Pure cycle algorithm ---------------------------------------
  void testNextArtworkType_emptyAvailable();
  void testNextArtworkType_singleEntry();
  void testNextArtworkType_advancesAndWraps();
  void testNextArtworkType_currentNotInList();

  // Construction / lifecycle ------------------------------------------------
  void testConstruction_initialState();
  void testHasArtworkForWidget_nullWidget();
  void testHasArtworkForWidget_untrackedWidget();

  // Cancellation -------------------------------------------------------------
  void testCancelAllArtworkLoading_idempotent();
  void testCancelAllArtworkLoading_clearsPending();
  void testDestruct_withInFlightDispatch_doesNotCrash();

  // ArtworkLoadDispatcher generation-counter regression coverage (Kartend-7rpq)
  void testDispatcher_dispatchAfterCancelAllStillCompletes();
  void testDispatcher_cancelMidFlightSuppressesHandler();
  // Abandon-on-timeout cache-handle coverage (Kartend-xoftg)
  void testDispatcher_abandonedTaskNeverTouchesCacheAfterDestruct();

  // loadArtworkParallel ------------------------------------------------------
  void testLoadArtworkParallel_emptyListNoop();
  void testLoadArtworkParallel_withoutSetupReferences();

  // Scroll-suppression / stale-widget delivery must not discard decodes ------
  void testSuppressedApply_cachesDecodedResult();
  void testDrain_cachesResultForRecycledWidget();

  // Stale-size composed card re-delivery -------------------------------------
  void testStaleComposedCard_triggersRedelivery();

  // addPendingArtwork dedup --------------------------------------------------
  void testAddPendingArtwork_nullWidgetSafe();
  void testAddPendingArtwork_emptyPathSafe();
  void testAddPendingArtwork_withoutStackedWidgetIsNoop();

  // clearPendingArtworkForWidget --------------------------------------------
  void testClearPendingArtworkForWidget_nullSafe();

  // QPointer-based widget tracking ------------------------------------------
  void testClearWidgetReferences_survivesDeletedWidget();

private:
  std::unique_ptr<CacheManager> m_cache;
  std::unique_ptr<InteractionStateHolder> m_state;
  std::unique_ptr<ApplicationContext> m_ctx;
  std::unique_ptr<QStackedWidget> m_stacked;
  QWidget *m_itemsPage = nullptr;
  QWidget *m_gridContainer = nullptr;
  QTemporaryDir m_tempDir;

  void wireSetup(ArtworkManager *manager);
};

void TestArtworkManager::initTestCase() {
  QVERIFY(m_tempDir.isValid());
}

void TestArtworkManager::cleanupTestCase() {}

void TestArtworkManager::init() {
  m_cache = std::make_unique<CacheManager>();
  m_state = std::make_unique<InteractionStateHolder>();
  m_ctx = std::make_unique<ApplicationContext>();
  // Kartend-davi: ArtworkManager now reads its cache pointer through ctx
  // rather than a constructor arg; the test's ctx must publish the cache
  // before any ArtworkManager exercises a cache call.
  m_ctx->managers.cacheManager = m_cache.get();
  m_ctx->managers.interactionState = m_state.get();

  m_stacked = std::make_unique<QStackedWidget>();
  m_itemsPage = new QWidget();
  m_gridContainer = new QWidget(m_itemsPage);
  m_stacked->addWidget(m_itemsPage);
  m_stacked->setCurrentWidget(m_itemsPage);
}

void TestArtworkManager::cleanup() {
  m_stacked.reset();
  m_itemsPage = nullptr;
  m_gridContainer = nullptr;
  m_ctx.reset();
  m_state.reset();
  m_cache.reset();
}

void TestArtworkManager::wireSetup(ArtworkManager *manager) {
  ArtworkManagerSetup setup;
  setup.ctx = m_ctx.get();
  setup.stackedWidget = m_stacked.get();
  setup.itemsPage = m_itemsPage;
  setup.gridContainer = m_gridContainer;
  setup.itemScrollArea = nullptr;
  setup.collections = nullptr;
  setup.currentCollectionIndex = nullptr;
  manager->setupReferences(setup);
}

// ─── Static helpers ─────────────────────────────────────────────────────────

void TestArtworkManager::testCreateProcessedArtwork_nullPixmap() {
  QPixmap result = ArtworkManager::createProcessedArtwork(QPixmap{});
  QVERIFY(result.isNull());
}

void TestArtworkManager::testCreateProcessedArtwork_validPixmap() {
  QPixmap input(120, 80);
  input.fill(Qt::red);
  QPixmap result = ArtworkManager::createProcessedArtwork(input);
  QVERIFY(!result.isNull());
}

void TestArtworkManager::testFindArtworkForFile_missing() {
  // Empty directory should return empty path.
  const QString path = ArtworkManager::findArtworkForFile("nonexistent.rom", m_tempDir.path());
  QVERIFY(path.isEmpty());
}

namespace {
// Write a placeholder artwork file so the resolver's existence probe
// hits. Returns the full path, or empty on failure.
QString writeArtworkFile(const QString &dir, const QString &name) {
  if (!QDir().mkpath(dir)) {
    return {};
  }
  const QString path = QDir(dir).filePath(name);
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return {};
  }
  f.write("PNG");
  f.close();
  return path;
}
} // namespace

void TestArtworkManager::testFindArtworkForFile_resolvesFromFrontSubdir() {
  // Scrapes write the cover into the `front/` typed subdir and no
  // longer drop a flat-root copy — the resolver must still find it.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString front = writeArtworkFile(QDir(tmp.path()).filePath(QStringLiteral("front")),
                                         QStringLiteral("game.png"));
  QVERIFY(!front.isEmpty());
  const QString path = ArtworkManager::findArtworkForFile("game.rom", tmp.path());
  QCOMPARE(path, front);
}

void TestArtworkManager::testFindArtworkForFile_fallsBackThroughCoverSubdirs() {
  // No `front` cover — the resolver walks the lower-priority cover
  // subdirs (box, box-3d, … screenshot) and still finds something.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shot = writeArtworkFile(QDir(tmp.path()).filePath(QStringLiteral("screenshot")),
                                        QStringLiteral("game.png"));
  QVERIFY(!shot.isEmpty());
  const QString path = ArtworkManager::findArtworkForFile("game.rom", tmp.path());
  QCOMPARE(path, shot);
}

void TestArtworkManager::testFindArtworkForFile_prefersFrontOverScreenshot() {
  // When both a front cover and a screenshot exist, `front` wins —
  // it is first in the cover-subdir priority order.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString front = writeArtworkFile(QDir(tmp.path()).filePath(QStringLiteral("front")),
                                         QStringLiteral("game.png"));
  QVERIFY(!front.isEmpty());
  QVERIFY(!writeArtworkFile(QDir(tmp.path()).filePath(QStringLiteral("screenshot")),
                            QStringLiteral("game.png"))
               .isEmpty());
  const QString path = ArtworkManager::findArtworkForFile("game.rom", tmp.path());
  QCOMPARE(path, front);
}

// ─── Pure cycle algorithm ─────────────────────────────────────

void TestArtworkManager::testNextArtworkType_emptyAvailable() {
  // No types available → no cycling possible, returns currentType verbatim.
  QCOMPARE(ArtworkUtils::nextArtworkType(QString(), QStringList{}), QString());
  QCOMPARE(ArtworkUtils::nextArtworkType(QStringLiteral("box"), QStringList{}),
           QStringLiteral("box"));
}

void TestArtworkManager::testNextArtworkType_singleEntry() {
  // One entry → cycle is a no-op, returns currentType verbatim regardless of
  // whether it matches the lone entry. Matches the "user shift+middle-clicked
  // but only one type exists" UX path: nothing visibly changes.
  const QStringList only = {QStringLiteral("box")};
  QCOMPARE(ArtworkUtils::nextArtworkType(QStringLiteral("box"), only), QStringLiteral("box"));
  QCOMPARE(ArtworkUtils::nextArtworkType(QString(), only), QString());
}

void TestArtworkManager::testNextArtworkType_advancesAndWraps() {
  // Empty-string entry represents the legacy "primary" artwork; cycle starts
  // there and wraps from the last back to it.
  const QStringList types = {QString(), QStringLiteral("box"), QStringLiteral("logo")};
  QCOMPARE(ArtworkUtils::nextArtworkType(QString(), types), QStringLiteral("box"));
  QCOMPARE(ArtworkUtils::nextArtworkType(QStringLiteral("box"), types), QStringLiteral("logo"));
  QCOMPARE(ArtworkUtils::nextArtworkType(QStringLiteral("logo"), types), QString());
}

void TestArtworkManager::testNextArtworkType_currentNotInList() {
  // Stale override (e.g., the underlying type was removed) → snap to the first
  // entry rather than getting stuck on an invalid state.
  const QStringList types = {QString(), QStringLiteral("box")};
  QCOMPARE(ArtworkUtils::nextArtworkType(QStringLiteral("screenshot"), types), QString());
}

// ─── Construction / lifecycle ───────────────────────────────────────────────

void TestArtworkManager::testConstruction_initialState() {
  ArtworkManager manager;
  QVERIFY(!manager.isSilentLoadingActive());
  // Newly constructed: m_lastUserActivity initialized to "now", so not idle.
  QVERIFY(!manager.isUserIdle());
}

void TestArtworkManager::testHasArtworkForWidget_nullWidget() {
  ArtworkManager manager;
  QVERIFY(!manager.hasArtworkForWidget(nullptr));
}

void TestArtworkManager::testHasArtworkForWidget_untrackedWidget() {
  ArtworkManager manager;
  ItemWidget widget;
  QVERIFY(!manager.hasArtworkForWidget(&widget));
}

// ─── Cancellation ───────────────────────────────────────────────────────────

void TestArtworkManager::testCancelAllArtworkLoading_idempotent() {
  ArtworkManager manager;
  // Calling repeatedly on empty state must not crash.
  manager.cancelAllArtworkLoading();
  manager.cancelAllArtworkLoading();
  manager.cancelAllArtworkLoading();
  QVERIFY(true);
}

void TestArtworkManager::testCancelAllArtworkLoading_clearsPending() {
  ArtworkManager manager;
  wireSetup(&manager);

  ItemWidget widget;
  manager.addPendingArtwork(&widget, "/nonexistent/path/to/art.png");
  // No assertion on private pendingArtwork; just verify cancel does not crash
  // and subsequent hasArtworkForWidget remains false (entry was pending, not
  // loaded).
  manager.cancelAllArtworkLoading();
  QVERIFY(!manager.hasArtworkForWidget(&widget));
}

void TestArtworkManager::testDestruct_withInFlightDispatch_doesNotCrash() {
  // Exercises ~ArtworkManager()'s "abandon the QThreadPool without waiting"
  // path: the destructor sets the cancellation flag, clears queued tasks,
  // and intentionally leaks m_artworkThreadPool rather than blocking on
  // ~QThreadPool. Validates the path doesn't crash when futures are still
  // scheduled at destruction time.
  //
  // Build a real artwork file on disk so loadArtworkParallel actually
  // dispatches QtConcurrent work (rather than short-circuiting on a null
  // image). Several entries gives the pool a non-trivial backlog.
  const QString artPath = m_tempDir.path() + "/inflight.png";
  QPixmap onDisk(400, 400);
  onDisk.fill(Qt::magenta);
  QVERIFY(onDisk.save(artPath, "PNG"));

  auto *manager = new ArtworkManager();
  wireSetup(manager);

  QList<ItemWidget *> widgets;
  QList<ArtworkInfo> infos;
  for (int i = 0; i < 8; ++i) {
    auto *w = new ItemWidget();
    widgets.append(w);
    infos.append(ArtworkInfo{QPointer<ItemWidget>(w), artPath});
  }
  manager->loadArtworkParallel(infos, /*highPriority=*/true);

  // Yield once so the dispatch queue is observed by the pool, then destruct
  // immediately — the cancellation flag must drive worker tasks to early-
  // return and the destructor must return without crashing or hanging.
  QCoreApplication::processEvents();
  delete manager;

  qDeleteAll(widgets);
  QCoreApplication::processEvents();

  QVERIFY(true);
}

// ─── ArtworkLoadDispatcher generation-counter coverage (Kartend-7rpq) ───────

void TestArtworkManager::testDispatcher_dispatchAfterCancelAllStillCompletes() {
  // The Kartend-uxo0 regression: under the prior token-swap design, a
  // dispatch issued immediately after cancelAll() captured the
  // still-cancelled token and short-circuited inside the worker, so the
  // handler never fired and the requesting widget stalled forever on a
  // blank tile. The generation-counter rewrite reads the counter at
  // dispatch time, so a post-cancelAll() dispatch picks up the bumped
  // generation and runs to completion. Guard the regression here.
  const QString artPath = m_tempDir.path() + "/dispatcher_postcancel.png";
  QPixmap onDisk(64, 64);
  onDisk.fill(Qt::cyan);
  QVERIFY(onDisk.save(artPath, "PNG"));

  ArtworkLoadDispatcher dispatcher(m_cache.get());

  // Pre-cancel: nothing is in flight so this is purely a generation bump.
  dispatcher.cancelAll();

  ItemWidget widget;
  QList<ArtworkInfo> batch;
  batch.append(ArtworkInfo{QPointer<ItemWidget>(&widget), artPath});

  bool handlerCalled = false;
  int seenResults = -1;
  dispatcher.dispatchBatch(std::move(batch), /*highPriority=*/true,
                           [&handlerCalled, &seenResults](
                               const QList<ArtworkInfo::Result> &results, int /*requestedCount*/,
                               qint64 /*elapsedMs*/, bool /*highPriority*/) {
                             handlerCalled = true;
                             seenResults = results.size();
                           });

  QTRY_VERIFY_WITH_TIMEOUT(handlerCalled, 5000);
  QCOMPARE(seenResults, 1);
}

void TestArtworkManager::testDispatcher_cancelMidFlightSuppressesHandler() {
  // cancelAll() invoked while a batch is on the worker pool must bump the
  // generation so the worker's per-item cancellation check observes the
  // mismatch and bails before invoking the handler. We dispatch a batch
  // with several entries (so the worker spends time decoding), call
  // cancelAll immediately, and then assert the handler does NOT fire within
  // a generous timeout — the cancellation observation drops the queued
  // QMetaObject::invokeMethod posting entirely.
  const QString artPath = m_tempDir.path() + "/dispatcher_midflight.png";
  QPixmap onDisk(400, 400);
  onDisk.fill(Qt::magenta);
  QVERIFY(onDisk.save(artPath, "PNG"));

  ArtworkLoadDispatcher dispatcher(m_cache.get());

  QList<ItemWidget *> widgets;
  QList<ArtworkInfo> batch;
  for (int i = 0; i < 8; ++i) {
    auto *w = new ItemWidget();
    widgets.append(w);
    batch.append(ArtworkInfo{QPointer<ItemWidget>(w), artPath});
  }

  bool handlerCalled = false;
  dispatcher.dispatchBatch(std::move(batch), /*highPriority=*/false,
                           [&handlerCalled](const QList<ArtworkInfo::Result> &, int, qint64, bool) {
                             handlerCalled = true;
                           });
  dispatcher.cancelAll();

  // Pump events for long enough that a non-cancelled batch would have
  // delivered. If cancellation is honoured the handler never runs.
  const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 500;
  while (QDateTime::currentMSecsSinceEpoch() < deadline) {
    QCoreApplication::processEvents();
  }

  QVERIFY2(!handlerCalled,
           "cancelAll() before delivery must suppress the handler on the queued posting");

  qDeleteAll(widgets);
  QCoreApplication::processEvents();
}

namespace {

/// ICacheManager double whose disk-cache read blocks on a caller-controlled
/// gate, simulating wedged storage so the dispatcher destructor's bounded
/// pool drain expires and abandons the task (Kartend-xoftg).
class BlockingCacheMock : public ICacheManager {
public:
  std::atomic<int> diskCacheCalls{0};
  QSemaphore entered;
  QSemaphore gate;

  QImage tryLoadArtworkImageFromDiskCache(const QString & /*artworkPath*/) override {
    diskCacheCalls.fetch_add(1, std::memory_order_acq_rel);
    entered.release();
    gate.acquire(); // wedge until the test releases us
    return {};
  }

  // Inert remainder of the interface.
  void initialize() override {}
  void saveToDisk() override {}
  void saveToDiskForShutdown() override {}
  void scheduleSaveToDisk(int /*delayMs*/) override {}
  [[nodiscard]] CacheTimestampsSnapshot snapshotTimestampsForShutdown() const override {
    return {};
  }
  void cancelPendingIo() override {}
  [[nodiscard]] QPixmap getArtwork(const QString & /*artworkPath*/) override { return {}; }
  [[nodiscard]] QPixmap getArtworkFromMemoryOnly(const QString & /*artworkPath*/) override {
    return {};
  }
  void cacheArtwork(const QString & /*artworkPath*/, const QPixmap & /*pixmap*/) override {}
  void cacheArtworkInMemoryOnly(const QString & /*artworkPath*/,
                                const QPixmap & /*pixmap*/) override {}
  void clearCollectionCache(const QString & /*artworkDirectoryPrefix*/) override {}
  void releaseGuiResources() override {}
  void setArtworkCacheBudgetMB(int /*megabytes*/) override {}
  [[nodiscard]] CacheMetrics metrics() const override { return {}; }
  void resetMetrics() override {}
  void logMetrics() const override {}
  [[nodiscard]] qint64 getCacheSize() const override { return 0; }
};

} // namespace

void TestArtworkManager::testDispatcher_abandonedTaskNeverTouchesCacheAfterDestruct() {
  // Kartend-xoftg: workers must reach the disk cache only through the
  // dispatcher's shared cache handle. When the destructor's bounded pool
  // drain expires (wedged storage) and the task is abandoned, the handle
  // has been invalidated — so the task must NEVER call back into the cache
  // once ~ArtworkLoadDispatcher returns, even though CacheManager is only
  // destroyed later in ApplicationManager teardown. This test wedges the
  // first disk-cache read past the drain budget and asserts no further
  // cache call happens after destruction. Runtime ~2.5s (2s drain budget
  // + 500ms handle quiesce budget, both intentionally exhausted).
  BlockingCacheMock mock;
  auto *dispatcher = new ArtworkLoadDispatcher(&mock);

  QList<ItemWidget *> widgets;
  QList<ArtworkInfo> batch;
  for (int i = 0; i < 3; ++i) {
    auto *w = new ItemWidget();
    widgets.append(w);
    batch.append(ArtworkInfo{QPointer<ItemWidget>(w),
                             m_tempDir.path() + QStringLiteral("/missing_%1.png").arg(i)});
  }

  bool handlerCalled = false;
  dispatcher->dispatchBatch(std::move(batch), /*highPriority=*/false,
                            [&handlerCalled](const QList<ArtworkInfo::Result> &, int, qint64,
                                             bool) { handlerCalled = true; });

  // Wait until the worker is wedged inside the mock's disk-cache read.
  QVERIFY2(mock.entered.tryAcquire(1, 5000), "worker never reached the disk cache");
  QCOMPARE(mock.diskCacheCalls.load(std::memory_order_acquire), 1);

  // Destruct while the read is wedged: the 2s drain budget expires, the
  // pool is abandoned, and the handle quiesce budget (500ms) also expires
  // because the worker still holds the read pin. The destructor must
  // return regardless (bounded teardown) — the leaked pool is the
  // documented abandon-on-timeout trade-off.
  delete dispatcher;

  // Un-wedge the abandoned task. It exits the mock, and on its next
  // per-item step must observe the invalidated handle / bumped generation
  // and bail without ever touching the cache again.
  mock.gate.release(widgets.size()); // release generously; only one acquire is pending
  const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 500;
  while (QDateTime::currentMSecsSinceEpoch() < deadline) {
    QCoreApplication::processEvents();
  }

  QCOMPARE(mock.diskCacheCalls.load(std::memory_order_acquire), 1);
  QVERIFY2(!handlerCalled, "abandoned batch must not deliver its handler after destruction");

  qDeleteAll(widgets);
  QCoreApplication::processEvents();
}

// ─── loadArtworkParallel ────────────────────────────────────────────────────

void TestArtworkManager::testLoadArtworkParallel_emptyListNoop() {
  ArtworkManager manager;
  wireSetup(&manager);
  manager.loadArtworkParallel({}, /*highPriority=*/true);
  QVERIFY(true);
}

void TestArtworkManager::testLoadArtworkParallel_withoutSetupReferences() {
  // Without setupReferences, shouldSkipArtworkLoading() returns true because
  // stackedWidget is nullptr — call must be a safe noop.
  ArtworkManager manager;
  ItemWidget widget;
  ArtworkInfo info{QPointer<ItemWidget>(&widget), "/some/path.png"};
  manager.loadArtworkParallel({info}, /*highPriority=*/true);
  QVERIFY(true);
}

// ─── Suppressed / stale-widget delivery keeps the decode ────────────────────

void TestArtworkManager::testSuppressedApply_cachesDecodedResult() {
  // Artwork delivered while scroll suppression is active is requeued instead
  // of applied — but the decoded image must land in the memory cache so the
  // post-scroll pass resolves through the cache fast path with zero
  // re-decode (the old requeue stripped the Result back to a bare
  // ArtworkInfo and threw the decode away).
  const QString artPath = m_tempDir.path() + "/suppressed_cache.png";
  QPixmap onDisk(400, 400);
  onDisk.fill(Qt::darkGreen);
  QVERIFY(onDisk.save(artPath, "PNG"));

  ArtworkManager manager;
  wireSetup(&manager);

  ItemWidget widget;
  // Basename must match the artwork so the pre-dispatch identity check in
  // collectUncachedAndApplyCached lets the item through.
  widget.setFilePath(m_tempDir.path() + "/suppressed_cache.rom");
  ArtworkInfo info{QPointer<ItemWidget>(&widget), artPath};
  info.widgetIdentity = widget.getFilePath();

  // Suppression is read at delivery time; the main thread doesn't re-enter
  // the event loop until QTRY below, so the flag is guaranteed set first.
  m_state->setGlideAnimating(true);
  manager.loadArtworkParallel({info}, /*highPriority=*/true);

  QTRY_VERIFY_WITH_TIMEOUT(!manager.getCachedPixmap(artPath).isNull(), 5000);
  m_state->setGlideAnimating(false);
}

void TestArtworkManager::testDrain_cachesResultForRecycledWidget() {
  // A widget recycled to a different item while its artwork was in flight
  // makes the delivery skip that widget — but caching is path-keyed and
  // widget-independent, so the decoded image must still be cached instead of
  // discarded with the stale result.
  const QString artPath = m_tempDir.path() + "/stale_widget.png";
  QPixmap onDisk(400, 400);
  onDisk.fill(Qt::darkBlue);
  QVERIFY(onDisk.save(artPath, "PNG"));

  ArtworkManager manager;
  wireSetup(&manager);

  ItemWidget widget;
  widget.setFilePath(m_tempDir.path() + "/stale_widget.rom");
  ArtworkInfo info{QPointer<ItemWidget>(&widget), artPath};
  info.widgetIdentity = widget.getFilePath();
  manager.loadArtworkParallel({info}, /*highPriority=*/true);

  // Recycle the widget before delivery: the completion handler is queued to
  // the main thread and can only run once QTRY re-enters the event loop, so
  // the identity mismatch is deterministic.
  widget.setFilePath(m_tempDir.path() + "/other_item.rom");

  QTRY_VERIFY_WITH_TIMEOUT(!manager.getCachedPixmap(artPath).isNull(), 5000);
  QVERIFY2(!manager.hasArtworkForWidget(&widget),
           "recycled widget must not be marked loaded by the stale delivery");
}

// ─── Stale-size composed card re-delivery ───────────────────────────────────

void TestArtworkManager::testStaleComposedCard_triggersRedelivery() {
  // A worker-composed card is display-final: after a tile dimension change it
  // can't be re-composited without doubling its border. addPendingArtwork
  // must treat a loaded widget holding a stale-size card as needing
  // re-delivery, and the cache fast path then rebuilds it at the new size.
  const QString artPath = m_tempDir.path() + "/composed.png";
  // 400x400: comfortably above the memory cache's MIN_PIXMAP_SIZE gate.
  QPixmap onDisk(400, 400);
  onDisk.fill(Qt::darkRed);
  QVERIFY(onDisk.save(artPath, "PNG"));

  ArtworkManager manager;
  wireSetup(&manager);
  m_cache->cacheArtworkInMemoryOnly(artPath, onDisk);

  ItemWidget widget;
  widget.setFilePath(m_tempDir.path() + "/composed.rom");
  widget.setItemDimensions(220, 280);
  QVERIFY(widget.imageLabel);
  const QSize labelSize = widget.imageLabel->size();
  QVERIFY(!labelSize.isEmpty());

  // Cache hit marks the widget loaded (raw pixmap path).
  manager.addPendingArtwork(&widget, artPath);
  QVERIFY(manager.hasArtworkForWidget(&widget));

  // Simulate a worker-composed card delivery at the current tile size.
  QPixmap card(labelSize);
  card.fill(Qt::blue);
  widget.setComposedArtwork(card);
  QVERIFY(!widget.hasStaleComposedArtwork());

  // Same size + loaded → dedup early-return, composed card untouched.
  manager.addPendingArtwork(&widget, artPath);
  QVERIFY(!widget.hasStaleComposedArtwork());

  // Dimension change: the stored card no longer matches the label.
  widget.setItemDimensions(320, 400);
  QVERIFY(widget.imageLabel->size() != labelSize);
  QVERIFY(widget.hasStaleComposedArtwork());

  // Re-delivery: the stale card is replaced through the cache fast path
  // (raw pixmap → recomposed at the current size), and the widget is
  // re-registered as loaded.
  manager.addPendingArtwork(&widget, artPath);
  QVERIFY(!widget.hasStaleComposedArtwork());
  QVERIFY(manager.hasArtworkForWidget(&widget));
}

// ─── addPendingArtwork dedup ────────────────────────────────────────────────

void TestArtworkManager::testAddPendingArtwork_nullWidgetSafe() {
  ArtworkManager manager;
  wireSetup(&manager);
  manager.addPendingArtwork(nullptr, "/a/b/c.png");
  QVERIFY(true);
}

void TestArtworkManager::testAddPendingArtwork_emptyPathSafe() {
  ArtworkManager manager;
  wireSetup(&manager);
  ItemWidget widget;
  manager.addPendingArtwork(&widget, QString{});
  QVERIFY(!manager.hasArtworkForWidget(&widget));
}

void TestArtworkManager::testAddPendingArtwork_withoutStackedWidgetIsNoop() {
  // Without setupReferences the early-return guards in addPendingArtwork
  // protect against UB. Idempotent calls must remain safe.
  ArtworkManager manager;
  ItemWidget widget;
  manager.addPendingArtwork(&widget, "/x.png");
  manager.addPendingArtwork(&widget, "/x.png");
  QVERIFY(!manager.hasArtworkForWidget(&widget));
}

// ─── clearPendingArtworkForWidget ───────────────────────────────────────────

void TestArtworkManager::testClearPendingArtworkForWidget_nullSafe() {
  ArtworkManager manager;
  manager.clearPendingArtworkForWidget(nullptr);
  QVERIFY(true);
}

// ─── QPointer-based widget tracking ─────────────────────────────────────────

void TestArtworkManager::testClearWidgetReferences_survivesDeletedWidget() {
  ArtworkManager manager;
  wireSetup(&manager);

  // Use a heap-allocated widget so we can delete it and then exercise
  // clearWidgetReferences(); QPointer entries inside loadedArtwork must
  // be tolerated as null without crashing.
  auto *widget = new ItemWidget();
  manager.addPendingArtwork(widget, "/some/path.png");
  delete widget;

  manager.clearWidgetReferences();
  QVERIFY(true); // Survived deleted-widget cleanup without crashing.
}

QTEST_MAIN(TestArtworkManager)
#include "test_artworkmanager.moc"
