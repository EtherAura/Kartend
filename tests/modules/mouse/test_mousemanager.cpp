/**
 * @file test_mousemanager.cpp
 * @brief Unit tests for MouseManager's hold-scroll / wheel / widget-finding
 *        logic (Kartend-ior0a).
 *
 * test_mousehelpers covers the pure input math (wheel-step accumulation,
 * vertical scroll direction, horizontal-candidate computation). These tests
 * cover the manager logic around it, mirroring the TestGamepadManager
 * pattern: friend access to drive the private timer-timeout slots directly
 * (the real delays are settings-driven; waiting them out would be slow and
 * flaky), a minimal fake IScrollManager via ApplicationContext, and real
 * QWidgets / synthesized QWheelEvents under the offscreen QPA.
 *
 * Covered surfaces:
 *  - computeWheelSteps over synthesized QWheelEvents (angle / pixel / null);
 *  - click-hold timer arming (settings-driven delay) and the timeout ->
 *    hold-scroll handoff gated on the left button still being down;
 *  - vertical hold-scroll start/stop: guard paths, InteractionStateHolder
 *    flag transitions, signal contract, list-vs-grid repeat interval;
 *  - horizontal click-hold: candidate eligibility plumbing, start-index
 *    restore, and the horizontal signal/state variant;
 *  - the repeat-step slot's direction/axis emission and its self-stop when
 *    the item set vanishes;
 *  - findBestWidgetForClick / findClosestWidget against real ItemWidgets
 *    placed in a virtual-container hierarchy.
 */

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "gridlayoutcalculator.h"
#include "interactionstateholder.h"
#include "iscrollmanager.h"
#include "itemwidget.h"
#include "mousemanager.h"
#include "uiconstants/grid.h"

#include <memory>

#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QScrollArea>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QWheelEvent>
#include <QWidget>

namespace {

/// Minimal IScrollManager fake: MouseManager only reads getTotalItems(),
/// getActiveWidgets() and getMetrics() (plus a non-null check), so those
/// three are configurable members and everything else is an inert stub.
class FakeScrollManager : public IScrollManager {
public:
  int totalItems = 0;
  QHash<int, ItemWidget *> activeWidgets;
  GridMetrics metrics;

  [[nodiscard]] int getTotalItems() const override { return totalItems; }
  [[nodiscard]] const QHash<int, ItemWidget *> &getActiveWidgets() const override {
    return activeWidgets;
  }
  [[nodiscard]] const GridMetrics &getMetrics() const override { return metrics; }

