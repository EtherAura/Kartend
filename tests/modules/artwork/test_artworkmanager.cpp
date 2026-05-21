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
#include "interactionstateholder.h"
#include "itemwidget.h"

#include <QApplication>
#include <QDateTime>
#include <QPixmap>
#include <QPointer>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>
#include <memory>

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

  // loadArtworkParallel ------------------------------------------------------
  void testLoadArtworkParallel_emptyListNoop();
  void testLoadArtworkParallel_withoutSetupReferences();

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

void TestArtworkManager::initTestCase() { QVERIFY(m_tempDir.isValid()); }

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
  const QString path =
      ArtworkManager::findArtworkForFile("nonexistent.rom", m_tempDir.path());
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
                            [&handlerCalled](const QList<ArtworkInfo::Result> &, int, qint64,
                                             bool) { handlerCalled = true; });
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
