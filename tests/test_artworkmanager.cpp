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
#include "artworkmanager.h"
#include "artworkutils.h"
#include "cachemanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"

#include <QApplication>
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
  ArtworkManager manager(m_cache.get());
  QVERIFY(!manager.isSilentLoadingActive());
  // Newly constructed: m_lastUserActivity initialized to "now", so not idle.
  QVERIFY(!manager.isUserIdle());
}

void TestArtworkManager::testHasArtworkForWidget_nullWidget() {
  ArtworkManager manager(m_cache.get());
  QVERIFY(!manager.hasArtworkForWidget(nullptr));
}

void TestArtworkManager::testHasArtworkForWidget_untrackedWidget() {
  ArtworkManager manager(m_cache.get());
  ItemWidget widget;
  QVERIFY(!manager.hasArtworkForWidget(&widget));
}

// ─── Cancellation ───────────────────────────────────────────────────────────

void TestArtworkManager::testCancelAllArtworkLoading_idempotent() {
  ArtworkManager manager(m_cache.get());
  // Calling repeatedly on empty state must not crash.
  manager.cancelAllArtworkLoading();
  manager.cancelAllArtworkLoading();
  manager.cancelAllArtworkLoading();
  QVERIFY(true);
}

void TestArtworkManager::testCancelAllArtworkLoading_clearsPending() {
  ArtworkManager manager(m_cache.get());
  wireSetup(&manager);

  ItemWidget widget;
  manager.addPendingArtwork(&widget, "/nonexistent/path/to/art.png");
  // No assertion on private pendingArtwork; just verify cancel does not crash
  // and subsequent hasArtworkForWidget remains false (entry was pending, not
  // loaded).
  manager.cancelAllArtworkLoading();
  QVERIFY(!manager.hasArtworkForWidget(&widget));
}

// ─── loadArtworkParallel ────────────────────────────────────────────────────

void TestArtworkManager::testLoadArtworkParallel_emptyListNoop() {
  ArtworkManager manager(m_cache.get());
  wireSetup(&manager);
  manager.loadArtworkParallel({}, /*highPriority=*/true);
  QVERIFY(true);
}

void TestArtworkManager::testLoadArtworkParallel_withoutSetupReferences() {
  // Without setupReferences, shouldSkipArtworkLoading() returns true because
  // stackedWidget is nullptr — call must be a safe noop.
  ArtworkManager manager(m_cache.get());
  ItemWidget widget;
  ArtworkInfo info{QPointer<ItemWidget>(&widget), "/some/path.png"};
  manager.loadArtworkParallel({info}, /*highPriority=*/true);
  QVERIFY(true);
}

// ─── addPendingArtwork dedup ────────────────────────────────────────────────

void TestArtworkManager::testAddPendingArtwork_nullWidgetSafe() {
  ArtworkManager manager(m_cache.get());
  wireSetup(&manager);
  manager.addPendingArtwork(nullptr, "/a/b/c.png");
  QVERIFY(true);
}

void TestArtworkManager::testAddPendingArtwork_emptyPathSafe() {
  ArtworkManager manager(m_cache.get());
  wireSetup(&manager);
  ItemWidget widget;
  manager.addPendingArtwork(&widget, QString{});
  QVERIFY(!manager.hasArtworkForWidget(&widget));
}

void TestArtworkManager::testAddPendingArtwork_withoutStackedWidgetIsNoop() {
  // Without setupReferences the early-return guards in addPendingArtwork
  // protect against UB. Idempotent calls must remain safe.
  ArtworkManager manager(m_cache.get());
  ItemWidget widget;
  manager.addPendingArtwork(&widget, "/x.png");
  manager.addPendingArtwork(&widget, "/x.png");
  QVERIFY(!manager.hasArtworkForWidget(&widget));
}

// ─── clearPendingArtworkForWidget ───────────────────────────────────────────

void TestArtworkManager::testClearPendingArtworkForWidget_nullSafe() {
  ArtworkManager manager(m_cache.get());
  manager.clearPendingArtworkForWidget(nullptr);
  QVERIFY(true);
}

// ─── QPointer-based widget tracking ─────────────────────────────────────────

void TestArtworkManager::testClearWidgetReferences_survivesDeletedWidget() {
  ArtworkManager manager(m_cache.get());
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