  // --- inert stubs below ---
  void setupVirtualScrolling(int, const CollectionContext &) override {}
  void updateMediaItemCount(int) override {}
  void receiveItemsRange(int, const QStringList &, const QHash<QString, QString> &,
                         const QHash<QString, QString> &) override {}
  void cleanup() override {}
  void updateGridWidth(int) override {}
  void updateHorizontalGridHeight(int) override {}
  void setSidebarShrinkingActive(bool) override {}
  [[nodiscard]] bool sidebarShrinkingActive() const override { return false; }
  void updateViewType(ViewType) override {}
  void updateVirtualView() override {}
  [[nodiscard]] int getEffectiveHorizontalSpacing() const override { return 0; }
  [[nodiscard]] QString getSubcollectionName(int) const override { return {}; }
  void updateSelectionForIndex(int) override {}
  void refreshSelectionOverlayState() override {}
  void setForceSelectionOverlayVisible(bool) override {}
  void applyFilter(const QString &) override {}
  void cleanupActiveWidgets() override {}
  void clearFilter() override {}
  void showSearchLoadingOverlay() override {}
  void hideSearchLoadingOverlay() override {}
  void savePreSearchState() override {}
  void restorePreSearchState() override {}
  [[nodiscard]] bool hasPreSearchState() const override { return false; }
  [[nodiscard]] int getFilteredIndex(int visualIndex) const override { return visualIndex; }
  void recreateLayout() override {}
  void setPendingSelectionRestoreByPath(const QString &) override {}
  [[nodiscard]] bool hasPendingSelectionRestoreByPath() const override { return false; }
  [[nodiscard]] bool isArtworkPreviewVisible() const override { return false; }
  bool hideArtworkPreview() override { return false; }
  bool showMediaPreview(const QString &, const QString &, const QString &) override {
    return false;
  }
  void centerHorizontalScrollbar(int, const QList<CollectionConfig> &) override {}
  void recenterVirtualContainer() override {}
  void handleLayoutChange() override {}
  [[nodiscard]] int getCurrentGridWidth() const override { return 0; }
  void updateContextForSubcollection(int) override {}
  void applySubcollectionFilter(int) override {}
  void recalculateContainerMetrics() override {}
  void forceVirtualViewUpdate() override {}
  void preCalculateLayout() override {}
  [[nodiscard]] int indexForWidget(ItemWidget *) const override { return -1; }
  void injectCachedItems(int, const QStringList &, const QHash<QString, QString> &,
                         const QHash<QString, QString> &) override {}
  [[nodiscard]] bool getCurrentViewportForCache(int &, int &, QStringList &,
                                                QHash<QString, QString> &,
                                                QHash<QString, QString> &) const override {
    return false;
  }
  [[nodiscard]] const QStringList &getFilePaths() const override { return m_filePaths; }
  [[nodiscard]] const QHash<QString, QString> &getFileNames() const override { return m_fileNames; }
  [[nodiscard]] int getSubcollectionCount() const override { return 0; }
  [[nodiscard]] int getVirtualFolderCount() const override { return 0; }
  [[nodiscard]] QString filePathForVisualIndex(int) const override { return {}; }
  [[nodiscard]] QString virtualFolderPathForVisualIndex(int) const override { return {}; }
  void primeLayoutFor(const CollectionConfig &) override {}
  void setInitialScrollIndex(int) override {}
  [[nodiscard]] int subcollectionIndexFromActual(int) const override { return -1; }

private:
  QStringList m_filePaths;
  QHash<QString, QString> m_fileNames;
};

} // namespace

class TestMouseManager : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  // computeWheelSteps — synthesized QWheelEvents
  void wheel_nullEventIsZeroSteps();
  void wheel_angleDeltaConvertsToNotchSteps();
  void wheel_subStepAngleStillMovesOne();
  void wheel_pixelDeltaUsedWhenAngleIsZero();

  // Simple state tracking
  void state_leftMouseDownAndWheelScrollingTracked();

  // Click-hold timer
  void clickHold_timerArmsWithSettingsDelay();
  void clickHold_stopDisarmsTimer();
  void clickHold_timeoutWithButtonDownStartsHoldScrolling();
  void clickHold_timeoutWithButtonUpDoesNothing();

  // Vertical hold scrolling
  void vertical_startBelowViewportScrollsDown();
  void vertical_startNearTopScrollsUp();
  void vertical_startSetsInteractionStateAndOverlay();
  void vertical_listModeUsesFasterRepeatInterval();
  void vertical_invalidArgsRefuse();
  void vertical_missingScrollManagerRefuses();

  // Horizontal click-hold
  void horizontal_eligibleCandidateStartsHorizontalHold();
  void horizontal_startIndexRestoreRequestsSelectionUpdate();
  void horizontal_clearCandidateBlocksHorizontalHold();

  // Stop + repeat step
  void stop_resetsStateEmitsStoppedOnce();
  void step_emitsDirectionForVerticalAndHorizontal();
  void step_stopsWhenItemsVanish();

  // Widget finding (static helpers)
  void find_nullArgumentsReturnNoWidget();
  void find_emptyActiveWidgetsReturnsNoWidget();
  void find_clickInsideWidgetReturnsIt();
  void find_clickInGapSnapsToNearestWidget();
  void find_closestWidgetByCenterDistance();

