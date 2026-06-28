// Unit coverage for HoverScrollHandler's DETERMINISTIC surface only (Kartend
// audit 4yktu). This handler's substance — the dwell -> commit -> continuous-
// scroll cycle — is dominated by QCursor::pos(), widget->mapFromGlobal() /
// rect().contains(), QApplication::widgetAt() and viewport()->rect(), all of
// which are flaky or inert headless and so are listed for manual verification.
// What IS deterministic, and all this test asserts, is: the handleEvent early-
// guard gates, the button-held-during-MouseMove skip, the non-ItemWidget keep-
// dwell branch, clearPendingScroll's state-reset contract, and the destroyed-
// widget commit guard. These lock in the guard ordering + state-flag contract
// without exercising the actual hover-scroll behavior.
//
// commitPendingScroll is a private slot and the pending-dwell members are
// private; this test is a declared friend (see hoverscrollhandler.h) so it can
// stage / drive them directly without waiting on the real dwell timer.
#include <QMouseEvent>
#include <QPointF>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTest>
#include <QWidget>

#include <memory>

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "fakescrollmanager.h"
#include "hoverscrollhandler.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "stubselectionmanager.h"

using KartendTest::FakeScrollManager;
using KartendTest::StubSelectionManager;

class TestHoverScrollHandler : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();

  void handleEventDisabledSettingClearsAndReturnsFalse();
  void handleEventWhileRestoringSelectionIsNoOp();
  void handleEventMissingItemsPageOrWrongStackPageBails();
  void handleEventMissingScrollDataOrSelectionMgrBails();
  void handleEventNonInteractiveCollectionIndexBails();
  void handleEventMouseMoveWithButtonHeldReturnsFalseNoStaging();
  void handleEventNonItemWidgetObjectKeepsInFlightDwell();
  void clearPendingScrollResetsStateAndFlag();
  void commitPendingScrollWithDestroyedWidgetBailsCleanly();

  void mouseMoveStagesDwellAnchorAndSelectsHoveredItem();
  void mouseMoveWithinRadiusKeepsDwellBeyondReArms();
  void pollCursorForContinueShortCircuitsBeforeCursorReads();

private:
  /// Wire the handler with all guards satisfiable: real (un-laid-out)
  /// QScrollArea + grid container + a stacked widget whose current page is the
  /// items page, scroll roles + selection manager seeded, a Grid home view
  /// (-1 sentinel). Individual tests null specific pieces to hit a guard.
  void wireAllGuardsPass();

  /// Stage pending dwell state so a follow-up clear / bail can be observed.
  void stagePending(int index);

  GeneralSettings m_settings;
  ApplicationContext m_ctx;
  FakeScrollManager m_scroll;
  StubSelectionManager m_sel;
  InteractionStateHolder m_state;
  QList<CollectionConfig> m_collections;
  int m_viewIndex = -1;

  std::unique_ptr<QScrollArea> m_scrollArea;
  std::unique_ptr<QWidget> m_gridContainer;
  std::unique_ptr<QStackedWidget> m_stack;
  QWidget *m_itemsPage = nullptr; // owned by m_stack
  std::unique_ptr<HoverScrollHandler> m_handler;
};

void TestHoverScrollHandler::init() {
  m_settings = GeneralSettings{};
  m_settings.input.selectItemOnHover = true;
  m_ctx = ApplicationContext{};
  // FakeScrollManager is a QObject (copy/move-disabled): reset fields in place.
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

  m_scrollArea = std::make_unique<QScrollArea>();
  m_gridContainer = std::make_unique<QWidget>();
  m_stack = std::make_unique<QStackedWidget>();
  m_itemsPage = new QWidget;
  m_stack->addWidget(m_itemsPage);
  m_stack->setCurrentWidget(m_itemsPage);

  m_handler = std::make_unique<HoverScrollHandler>();
}

void TestHoverScrollHandler::cleanup() {
  m_handler.reset();
  m_stack.reset();
  m_gridContainer.reset();
  m_scrollArea.reset();
}

void TestHoverScrollHandler::wireAllGuardsPass() {
  m_ctx.managers.seedScrollRoles(&m_scroll);
  m_ctx.managers.selectionManager = &m_sel;
  m_ctx.managers.interactionState = &m_state;

  HoverScrollHandler::Setup setup;
  setup.ctx = &m_ctx;
  setup.itemScrollArea = m_scrollArea.get();
  setup.gridContainer = m_gridContainer.get();
  setup.stackedWidget = m_stack.get();
  setup.itemsPage = m_itemsPage;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &m_viewIndex;
  setup.generalSettings = &m_settings;
  m_handler->setupReferences(setup);
}

void TestHoverScrollHandler::stagePending(int index) {
  m_handler->m_pendingIndex = index;
  m_state.scroll().hoverScrollPending = true;
}

// ─── handleEvent early guards ────────────────────────────────────────────────

