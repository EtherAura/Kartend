// Unit coverage for WheelEventHandler's deterministic core: the
// applySelectionDelta selection-math (row/column stepping, mouseWheelRows +
// velocity scaling, single-step List/CoverFlow, ±1 horizontal column clamp,
// wrap modulo, clamp, zero/empty guards) and the onAnimationFinished cleanup +
// scrollEnded signal contract (Kartend audit 4yktu).
//
// applySelectionDelta is pure index arithmetic over CollectionConfig +
// GeneralSettings + stubbed totalItems/currentSelection with NO scrollbar /
// viewport geometry on its own code path — geometry only enters via
// computeTargetScroll on the handleEvent path, which stays in the manual tier.
// The handler's selection-math + cleanup methods are private; this test is a
// declared friend (see wheeleventhandler.h) so it drives them directly,
// mirroring the friend-access pattern test_mousemanager uses.
#include <QScrollArea>
#include <QSignalSpy>
#include <QTest>

// WheelEventHandler holds a QPointer<QScrollArea> member with QScrollArea only
// forward-declared in its header. Constructing the handler here instantiates
// that QPointer, which needs the complete type — newer Qt tolerates the
// incomplete type, but the CI floor (Qt 6.4.2) rejects it. Include it directly
// (Kartend audit 4yktu).
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/validationhelpers.h"
#include "fakescrollmanager.h"
#include "interactionstateholder.h"
#include "stubselectionmanager.h"
#include "wheeleventhandler.h"

using KartendTest::FakeScrollManager;
using KartendTest::StubSelectionManager;

namespace {

/// Effective grid width for the deterministic Grid cases, computed the same way
/// the handler does (effectiveGridWidth on the active collection) so the
/// assertions track the algorithm rather than a hard-coded layout.
int gridWidthFor(const CollectionConfig &config) {
  return CollectionUtils::effectiveGridWidth(config, /*sidebarShrinkingActive=*/false);
}

} // namespace

class TestWheelEventHandler : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();

  void deltaZeroStepsIsNoOpReturnsFalse();
  void deltaGridMovesByRowStrideWheelSteps();
  void deltaVerticalAppliesMouseWheelRowsAndVelocityMultiplier();
  void deltaListViewMovesSingleItemPerNotch();
  void deltaHorizontalClampsMagnitudeToOneColumn();
  void deltaWrapEnabledWrapsForwardAtUpperBoundary();
  void deltaWrapDisabledClampsToValidRange();
  void deltaNoMovementReturnsWrapFlagOnly();
  void deltaZeroTotalItemsBailsSafely();
  void deltaNoScrollManagerBails();
  void onAnimationFinishedClearsFlagsAndEmitsScrollEnded();

private:
  /// Wire the handler against a fresh ctx for a Grid home view (-1 sentinel)
  /// or a single-collection list with viewIndex=0. Pass an empty list +
  /// viewIndex=-1 for the Grid/home cases.
  void wire(int viewIndex);

  GeneralSettings m_settings;
  ApplicationContext m_ctx;
  FakeScrollManager m_scroll;
  StubSelectionManager m_sel;
  InteractionStateHolder m_state;
  QList<CollectionConfig> m_collections;
  int m_viewIndex = -1;
  WheelEventHandler m_handler;
};

void TestWheelEventHandler::init() {
  // IScrollManager / FakeScrollManager are QObjects (copy/move-disabled), so
  // reset the configurable fields in place rather than reassigning the object.
  m_settings = GeneralSettings{};
  m_ctx = ApplicationContext{};
  m_scroll.totalItems = 0;
  m_scroll.activeWidgets.clear();
  m_scroll.metrics = GridMetrics{};
  m_scroll.subcollectionCount = 0;
  m_scroll.virtualFolderCount = 0;
  m_scroll.filteredIndexOverride = -1;
  m_sel = StubSelectionManager{};
  m_state.scroll() = {};
  m_state.arrow() = {};
  m_collections.clear();
  m_viewIndex = -1;
}

void TestWheelEventHandler::cleanup() {}