private:
  /// Sets up a 10-wide grid collection, a viewport-backed scroll area and
  /// the ctx-driven references the hold-scroll path reads.
  void wireManager();

  GeneralSettings m_settings;
  ApplicationContext m_appCtx;
  FakeScrollManager m_scroll;
  InteractionStateHolder m_state;
  QVector<CollectionConfig> m_collections;
  int m_currentCollection = 0;
  std::unique_ptr<QScrollArea> m_scrollArea;
  std::unique_ptr<MouseManager> m_mgr;
};

void TestMouseManager::init() {
  m_settings = GeneralSettings{};
  m_appCtx = ApplicationContext{};
  m_scroll.totalItems = 1000;
  m_scroll.activeWidgets.clear();
  m_scroll.metrics = GridMetrics{};
  m_state.scroll() = {};
  m_state.artwork() = {};

  CollectionConfig config;
  config.name = QStringLiteral("Videos");
  config.gridLayout.itemHeight = 100;
  config.gridLayout.verticalSpacing = 10; // rowHeight = 110
  m_collections = {config};
  m_currentCollection = 0;

  m_scrollArea = std::make_unique<QScrollArea>();
  m_scrollArea->resize(400, 300);

  m_appCtx.managers.scrollManager = &m_scroll;
  m_appCtx.managers.interactionState = &m_state;

  m_mgr = std::make_unique<MouseManager>();
  wireManager();
}

void TestMouseManager::wireManager() {
  MouseManagerSetup setup;
  setup.ctx = &m_appCtx;
  setup.itemScrollArea = m_scrollArea.get();
  setup.gridContainer = nullptr;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &m_currentCollection;
  setup.generalSettings = &m_settings;
  m_mgr->setupReferences(setup);
}