void TestHoverScrollHandler::handleEventDisabledSettingClearsAndReturnsFalse() {
  wireAllGuardsPass();
  m_settings.input.selectItemOnHover = false;
  stagePending(5);

  const bool consumed = m_handler->handleEvent(nullptr, nullptr, /*isRestoringSelection=*/false);

  QVERIFY(!consumed);
  QCOMPARE(m_handler->m_pendingIndex, -1);
  QVERIFY(!m_state.scroll().hoverScrollPending);
}

void TestHoverScrollHandler::handleEventWhileRestoringSelectionIsNoOp() {
  wireAllGuardsPass();
  stagePending(5);

  const bool consumed = m_handler->handleEvent(nullptr, nullptr, /*isRestoringSelection=*/true);

  QVERIFY(!consumed);
  QCOMPARE(m_handler->m_pendingIndex, -1);
  QVERIFY(!m_state.scroll().hoverScrollPending);
  QCOMPARE(m_sel.selectItemByHoverCalls, 0);
}

void TestHoverScrollHandler::handleEventMissingItemsPageOrWrongStackPageBails() {
  // Stacked widget present but its current page is NOT the items page.
  auto *otherPage = new QWidget;
  m_stack->addWidget(otherPage);
  m_stack->setCurrentWidget(otherPage);
  wireAllGuardsPass();
  stagePending(5);

  const bool consumed = m_handler->handleEvent(nullptr, nullptr, false);

  QVERIFY(!consumed);
  QCOMPARE(m_handler->m_pendingIndex, -1);
  QVERIFY(!m_state.scroll().hoverScrollPending);
}

void TestHoverScrollHandler::handleEventMissingScrollDataOrSelectionMgrBails() {
  // scrollData / selectionManager intentionally NOT seeded.
  m_ctx.managers.interactionState = &m_state;
  HoverScrollHandler::Setup setup;
  setup.ctx = &m_ctx;
  setup.itemScrollArea = m_scrollArea.get();
  setup.gridContainer = m_gridContainer.get();
  setup.stackedWidget = m_stack.get();
  setup.itemsPage = m_itemsPage;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &m_viewIndex;
  setup.generalSettings = &m_settings;
  m_handler->setupReferences(setup);
  stagePending(5);

  const bool consumed = m_handler->handleEvent(nullptr, nullptr, false);

  QVERIFY(!consumed);
  QCOMPARE(m_handler->m_pendingIndex, -1);
  QVERIFY(!m_state.scroll().hoverScrollPending);
}

void TestHoverScrollHandler::handleEventNonInteractiveCollectionIndexBails() {
  // 99 with an empty collection list is out-of-range AND not the -1 sentinel,
  // so isInteractiveViewIndex is false.
  m_viewIndex = 99;
  wireAllGuardsPass();
  stagePending(5);

  const bool consumed = m_handler->handleEvent(nullptr, nullptr, false);

  QVERIFY(!consumed);
  QCOMPARE(m_handler->m_pendingIndex, -1);
  QVERIFY(!m_state.scroll().hoverScrollPending);
}