void TestWheelEventHandler::wire(int viewIndex) {
  m_viewIndex = viewIndex;
  m_ctx.managers.seedScrollRoles(&m_scroll);
  m_ctx.managers.selectionManager = &m_sel;
  m_ctx.managers.interactionState = &m_state;
  // ViewportManager / MouseManager / AnimationManager intentionally left null:
  // the handler's null-guards skip them, keeping the delta-math deterministic.

  WheelEventHandler::Setup setup;
  setup.ctx = &m_ctx;
  setup.itemScrollArea = nullptr; // not on the applySelectionDelta path
  setup.stackedWidget = nullptr;
  setup.itemsPage = nullptr;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &m_viewIndex;
  setup.generalSettings = &m_settings;
  m_handler.setupReferences(setup);
}

// ─── delta math ──────────────────────────────────────────────────────────────

void TestWheelEventHandler::deltaZeroStepsIsNoOpReturnsFalse() {
  wire(/*viewIndex=*/-1);
  m_scroll.totalItems = 100;
  m_sel.index = 5;

  QVERIFY(!m_handler.applySelectionDelta(0));
  QCOMPARE(m_sel.setSelectedIndexCalls, 0);
  QCOMPARE(m_sel.notifyCalls, 0);
}

void TestWheelEventHandler::deltaGridMovesByRowStrideWheelSteps() {
  wire(/*viewIndex=*/-1); // Grid home view, default config (gridWidth = 4)
  m_scroll.totalItems = 100;
  m_sel.index = 20;
  m_settings.input.mouseWheelRows = 1;
  m_settings.input.scrollVelocityMultiplier = 1.0;

  const CollectionConfig defaultConfig;
  const int gw = gridWidthFor(defaultConfig);

  // Positive wheelSteps = scroll up = one row toward index 0.
  const bool wrapped = m_handler.applySelectionDelta(1);

  QVERIFY(!wrapped);
  QCOMPARE(m_sel.setSelectedIndexCalls, 1);
  QCOMPARE(m_sel.lastSetIndex, 20 - gw);
  QVERIFY(m_sel.notified);
}

void TestWheelEventHandler::deltaVerticalAppliesMouseWheelRowsAndVelocityMultiplier() {
  wire(/*viewIndex=*/-1);
  m_scroll.totalItems = 1000;
  m_sel.index = 500;
  m_settings.input.mouseWheelRows = 2;
  m_settings.input.scrollVelocityMultiplier = 1.5;

  const CollectionConfig defaultConfig;
  const int gw = gridWidthFor(defaultConfig);

  // wheelSteps = -1 (down): rowDelta = 1*2 = 2, then * 1.5 = 3 (round toward
  // travel) -> 3 rows down.
  m_handler.applySelectionDelta(-1);

  QCOMPARE(m_sel.setSelectedIndexCalls, 1);
  QCOMPARE(m_sel.lastSetIndex, 500 + 3 * gw);
}

void TestWheelEventHandler::deltaListViewMovesSingleItemPerNotch() {
  CollectionConfig listConfig;
  listConfig.viewType = ViewType::List;
  m_collections = {listConfig};
  wire(/*viewIndex=*/0);
  m_scroll.totalItems = 100;
  m_sel.index = 10;
  m_settings.input.mouseWheelRows = 1;

  // wheelSteps = -1 (down): single-step view moves by rowDelta (1), NOT
  // rowDelta * gridWidth.
  m_handler.applySelectionDelta(-1);

  QCOMPARE(m_sel.setSelectedIndexCalls, 1);
  QCOMPARE(m_sel.lastSetIndex, 11);
}

void TestWheelEventHandler::deltaHorizontalClampsMagnitudeToOneColumn() {
  CollectionConfig horizConfig;
  horizConfig.viewType = ViewType::Horizontal;
  m_collections = {horizConfig};
  wire(/*viewIndex=*/0);
  m_scroll.metrics.itemsPerRow = 8; // items-per-column in horizontal view
  m_scroll.totalItems = 200;
  m_sel.index = 80;

  // A fast/trackpad burst (wheelSteps = 5) must move exactly one column, not
  // five: rowDelta forced to -1, selectionDelta = -1 * 8.
  m_handler.applySelectionDelta(5);

  QCOMPARE(m_sel.setSelectedIndexCalls, 1);
  QCOMPARE(m_sel.lastSetIndex, 80 - 8);
}