void TestMouseManager::cleanup() {
  m_mgr.reset();
  m_scrollArea.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// computeWheelSteps
// ─────────────────────────────────────────────────────────────────────────────

void TestMouseManager::wheel_nullEventIsZeroSteps() {
  QCOMPARE(MouseManager::computeWheelSteps(nullptr), 0);
}

void TestMouseManager::wheel_angleDeltaConvertsToNotchSteps() {
  const QWheelEvent twoNotchesDown(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -240),
                                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QCOMPARE(MouseManager::computeWheelSteps(&twoNotchesDown), -2);

  const QWheelEvent oneNotchUp(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 120),
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QCOMPARE(MouseManager::computeWheelSteps(&oneNotchUp), 1);
}

void TestMouseManager::wheel_subStepAngleStillMovesOne() {
  // A trackpad flick smaller than one notch must still move selection.
  const QWheelEvent tinyFlick(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 40),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QCOMPARE(MouseManager::computeWheelSteps(&tinyFlick), 1);
}

void TestMouseManager::wheel_pixelDeltaUsedWhenAngleIsZero() {
  const QWheelEvent pixelScroll(QPointF(10, 10), QPointF(10, 10), QPoint(0, -60), QPoint(),
                                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QCOMPARE(MouseManager::computeWheelSteps(&pixelScroll), -1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Simple state tracking
// ─────────────────────────────────────────────────────────────────────────────

void TestMouseManager::state_leftMouseDownAndWheelScrollingTracked() {
  QVERIFY(!m_mgr->isLeftMouseDown());
  QVERIFY(!m_mgr->isWheelScrolling());

  m_mgr->setLeftMouseDown(true);
  m_mgr->setWheelScrolling(true);
  QVERIFY(m_mgr->isLeftMouseDown());
  QVERIFY(m_mgr->isWheelScrolling());

  m_mgr->setLeftMouseDown(false);
  m_mgr->setWheelScrolling(false);
  QVERIFY(!m_mgr->isLeftMouseDown());
  QVERIFY(!m_mgr->isWheelScrolling());
}

// ─────────────────────────────────────────────────────────────────────────────
// Click-hold timer
// ─────────────────────────────────────────────────────────────────────────────

void TestMouseManager::clickHold_timerArmsWithSettingsDelay() {
  m_settings.input.clickHoldDelayMs = 123;

  m_mgr->startClickHoldTimer(QPoint(10, 10), /*selectedItemIndex=*/5, /*gridWidth=*/10,
                             /*totalItems=*/100);

  QVERIFY(m_mgr->isClickHoldTimerActive());
  QCOMPARE(m_mgr->m_clickHoldTimer.interval(), 123);
}

void TestMouseManager::clickHold_stopDisarmsTimer() {
  m_mgr->startClickHoldTimer(QPoint(10, 10), 5, 10, 100);
  QVERIFY(m_mgr->isClickHoldTimerActive());

  m_mgr->stopClickHoldTimer();
  QVERIFY(!m_mgr->isClickHoldTimerActive());
}

void TestMouseManager::clickHold_timeoutWithButtonDownStartsHoldScrolling() {
  QSignalSpy started(m_mgr.get(), &MouseManager::holdScrollingStarted);

  m_mgr->setLeftMouseDown(true);
  // Row 50 is far below any plausible viewport bottom -> scroll down.
  m_mgr->startClickHoldTimer(QPoint(10, 10), /*selectedItemIndex=*/500, /*gridWidth=*/10,
                             /*totalItems=*/1000);
  m_mgr->onClickHoldTimerTimeout(); // fire the private slot directly

  QVERIFY(m_mgr->isMouseHoldScrolling());
  QCOMPARE(started.count(), 1);
  QCOMPARE(started.first().at(0).toBool(), false); // vertical
}

void TestMouseManager::clickHold_timeoutWithButtonUpDoesNothing() {
  QSignalSpy started(m_mgr.get(), &MouseManager::holdScrollingStarted);

  m_mgr->setLeftMouseDown(false);
  m_mgr->startClickHoldTimer(QPoint(10, 10), 500, 10, 1000);
  m_mgr->onClickHoldTimerTimeout();

  QVERIFY(!m_mgr->isMouseHoldScrolling());
  QCOMPARE(started.count(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Vertical hold scrolling
// ─────────────────────────────────────────────────────────────────────────────

void TestMouseManager::vertical_startBelowViewportScrollsDown() {
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), /*selectedItemIndex=*/500, /*gridWidth=*/10,
                                 /*totalItems=*/1000);

  QVERIFY(m_mgr->isMouseHoldScrolling());
  QVERIFY(!m_mgr->isHorizontalMode());
  QCOMPARE(m_mgr->holdDirection(), 1);
}

void TestMouseManager::vertical_startNearTopScrollsUp() {
  // Item on row 0 sits within one rowHeight of the unscrolled viewport's top
  // edge -> recenter by scrolling up.
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), /*selectedItemIndex=*/5, /*gridWidth=*/10,
                                 /*totalItems=*/1000);

  QVERIFY(m_mgr->isMouseHoldScrolling());
  QCOMPARE(m_mgr->holdDirection(), -1);
}

void TestMouseManager::vertical_startSetsInteractionStateAndOverlay() {
  QSignalSpy overlay(m_mgr.get(), &MouseManager::requestOverlayVisibility);
  QSignalSpy started(m_mgr.get(), &MouseManager::holdScrollingStarted);
  m_settings.input.clickHoldRepeatIntervalMs = 321;

  m_mgr->startMouseHoldScrolling(QPoint(10, 10), 500, 10, 1000);

  QVERIFY(m_state.artwork().suppressArtwork);
  QVERIFY(m_state.artwork().allowDuringSelection);
  QVERIFY(m_state.scroll().clickScroll);
  QVERIFY(m_state.scroll().clickHoldAdvancing);
  QVERIFY(!m_state.scroll().horizHoldActive);
  QCOMPARE(overlay.count(), 1);
  QCOMPARE(overlay.first().at(0).toBool(), true);
  QCOMPARE(started.count(), 1);
  QCOMPARE(m_mgr->m_mouseHoldTimer.interval(), 321); // grid-view interval
}

void TestMouseManager::vertical_listModeUsesFasterRepeatInterval() {
  m_collections[0].viewType = ViewType::List;
  m_settings.input.listClickHoldRepeatIntervalMs = 77;

  m_mgr->startMouseHoldScrolling(QPoint(10, 10), 500, 10, 1000);

  QVERIFY(m_mgr->isMouseHoldScrolling());
  QCOMPARE(m_mgr->m_mouseHoldTimer.interval(), 77);
}

void TestMouseManager::vertical_invalidArgsRefuse() {
  QSignalSpy started(m_mgr.get(), &MouseManager::holdScrollingStarted);

  m_mgr->startMouseHoldScrolling(QPoint(10, 10), /*selectedItemIndex=*/-1, 10, 1000);
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), 500, /*gridWidth=*/0, 1000);
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), 500, 10, /*totalItems=*/0);

  QVERIFY(!m_mgr->isMouseHoldScrolling());
  QCOMPARE(started.count(), 0);
}