void TestHoverScrollHandler::handleEventMouseMoveWithButtonHeldReturnsFalseNoStaging() {
  wireAllGuardsPass();
  // A MouseMove with a button held: the globalPos comes from the synthetic
  // event (NOT QCursor), and the handler returns false before staging.
  QMouseEvent move(QEvent::MouseMove, QPointF(10, 10), QPointF(10, 10), Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
  QWidget obj;

  const bool consumed = m_handler->handleEvent(&obj, &move, false);

  QVERIFY(!consumed);
  QCOMPARE(m_handler->m_pendingIndex, -1); // nothing staged
  QCOMPARE(m_sel.selectItemByHoverCalls, 0);
}

void TestHoverScrollHandler::handleEventNonItemWidgetObjectKeepsInFlightDwell() {
  wireAllGuardsPass();
  // A plain (non-ItemWidget) object with no pending widget staged: the
  // !m_pendingWidget branch clears and returns false. Pure QObject typing.
  QWidget obj;

  const bool consumed = m_handler->handleEvent(&obj, nullptr, false);

  QVERIFY(!consumed);
  QCOMPARE(m_handler->m_pendingIndex, -1);
  QVERIFY(!m_state.scroll().hoverScrollPending);
}

// ─── state reset + commit guard ──────────────────────────────────────────────

void TestHoverScrollHandler::clearPendingScrollResetsStateAndFlag() {
  wireAllGuardsPass();
  stagePending(5);
  m_handler->m_dwellTimer.start(10000); // long delay; must be stopped on clear

  m_handler->clearPendingScroll();

  QCOMPARE(m_handler->m_pendingIndex, -1);
  QVERIFY(!m_state.scroll().hoverScrollPending);
  QVERIFY(!m_handler->m_dwellTimer.isActive());
}

void TestHoverScrollHandler::commitPendingScrollWithDestroyedWidgetBailsCleanly() {
  wireAllGuardsPass();
  // Stage a heap ItemWidget as the pending widget, then delete it so the
  // QPointer auto-nulls. commitPendingScroll must early-return at the null/
  // visibility guard and clear pending state, never reaching cursor/geometry.
  auto *heapWidget = new ItemWidget;
  m_handler->m_pendingWidget = heapWidget;
  m_handler->m_pendingIndex = 5;
  m_state.scroll().hoverScrollPending = true;
  delete heapWidget;
  QVERIFY(m_handler->m_pendingWidget.isNull()); // QPointer decayed

  m_handler->commitPendingScroll();

  QCOMPARE(m_handler->m_pendingIndex, -1);
  QVERIFY(!m_state.scroll().hoverScrollPending);
  QCOMPARE(m_sel.selectItemByHoverCalls, 0);
}

// ─── dwell staging + re-arm + poll guards (Kartend audit 8ipd4) ────────────────

void TestHoverScrollHandler::mouseMoveStagesDwellAnchorAndSelectsHoveredItem() {
  // A MouseMove over an ItemWidget the cursor hasn't selected yet immediately
  // hover-selects it AND stages the dwell anchor (widget + index + global pos),
  // arming the dwell timer. The global pos comes from the synthetic event, so
  // this is deterministic; only the dwell *firing* path is manual.
  wireAllGuardsPass();
  auto *widget = new ItemWidget(m_gridContainer.get());
  m_gridContainer->show(); // offscreen: realize the child so isVisible()
  m_scroll.activeWidgets = {{7, widget}};
  m_sel.index = 0; // current selection != 7 -> the hover selects 7

  QMouseEvent move(QEvent::MouseMove, QPointF(100, 100), QPointF(100, 100), Qt::NoButton,
                   Qt::NoButton, Qt::NoModifier);
  (void)m_handler->handleEvent(widget, &move, false);

  QCOMPARE(m_handler->m_pendingIndex, 7);
  QCOMPARE(m_handler->m_pendingWidget.data(), widget);
  QCOMPARE(m_handler->m_pendingGlobalPos, QPoint(100, 100));
  QVERIFY(m_state.scroll().hoverScrollPending);
  QVERIFY(m_handler->m_dwellTimer.isActive());
  QCOMPARE(m_sel.selectItemByHoverCalls, 1);
}

void TestHoverScrollHandler::mouseMoveWithinRadiusKeepsDwellBeyondReArms() {
  // Once a dwell is staged, a tiny jitter inside HOVER_SCROLL_STABILITY_RADIUS_PX
  // (4px) leaves the anchor untouched and does not re-select; a larger move
  // re-arms the anchor to the new position.
  wireAllGuardsPass();
  auto *widget = new ItemWidget(m_gridContainer.get());
  m_gridContainer->show();
  m_scroll.activeWidgets = {{7, widget}};
  m_sel.index = 0; // first move selects + stages

  auto move = [&](int x, int y) {
    QMouseEvent e(QEvent::MouseMove, QPointF(x, y), QPointF(x, y), Qt::NoButton, Qt::NoButton,
                  Qt::NoModifier);
    (void)m_handler->handleEvent(widget, &e, false);
  };

  move(100, 100); // anchor at (100,100), selects 7
  QCOMPARE(m_handler->m_pendingGlobalPos, QPoint(100, 100));
  QCOMPARE(m_sel.selectItemByHoverCalls, 1);
  m_sel.index = 7; // the hover-selection has taken effect

  move(102, 100); // manhattan 2 <= 4 -> anchor unchanged, no re-select
  QCOMPARE(m_handler->m_pendingGlobalPos, QPoint(100, 100));
  QCOMPARE(m_sel.selectItemByHoverCalls, 1);

  move(150, 100); // manhattan 50 > 4 -> anchor re-armed
  QCOMPARE(m_handler->m_pendingGlobalPos, QPoint(150, 100));
  QVERIFY(m_handler->m_dwellTimer.isActive());
}

void TestHoverScrollHandler::pollCursorForContinueShortCircuitsBeforeCursorReads() {
  // pollCursorForContinue's early guards must return before the un-fakeable
  // QCursor::pos()/QApplication::widgetAt() reads, so neither short-circuit
  // re-selects anything.
  wireAllGuardsPass();

  // (a) a hover scroll is already staged -> the m_pendingWidget guard returns.
  auto *widget = new ItemWidget(m_gridContainer.get());
  m_handler->m_pendingWidget = widget;
  m_handler->pollCursorForContinue();
  QCOMPARE(m_sel.selectItemByHoverCalls, 0);

  // (b) the stack's current page is not the items page -> the stack guard returns.
  m_handler->m_pendingWidget.clear();
  auto *otherPage = new QWidget;
  m_stack->addWidget(otherPage);
  m_stack->setCurrentWidget(otherPage);
  m_handler->pollCursorForContinue();
  QCOMPARE(m_sel.selectItemByHoverCalls, 0);
}

QTEST_MAIN(TestHoverScrollHandler)
#include "test_hoverscrollhandler.moc"