void TestWheelEventHandler::deltaWrapEnabledWrapsForwardAtUpperBoundary() {
  wire(/*viewIndex=*/-1);
  m_settings.input.wrapNavigation = true;
  m_scroll.totalItems = 50;

  const CollectionConfig defaultConfig;
  const int gw = gridWidthFor(defaultConfig); // 4
  // currentSelection + gw >= totalItems triggers the forward-wrap modulo.
  m_sel.index = 50 - gw + 2; // 48: 48 + 4 = 52 -> wraps to 2

  const bool wrapped = m_handler.applySelectionDelta(-1); // one row down

  QVERIFY(wrapped);
  QCOMPARE(m_sel.setSelectedIndexCalls, 1);
  QCOMPARE(m_sel.lastSetIndex, (50 - gw + 2 + gw) % 50);
}

void TestWheelEventHandler::deltaWrapDisabledClampsToValidRange() {
  wire(/*viewIndex=*/-1);
  m_settings.input.wrapNavigation = false;
  m_settings.input.mouseWheelRows = 100; // large downward jump
  m_scroll.totalItems = 30;
  m_sel.index = 2;

  // raw newSelection far exceeds totalItems-1; qBound clamps to 29.
  const bool wrapped = m_handler.applySelectionDelta(-1);

  QVERIFY(!wrapped);
  QCOMPARE(m_sel.setSelectedIndexCalls, 1);
  QCOMPARE(m_sel.lastSetIndex, 29);
}

void TestWheelEventHandler::deltaNoMovementReturnsWrapFlagOnly() {
  wire(/*viewIndex=*/-1);
  m_settings.input.wrapNavigation = false;
  m_settings.input.mouseWheelRows = 100;
  m_scroll.totalItems = 30;
  m_sel.index = 29; // already pinned at the bottom; clamp keeps it at 29

  // newSelection == currentSelection -> early-out before setSelectedIndex.
  const bool wrapped = m_handler.applySelectionDelta(-1);

  QVERIFY(!wrapped);
  QCOMPARE(m_sel.setSelectedIndexCalls, 0);
}

void TestWheelEventHandler::deltaZeroTotalItemsBailsSafely() {
  wire(/*viewIndex=*/-1);
  m_scroll.totalItems = 0;
  m_sel.index = 0;

  // No divide/modulo by zero; bails at the totalItems<=0 guard.
  const bool wrapped = m_handler.applySelectionDelta(1);

  QVERIFY(!wrapped);
  QCOMPARE(m_sel.setSelectedIndexCalls, 0);
}

void TestWheelEventHandler::deltaNoScrollManagerBails() {
  // Wire everything EXCEPT the scroll roles.
  m_viewIndex = -1;
  m_ctx.managers.selectionManager = &m_sel;
  m_ctx.managers.interactionState = &m_state;
  WheelEventHandler::Setup setup;
  setup.ctx = &m_ctx;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &m_viewIndex;
  setup.generalSettings = &m_settings;
  m_handler.setupReferences(setup);
  m_sel.index = 5;

  const bool wrapped = m_handler.applySelectionDelta(1);

  QVERIFY(!wrapped);
  QCOMPARE(m_sel.setSelectedIndexCalls, 0);
}

void TestWheelEventHandler::onAnimationFinishedClearsFlagsAndEmitsScrollEnded() {
  wire(/*viewIndex=*/-1);
  // currentSelectedIndex = -1 skips the index-gated viewport/scroll calls; the
  // null mouse/viewport managers keep their branches deterministic.
  m_sel.index = -1;
  m_state.scroll().userScrollActive = true;
  m_state.scroll().programmaticScroll = true;

  QSignalSpy spy(&m_handler, &WheelEventHandler::scrollEnded);
  m_handler.onAnimationFinished();

  QCOMPARE(spy.count(), 1);
  QVERIFY(!m_state.scroll().userScrollActive);
  QVERIFY(!m_state.scroll().programmaticScroll);
}

QTEST_MAIN(TestWheelEventHandler)
#include "test_wheeleventhandler.moc"
