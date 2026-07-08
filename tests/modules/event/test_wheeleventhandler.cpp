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
#include <QApplication>
#include <QScrollArea>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QWheelEvent>
#include <QWidget>

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
#include "idetailspane.h"
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

/// Fake IDetailsPane whose asWidget() hands back a caller-owned QWidget, so
/// eventBelongsToSidebar()'s visibility short-circuit can run without a real
/// sidebar (Kartend audit 8ipd4). The handler reads the pane straight off
/// ctx->ui.sidebar (Kartend-dl0uz.2), so no IDetailsPaneManager double is
/// needed.
class FakeDetailsPane : public IDetailsPane {
public:
  explicit FakeDetailsPane(QWidget *widget) : m_widget(widget) {}
  void clearMetadata() override {}
  [[nodiscard]] QList<DetailsPaneGalleryEntry> currentGalleryEntries() const override { return {}; }
  [[nodiscard]] QWidget *asWidget() override { return m_widget; }
  [[nodiscard]] const QWidget *asWidget() const override { return m_widget; }

private:
  QWidget *m_widget;
};

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

  void modalWidgetActiveBailsBeforeSelectionMoves();
  void eventBelongsToSidebarBailsOnNullAndHiddenPane();

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
  m_ctx.managers.seedSelectionRoles(&m_sel);
  m_ctx.managers.interactionState = &m_state;
  // Viewport / mouse / animation roles intentionally left null: the
  // handler's null-guards skip them, keeping the delta-math deterministic.

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
  m_ctx.managers.seedSelectionRoles(&m_sel);
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

// ─── handleEvent gates (Kartend audit 8ipd4) ───────────────────────────────────

void TestWheelEventHandler::modalWidgetActiveBailsBeforeSelectionMoves() {
  // handleEvent's modal-widget gate swallows wheel input while any application-
  // modal widget is up, so a modal dialog's scroll never leaks into grid
  // selection. Wire the full proceed path (canProceed passes) so the no-modal
  // control case observably moves selection and the modal case does not.
  QScrollArea scrollArea;
  QStackedWidget stack;
  auto *itemsPage = new QWidget; // owned by the stack after addWidget()
  stack.addWidget(itemsPage);
  stack.setCurrentWidget(itemsPage);

  m_viewIndex = -1; // synthetic home view: interactive, no backing collection
  m_ctx.managers.seedScrollRoles(&m_scroll);
  m_ctx.managers.seedSelectionRoles(&m_sel);
  m_ctx.managers.interactionState = &m_state;
  WheelEventHandler::Setup setup;
  setup.ctx = &m_ctx;
  setup.itemScrollArea = &scrollArea;
  setup.stackedWidget = &stack;
  setup.itemsPage = itemsPage;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &m_viewIndex;
  setup.generalSettings = &m_settings;
  m_handler.setupReferences(setup);
  m_scroll.totalItems = 100;
  m_sel.index = 20;

  auto makeWheel = [] {
    return QWheelEvent(QPointF(0, 0), QPointF(0, 0), QPoint(), QPoint(0, 120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
  };

  // Control: no modal -> proceeds past the gate and moves the selection.
  QWheelEvent control = makeWheel();
  const bool controlConsumed = m_handler.handleEvent(&scrollArea, &control);
  QVERIFY(controlConsumed);
  QCOMPARE(m_sel.setSelectedIndexCalls, 1);

  // An application-modal widget up -> handleEvent bails at the modal gate and
  // never reaches applySelectionDelta.
  QWidget modal;
  modal.setWindowModality(Qt::ApplicationModal);
  modal.show();
  QVERIFY(QApplication::activeModalWidget() == &modal);

  QWheelEvent blocked = makeWheel();
  QVERIFY(!m_handler.handleEvent(&scrollArea, &blocked));
  QCOMPARE(m_sel.setSelectedIndexCalls, 1); // unchanged: the modal swallowed it
}

void TestWheelEventHandler::eventBelongsToSidebarBailsOnNullAndHiddenPane() {
  // The sidebar wheel guard's null/visibility branches — everything before the
  // QCursor::pos() hit-test, which stays manual — all return false.
  wire(/*viewIndex=*/-1);

  // (b1) no sidebar pane wired into ctx->ui at all.
  QVERIFY(m_ctx.ui.sidebar == nullptr);
  QVERIFY(!m_handler.eventBelongsToSidebar());

  // (b2) pane present but its asWidget() is null.
  FakeDetailsPane widgetlessPane(nullptr);
  m_ctx.ui.sidebar = &widgetlessPane;
  QVERIFY(!m_handler.eventBelongsToSidebar());

  // (b3) sidebar pane present but its widget is hidden -> isVisible() short-
  // circuits before the cursor hit-test.
  QWidget hiddenSidebar;
  hiddenSidebar.hide();
  FakeDetailsPane pane(&hiddenSidebar);
  m_ctx.ui.sidebar = &pane;
  QVERIFY(!hiddenSidebar.isVisible());
  QVERIFY(!m_handler.eventBelongsToSidebar());
}

QTEST_MAIN(TestWheelEventHandler)
#include "test_wheeleventhandler.moc"