void TestMouseManager::vertical_missingScrollManagerRefuses() {
  m_appCtx.managers.scrollManager = nullptr;
  QSignalSpy started(m_mgr.get(), &MouseManager::holdScrollingStarted);

  m_mgr->startMouseHoldScrolling(QPoint(10, 10), 500, 10, 1000);

  QVERIFY(!m_mgr->isMouseHoldScrolling());
  QCOMPARE(started.count(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Horizontal click-hold
// ─────────────────────────────────────────────────────────────────────────────

void TestMouseManager::horizontal_eligibleCandidateStartsHorizontalHold() {
  QSignalSpy started(m_mgr.get(), &MouseManager::holdScrollingStarted);

  // prev 2 -> target 4 on the same 10-wide row: eligible, rightward.
  m_mgr->updateClickHoldHorizontalCandidate(/*previousSelection=*/2, /*targetSelection=*/4,
                                            /*gridWidth=*/10);
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), /*selectedItemIndex=*/4, 10, 1000);

  QVERIFY(m_mgr->isMouseHoldScrolling());
  QVERIFY(m_mgr->isHorizontalMode());
  QCOMPARE(m_mgr->horizontalDirection(), 1);
  QVERIFY(m_state.scroll().horizHoldActive);
  QCOMPARE(started.count(), 1);
  QCOMPARE(started.first().at(0).toBool(), true); // horizontal
}

void TestMouseManager::horizontal_startIndexRestoreRequestsSelectionUpdate() {
  QSignalSpy selectionUpdate(m_mgr.get(), &MouseManager::requestSelectionUpdate);

  // The user crossed onto index 4, but by hold time the selection sits on 7:
  // the hold must rewind to where horizontal advance began.
  m_mgr->updateClickHoldHorizontalCandidate(2, 4, 10);
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), /*selectedItemIndex=*/7, 10, 1000);

  QVERIFY(m_mgr->isHorizontalMode());
  QCOMPARE(selectionUpdate.count(), 1);
  QCOMPARE(selectionUpdate.first().at(0).toInt(), 4);
}

void TestMouseManager::horizontal_clearCandidateBlocksHorizontalHold() {
  m_mgr->updateClickHoldHorizontalCandidate(2, 4, 10);
  m_mgr->clearHorizontalCandidate();

  m_mgr->startMouseHoldScrolling(QPoint(10, 10), /*selectedItemIndex=*/500, 10, 1000);

  // Falls through to the vertical path instead.
  QVERIFY(m_mgr->isMouseHoldScrolling());
  QVERIFY(!m_mgr->isHorizontalMode());
}

// ─────────────────────────────────────────────────────────────────────────────
// Stop + repeat step
// ─────────────────────────────────────────────────────────────────────────────

void TestMouseManager::stop_resetsStateEmitsStoppedOnce() {
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), 500, 10, 1000);
  QVERIFY(m_mgr->isMouseHoldScrolling());

  QSignalSpy stopped(m_mgr.get(), &MouseManager::holdScrollingStopped);
  QSignalSpy overlay(m_mgr.get(), &MouseManager::requestOverlayVisibility);

  m_mgr->stopMouseHoldScrolling();

  QVERIFY(!m_mgr->isMouseHoldScrolling());
  QCOMPARE(m_mgr->holdDirection(), 0);
  QVERIFY(!m_state.scroll().clickScroll);
  QVERIFY(!m_state.scroll().clickHoldAdvancing);
  QVERIFY(!m_state.scroll().horizHoldActive);
  QCOMPARE(stopped.count(), 1);
  QCOMPARE(overlay.count(), 1);
  QCOMPARE(overlay.first().at(0).toBool(), false);

  // A second stop is idempotent: no spurious "stopped" re-emission.
  m_mgr->stopMouseHoldScrolling();
  QCOMPARE(stopped.count(), 1);
}

void TestMouseManager::step_emitsDirectionForVerticalAndHorizontal() {
  QSignalSpy step(m_mgr.get(), &MouseManager::scrollStepRequested);

  // Vertical hold (downward).
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), 500, 10, 1000);
  m_mgr->onMouseHoldScrollStep();
  QCOMPARE(step.count(), 1);
  QCOMPARE(step.last().at(0).toInt(), 1);
  QCOMPARE(step.last().at(1).toBool(), false);
  m_mgr->stopMouseHoldScrolling();

  // Horizontal hold (leftward: prev 4 -> target 2).
  m_mgr->updateClickHoldHorizontalCandidate(4, 2, 10);
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), 2, 10, 1000);
  m_mgr->onMouseHoldScrollStep();
  QCOMPARE(step.count(), 2);
  QCOMPARE(step.last().at(0).toInt(), -1);
  QCOMPARE(step.last().at(1).toBool(), true);
}

void TestMouseManager::step_stopsWhenItemsVanish() {
  m_mgr->startMouseHoldScrolling(QPoint(10, 10), 500, 10, 1000);
  QVERIFY(m_mgr->isMouseHoldScrolling());

  QSignalSpy stopped(m_mgr.get(), &MouseManager::holdScrollingStopped);
  QSignalSpy step(m_mgr.get(), &MouseManager::scrollStepRequested);

  // Collection emptied mid-hold (e.g. filter applied): the step must stop
  // the hold instead of stepping a selection that no longer exists.
  m_scroll.totalItems = 0;
  m_mgr->onMouseHoldScrollStep();

  QVERIFY(!m_mgr->isMouseHoldScrolling());
  QCOMPARE(step.count(), 0);
  QCOMPARE(stopped.count(), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Widget finding (static helpers)
// ─────────────────────────────────────────────────────────────────────────────

void TestMouseManager::find_nullArgumentsReturnNoWidget() {
  QWidget container;
  auto [w1, i1] = MouseManager::findBestWidgetForClick(QPoint(0, 0), nullptr, &container);
  QVERIFY(!w1);
  QCOMPARE(i1, -1);

  auto [w2, i2] = MouseManager::findBestWidgetForClick(QPoint(0, 0), &m_scroll, nullptr);
  QVERIFY(!w2);
  QCOMPARE(i2, -1);
}

void TestMouseManager::find_emptyActiveWidgetsReturnsNoWidget() {
  QWidget container;
  m_scroll.activeWidgets.clear();
  auto [widget, index] = MouseManager::findBestWidgetForClick(QPoint(0, 0), &m_scroll, &container);
  QVERIFY(!widget);
  QCOMPARE(index, -1);
}

void TestMouseManager::find_clickInsideWidgetReturnsIt() {
  // grid container > virtual container (offset 5,5) > 100x100 tiles with
  // 10px spacing, two per row — the hierarchy findBestWidgetForClick maps
  // click coordinates through.
  QWidget container;
  container.resize(400, 400);
  auto *virtualContainer = new QWidget(&container);
  virtualContainer->move(5, 5);
  virtualContainer->resize(300, 300);

  auto *tile0 = new ItemWidget(virtualContainer);
  tile0->setGeometry(0, 0, 100, 100);
  auto *tile1 = new ItemWidget(virtualContainer);
  tile1->setGeometry(110, 0, 100, 100);
  auto *tile2 = new ItemWidget(virtualContainer);
  tile2->setGeometry(0, 110, 100, 100);
  container.show(); // offscreen QPA: makes the tiles isVisible()

  m_scroll.activeWidgets = {{0, tile0}, {1, tile1}, {2, tile2}};
  m_scroll.totalItems = 3;
  m_scroll.metrics.itemWidth = 100;
  m_scroll.metrics.itemHeight = 100;
  m_scroll.metrics.horizontalSpacing = 10;
  m_scroll.metrics.verticalSpacing = 10;
  m_scroll.metrics.itemsPerRow = 2;

  // Click at (60,60) in container coords -> (55,55) inside tile0.
  auto [widget, index] =
      MouseManager::findBestWidgetForClick(QPoint(60, 60), &m_scroll, &container);
  QCOMPARE(widget, tile0);
  QCOMPARE(index, 0);

  // Click inside the second column's tile.
  auto [widget1, index1] =
      MouseManager::findBestWidgetForClick(QPoint(160, 60), &m_scroll, &container);
  QCOMPARE(widget1, tile1);
  QCOMPARE(index1, 1);
}

void TestMouseManager::find_clickInGapSnapsToNearestWidget() {
  QWidget container;
  container.resize(400, 400);
  auto *virtualContainer = new QWidget(&container);
  virtualContainer->move(0, 0);
  virtualContainer->resize(300, 300);

  auto *tile0 = new ItemWidget(virtualContainer);
  tile0->setGeometry(0, 0, 100, 100);
  auto *tile1 = new ItemWidget(virtualContainer);
  tile1->setGeometry(110, 0, 100, 100);
  container.show();

  m_scroll.activeWidgets = {{0, tile0}, {1, tile1}};
  m_scroll.totalItems = 2;
  m_scroll.metrics.itemWidth = 100;
  m_scroll.metrics.itemHeight = 100;
  m_scroll.metrics.horizontalSpacing = 10;
  m_scroll.metrics.verticalSpacing = 10;
  m_scroll.metrics.itemsPerRow = 2;

  // (103,50) lands in the inter-tile gap: closer to tile0's center (50,50)
  // than tile1's (160,50), so the snap-to-nearest fallback picks tile0.
  auto [widget, index] =
      MouseManager::findBestWidgetForClick(QPoint(103, 50), &m_scroll, &container);
  QCOMPARE(widget, tile0);
  QCOMPARE(index, 0);
}

void TestMouseManager::find_closestWidgetByCenterDistance() {
  QWidget container;
  auto *tileNear = new ItemWidget(&container);
  tileNear->setGeometry(0, 0, 100, 100); // center (50,50)
  auto *tileFar = new ItemWidget(&container);
  tileFar->setGeometry(200, 200, 100, 100); // center (250,250)

  QCOMPARE(MouseManager::findClosestWidget({tileNear, tileFar, nullptr}, QPoint(60, 60)), tileNear);
  QCOMPARE(MouseManager::findClosestWidget({tileNear, tileFar}, QPoint(240, 240)), tileFar);
  QVERIFY(!MouseManager::findClosestWidget({}, QPoint(0, 0)));
}

QTEST_MAIN(TestMouseManager)
#include "test_mousemanager.moc"
