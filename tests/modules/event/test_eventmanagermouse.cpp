// Unit coverage for EventManager's mouse-dispatch core (Kartend audit 4yktu):
// handleMouseDoubleClick, the right/middle-click branches of handleMousePress,
// the handleWheelEvent modal + foreign-window gates, the shared
// modalInputGateActive() predicate (consumed by InteractionManager's
// gamepad-driven slots), and the itemWidgetForObject / visualIndexForWidget
// helpers. Most are signal-emission or QObject-parent-chain logic reachable
// with real (un-laid-out) ItemWidgets + stubs — no widget geometry.
//
// The LEFT-click selection path (Kartend audit 8ipd4) is covered here too: a
// real grid tree (gridContainer > virtual container > geometry-set ItemWidget
// tiles, mirroring test_mousemanager) under QT_QPA_PLATFORM=offscreen drives
// findBestWidgetForClick deterministically — the click coordinates come from the
// synthetic QMouseEvent, not QCursor::pos(), so the widgetClicked /
// clearSelection / items-top-bar-collision outcomes are headless-stable. Only
// the eventBelongsToSidebar cursor hit-test (QCursor::pos()) stays manual.
//
// EventManager's handle* methods are private; this test is a declared friend
// (see eventmanager.h) so it drives them directly, mirroring the friend-access
// pattern test_mousemanager uses.
#include <QApplication>
#include <QDialog>
#include <QMouseEvent>
#include <QPointF>
#include <QScrollArea>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QWidget>

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "eventmanager.h"
#include "fakescrollmanager.h"
#include "itemwidget.h"
#include "mockdatabasemanager.h"
#include "stubselectionmanager.h"

using KartendTest::FakeScrollManager;
using KartendTest::MockDatabaseManager;
using KartendTest::StubSelectionManager;

namespace {

/// MockDatabaseManager subclass that resolves one path to a fixed collection
/// index, exercising the db-collection-index branch of handleMouseDoubleClick.
class FixedIndexDatabaseManager : public MockDatabaseManager {
public:
  int collectionIndex = -1;
  [[nodiscard]] int getCollectionIndexForFile(const QString &) const override {
    return collectionIndex;
  }
};

/// Build a left-button double-click QMouseEvent at the origin (position is not
/// consulted on the double-click / right / middle branches).
QMouseEvent makeMouseEvent(QEvent::Type type, Qt::MouseButton button,
                           Qt::KeyboardModifiers mods = Qt::NoModifier) {
  return QMouseEvent(type, QPointF(0, 0), QPointF(0, 0), button, button, mods);
}

} // namespace

class TestEventManagerMouse : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();

  void doubleClickOnMediaItemEmitsWidgetDoubleClicked();
  void doubleClickUsesDatabaseCollectionIndexWhenAvailable();
  void doubleClickOnSubcollectionPassesThrough();
  void doubleClickEmptyPathAcceptsButEmitsNothing();
  void doubleClickNonLeftButtonIgnored();
  void rightClickOnItemEmitsContextMenuRequested();
  void middleClickWithCycleModifierEmitsArtworkCycle();
  void middleClickWithoutModifierEmitsMediaPreview();
  void mousePressDuringSelectionRestoreIsAcceptedNoOp();

  void leftClickInsideTileEmitsWidgetClicked();
  void leftClickOnEmptyAreaEmitsClearSelection();
  void leftClickInCoverFlowNeverRunsTheGridClickPath();
  void leftClickOnItemsTopBarIsRejected();

  void handleWheelEventBlockedWhileModalScrapeDialogVisible();
  void handleWheelEventRejectsTargetInForeignWindow();
  void modalInputGateCombinesModalWidgetAndScrapePredicate();
  void itemWidgetForObjectWalksParentChain();
  void visualIndexForWidgetReturnsScrollDataLookup();

private:
  /// Wire the EventManager against a one-collection list at viewIndex 0. UI
  /// pointers (scroll area / grid container / stack / items page) stay null —
  /// the right / middle / double-click branches return before the left-click
  /// geometry block reads them.
  void wire();
  /// Wire with a real items UI tree so the left-click geometry path runs:
  /// itemScrollArea / gridContainer / stackedWidget / itemsPage (and optionally
  /// itemsTopBar) are all set, with the stack's current page = itemsPage.
  void wireWithUi(QScrollArea *scrollArea, QWidget *gridContainer, QStackedWidget *stack,
                  QWidget *itemsPage, QWidget *itemsTopBar = nullptr);

  GeneralSettings m_settings;
  ApplicationContext m_ctx;
  FakeScrollManager m_scroll;
  StubSelectionManager m_sel;
  QList<CollectionConfig> m_collections;
  int m_viewIndex = 0;
  EventManager m_mgr;
};

void TestEventManagerMouse::init() {
  // FakeScrollManager is a QObject (copy/move-disabled): reset fields in place.
  m_settings = GeneralSettings{};
  m_ctx = ApplicationContext{};
  m_scroll.totalItems = 0;
  m_scroll.activeWidgets.clear();
  m_scroll.metrics = GridMetrics{};
  m_scroll.subcollectionCount = 0;
  m_scroll.virtualFolderCount = 0;
  m_scroll.filteredIndexOverride = -1;
  m_sel = StubSelectionManager{};
  m_collections = {CollectionConfig{}};
  m_viewIndex = 0;
}

void TestEventManagerMouse::cleanup() {}

void TestEventManagerMouse::wire() {
  m_ctx.managers.seedScrollRoles(&m_scroll);
  m_ctx.managers.seedSelectionRoles(&m_sel);

  EventManagerSetup setup;
  setup.ctx = &m_ctx;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &m_viewIndex;
  setup.generalSettings = &m_settings;
  m_mgr.setupReferences(setup);
}

void TestEventManagerMouse::wireWithUi(QScrollArea *scrollArea, QWidget *gridContainer,
                                       QStackedWidget *stack, QWidget *itemsPage,
                                       QWidget *itemsTopBar) {
  m_ctx.managers.seedScrollRoles(&m_scroll);
  m_ctx.managers.seedSelectionRoles(&m_sel);

  EventManagerSetup setup;
  setup.ctx = &m_ctx;
  setup.itemScrollArea = scrollArea;
  setup.gridContainer = gridContainer;
  setup.stackedWidget = stack;
  setup.itemsPage = itemsPage;
  setup.itemsTopBar = itemsTopBar;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &m_viewIndex;
  setup.generalSettings = &m_settings;
  m_mgr.setupReferences(setup);
}

// ─── double-click ────────────────────────────────────────────────────────────

void TestEventManagerMouse::doubleClickOnMediaItemEmitsWidgetDoubleClicked() {
  wire();
  ItemWidget widget;
  widget.setFilePath(QStringLiteral("video.mp4"));
  m_scroll.activeWidgets = {{0, &widget}};
  m_scroll.subcollectionCount = 0;
  m_scroll.virtualFolderCount = 0;

  QSignalSpy spy(&m_mgr, &EventManager::widgetDoubleClicked);
  auto evt = makeMouseEvent(QEvent::MouseButtonDblClick, Qt::LeftButton);
  const bool consumed = m_mgr.handleMouseDoubleClick(&widget, &evt);

  QVERIFY(consumed);
  QVERIFY(evt.isAccepted());
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("video.mp4"));
  // No databaseManager wired -> falls back to *m_currentCollectionIndex == 0.
  QCOMPARE(spy.at(0).at(1).toInt(), 0);
}

void TestEventManagerMouse::doubleClickUsesDatabaseCollectionIndexWhenAvailable() {
  FixedIndexDatabaseManager db;
  db.collectionIndex = 7;
  m_ctx.managers.seedDatabaseRoles(&db);
  wire();
  ItemWidget widget;
  widget.setFilePath(QStringLiteral("video.mp4"));
  m_scroll.activeWidgets = {{0, &widget}};

  QSignalSpy spy(&m_mgr, &EventManager::widgetDoubleClicked);
  auto evt = makeMouseEvent(QEvent::MouseButtonDblClick, Qt::LeftButton);
  const bool consumed = m_mgr.handleMouseDoubleClick(&widget, &evt);

  QVERIFY(consumed);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(1).toInt(), 7); // db path wins over current index
}

void TestEventManagerMouse::doubleClickOnSubcollectionPassesThrough() {
  wire();
  ItemWidget widget;
  widget.setFilePath(QStringLiteral("sub"));
  m_scroll.activeWidgets = {{0, &widget}};
  m_scroll.subcollectionCount = 1; // visual index 0 -> a subcollection
  m_scroll.virtualFolderCount = 0;

  QSignalSpy spy(&m_mgr, &EventManager::widgetDoubleClicked);
  auto evt = makeMouseEvent(QEvent::MouseButtonDblClick, Qt::LeftButton);
  const bool consumed = m_mgr.handleMouseDoubleClick(&widget, &evt);

  QVERIFY(!consumed); // event not consumed; default handling allowed
  QCOMPARE(spy.count(), 0);
}

void TestEventManagerMouse::doubleClickEmptyPathAcceptsButEmitsNothing() {
  wire();
  ItemWidget widget; // empty file path
  m_scroll.activeWidgets = {{0, &widget}};
  m_scroll.subcollectionCount = 0;
  m_scroll.virtualFolderCount = 0;

  QSignalSpy spy(&m_mgr, &EventManager::widgetDoubleClicked);
  auto evt = makeMouseEvent(QEvent::MouseButtonDblClick, Qt::LeftButton);
  const bool consumed = m_mgr.handleMouseDoubleClick(&widget, &evt);

  QVERIFY(consumed);
  QVERIFY(evt.isAccepted());
  QCOMPARE(spy.count(), 0);
}

void TestEventManagerMouse::doubleClickNonLeftButtonIgnored() {
  wire();
  ItemWidget widget;
  widget.setFilePath(QStringLiteral("video.mp4"));
  m_scroll.activeWidgets = {{0, &widget}};

  QSignalSpy spy(&m_mgr, &EventManager::widgetDoubleClicked);
  auto evt = makeMouseEvent(QEvent::MouseButtonDblClick, Qt::RightButton);
  const bool consumed = m_mgr.handleMouseDoubleClick(&widget, &evt);

  QVERIFY(!consumed);
  QCOMPARE(spy.count(), 0);
}

// ─── right / middle click ────────────────────────────────────────────────────

void TestEventManagerMouse::rightClickOnItemEmitsContextMenuRequested() {
  wire();
  ItemWidget widget;
  m_scroll.activeWidgets = {{3, &widget}}; // indexForWidget(widget) -> 3

  QSignalSpy spy(&m_mgr, &EventManager::contextMenuRequested);
  auto evt = makeMouseEvent(QEvent::MouseButtonPress, Qt::RightButton);
  const bool consumed = m_mgr.handleMousePress(&widget, &evt);

  QVERIFY(consumed);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(1).toInt(), 3); // visualIndex
}

void TestEventManagerMouse::middleClickWithCycleModifierEmitsArtworkCycle() {
  m_settings.input.artworkCycleModifier = static_cast<int>(Qt::ShiftModifier);
  wire();
  ItemWidget widget;
  m_scroll.activeWidgets = {{2, &widget}};

  QSignalSpy cycle(&m_mgr, &EventManager::artworkTypeCycleRequested);
  QSignalSpy preview(&m_mgr, &EventManager::mediaPreviewRequested);
  auto evt = makeMouseEvent(QEvent::MouseButtonPress, Qt::MiddleButton, Qt::ShiftModifier);
  const bool consumed = m_mgr.handleMousePress(&widget, &evt);

  QVERIFY(consumed);
  QCOMPARE(cycle.count(), 1);
  QCOMPARE(cycle.at(0).at(1).toInt(), 2);
  QCOMPARE(preview.count(), 0);
}

void TestEventManagerMouse::middleClickWithoutModifierEmitsMediaPreview() {
  m_settings.input.artworkCycleModifier = static_cast<int>(Qt::ShiftModifier);
  wire();
  ItemWidget widget;
  m_scroll.activeWidgets = {{2, &widget}};

  QSignalSpy cycle(&m_mgr, &EventManager::artworkTypeCycleRequested);
  QSignalSpy preview(&m_mgr, &EventManager::mediaPreviewRequested);
  auto evt = makeMouseEvent(QEvent::MouseButtonPress, Qt::MiddleButton, Qt::NoModifier);
  const bool consumed = m_mgr.handleMousePress(&widget, &evt);

  QVERIFY(consumed);
  QCOMPARE(preview.count(), 1);
  QCOMPARE(cycle.count(), 0);
}

void TestEventManagerMouse::mousePressDuringSelectionRestoreIsAcceptedNoOp() {
  m_sel.restoring = true;
  wire();
  ItemWidget widget;
  m_scroll.activeWidgets = {{0, &widget}};

  QSignalSpy ctx(&m_mgr, &EventManager::contextMenuRequested);
  QSignalSpy clearSel(&m_mgr, &EventManager::clearSelectionRequested);
  auto evt = makeMouseEvent(QEvent::MouseButtonPress, Qt::RightButton);
  const bool consumed = m_mgr.handleMousePress(&widget, &evt);

  QVERIFY(consumed);
  QVERIFY(evt.isAccepted());
  QCOMPARE(ctx.count(), 0);
  QCOMPARE(clearSel.count(), 0);
}

// ─── wheel gates ─────────────────────────────────────────────────────────────

void TestEventManagerMouse::handleWheelEventBlockedWhileModalScrapeDialogVisible() {
  wire();
  m_mgr.setModalScrapeDialogVisiblePredicate([] { return true; });
  QWidget target;

  QWheelEvent wheel(QPointF(0, 0), QPointF(0, 0), QPoint(), QPoint(0, 120), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  const bool consumed = m_mgr.handleWheelEvent(&target, &wheel);

  QVERIFY(!consumed); // bails before consulting the wheel handler
}

void TestEventManagerMouse::handleWheelEventRejectsTargetInForeignWindow() {
  // m_itemsPage lives in window A; the wheel target lives in a separate
  // top-level window B. Window identity (not pixel geometry) drives the reject.
  QWidget windowA;
  EventManagerSetup setup;
  setup.ctx = &m_ctx;
  setup.itemsPage = &windowA;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &m_viewIndex;
  setup.generalSettings = &m_settings;
  m_ctx.managers.seedScrollRoles(&m_scroll);
  m_ctx.managers.seedSelectionRoles(&m_sel);
  m_mgr.setupReferences(setup);

  QWidget windowB; // distinct top-level window
  QWheelEvent wheel(QPointF(0, 0), QPointF(0, 0), QPoint(), QPoint(0, 120), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  const bool consumed = m_mgr.handleWheelEvent(&windowB, &wheel);

  QVERIFY(!consumed);
}

// ─── shared modal-input gate ─────────────────────────────────────────────────

void TestEventManagerMouse::modalInputGateCombinesModalWidgetAndScrapePredicate() {
  wire();

  // m_mgr is shared across slots and an earlier slot injects a true-returning
  // scrape predicate -- start from a known-inactive gate.
  m_mgr.setModalScrapeDialogVisiblePredicate([] { return false; });
  QVERIFY(!m_mgr.modalInputGateActive());

  // The injected scrape-dialog predicate alone activates the gate.
  m_mgr.setModalScrapeDialogVisiblePredicate([] { return true; });
  QVERIFY(m_mgr.modalInputGateActive());
  m_mgr.setModalScrapeDialogVisiblePredicate([] { return false; });
  QVERIFY(!m_mgr.modalInputGateActive());

  // An application-modal widget alone activates the gate. The modal stack is
  // QApplication-internal bookkeeping, so this holds offscreen too.
  QDialog dialog;
  dialog.setModal(true);
  dialog.show();
  QVERIFY(QApplication::activeModalWidget());
  QVERIFY(m_mgr.modalInputGateActive());
  dialog.hide();
  QVERIFY(!QApplication::activeModalWidget());
  QVERIFY(!m_mgr.modalInputGateActive());
}

// ─── helpers ─────────────────────────────────────────────────────────────────

void TestEventManagerMouse::itemWidgetForObjectWalksParentChain() {
  wire();
  ItemWidget item;
  auto *childFrame = new QWidget(&item);
  auto *grandchild = new QWidget(childFrame);

  QCOMPARE(m_mgr.itemWidgetForObject(grandchild), &item);
  QCOMPARE(m_mgr.itemWidgetForObject(&item), &item);
  QWidget orphan;
  QCOMPARE(m_mgr.itemWidgetForObject(&orphan), nullptr);
}

void TestEventManagerMouse::visualIndexForWidgetReturnsScrollDataLookup() {
  wire();
  ItemWidget widget;
  m_scroll.activeWidgets = {{7, &widget}};

  QCOMPARE(m_mgr.visualIndexForWidget(&widget), 7);
  ItemWidget unknown;
  QCOMPARE(m_mgr.visualIndexForWidget(&unknown), -1);
  QCOMPARE(m_mgr.visualIndexForWidget(nullptr), -1);
}

// ─── left-click selection (geometry, Kartend audit 8ipd4) ──────────────────────

void TestEventManagerMouse::leftClickInsideTileEmitsWidgetClicked() {
  // Real grid tree mirroring test_mousemanager's find* setup: gridContainer >
  // virtual container (offset 5,5) > 100x100 tiles. A left-click at (60,60) in
  // gridContainer coords maps to (55,55) inside tile0 -> widgetClicked(index 0).
  QStackedWidget stack;
  auto *itemsPage = new QWidget; // owned by the stack after addWidget()
  stack.addWidget(itemsPage);
  stack.setCurrentWidget(itemsPage);
  auto *gridContainer = new QWidget(itemsPage);
  gridContainer->resize(400, 400);
  auto *virtualContainer = new QWidget(gridContainer);
  virtualContainer->move(5, 5);
  virtualContainer->resize(300, 300);
  auto *tile0 = new ItemWidget(virtualContainer);
  tile0->setGeometry(0, 0, 100, 100);
  QScrollArea scrollArea;
  stack.show(); // offscreen QPA: realize the page so the tile is isVisible()

  m_scroll.activeWidgets = {{0, tile0}};
  m_scroll.totalItems = 1;
  m_scroll.metrics.itemWidth = 100;
  m_scroll.metrics.itemHeight = 100;
  m_scroll.metrics.horizontalSpacing = 10;
  m_scroll.metrics.verticalSpacing = 10;
  m_scroll.metrics.itemsPerRow = 2;
  wireWithUi(&scrollArea, gridContainer, &stack, itemsPage);

  QSignalSpy clicked(&m_mgr, &EventManager::widgetClicked);
  QSignalSpy clearSel(&m_mgr, &EventManager::clearSelectionRequested);
  // obj == gridContainer, so handleMousePress consults pos() directly (no remap).
  QMouseEvent evt(QEvent::MouseButtonPress, QPointF(60, 60), QPointF(60, 60), Qt::LeftButton,
                  Qt::LeftButton, Qt::NoModifier);
  const bool consumed = m_mgr.handleMousePress(gridContainer, &evt);

  QVERIFY(consumed);
  QVERIFY(evt.isAccepted());
  QCOMPARE(clicked.count(), 1);
  QCOMPARE(clicked.at(0).at(1).toInt(), 0); // visualIndex
  QCOMPARE(clearSel.count(), 0);
}

void TestEventManagerMouse::leftClickOnEmptyAreaEmitsClearSelection() {
  // Same UI tree but no active widgets: findBestWidgetForClick returns
  // {nullptr,-1}, so a left-click resolves to clearSelectionRequested.
  QStackedWidget stack;
  auto *itemsPage = new QWidget;
  stack.addWidget(itemsPage);
  stack.setCurrentWidget(itemsPage);
  auto *gridContainer = new QWidget(itemsPage);
  gridContainer->resize(400, 400);
  QScrollArea scrollArea;
  stack.show();

  m_scroll.activeWidgets.clear();
  m_scroll.totalItems = 0;
  wireWithUi(&scrollArea, gridContainer, &stack, itemsPage);

  QSignalSpy clicked(&m_mgr, &EventManager::widgetClicked);
  QSignalSpy clearSel(&m_mgr, &EventManager::clearSelectionRequested);
  QMouseEvent evt(QEvent::MouseButtonPress, QPointF(60, 60), QPointF(60, 60), Qt::LeftButton,
                  Qt::LeftButton, Qt::NoModifier);
  const bool consumed = m_mgr.handleMousePress(gridContainer, &evt);

  QVERIFY(consumed);
  QVERIFY(evt.isAccepted());
  QCOMPARE(clearSel.count(), 1);
  QCOMPARE(clicked.count(), 0);
}

void TestEventManagerMouse::leftClickInCoverFlowNeverRunsTheGridClickPath() {
  // Kartend-g7hbx: with cover flow active the grid is HIDDEN and
  // CoverFlowWidget owns all click hit-testing. The grid click path used to
  // run anyway for clicks that fell through to the items page: it mapped the
  // position into the hidden gridContainer, found no widget, and emitted
  // clearSelectionRequested — clearing the canonical selection and snapping
  // the carousel to item 0. handleMousePress must decline left-clicks
  // entirely in cover flow, clear nothing, and leave the event to Qt.
  QStackedWidget stack;
  auto *itemsPage = new QWidget;
  stack.addWidget(itemsPage);
  stack.setCurrentWidget(itemsPage);
  auto *gridContainer = new QWidget(itemsPage);
  gridContainer->resize(400, 400);
  QScrollArea scrollArea;
  stack.show();

  m_collections[0].viewType = ViewType::CoverFlow;
  m_scroll.activeWidgets.clear();
  m_scroll.totalItems = 0;
  wireWithUi(&scrollArea, gridContainer, &stack, itemsPage);

  QSignalSpy clicked(&m_mgr, &EventManager::widgetClicked);
  QSignalSpy clearSel(&m_mgr, &EventManager::clearSelectionRequested);
  QMouseEvent evt(QEvent::MouseButtonPress, QPointF(60, 60), QPointF(60, 60), Qt::LeftButton,
                  Qt::LeftButton, Qt::NoModifier);
  const bool consumed = m_mgr.handleMousePress(gridContainer, &evt);

  QVERIFY(!consumed);
  QCOMPARE(clearSel.count(), 0);
  QCOMPARE(clicked.count(), 0);
}

void TestEventManagerMouse::leftClickOnItemsTopBarIsRejected() {
  // A visible items top bar swallows left-clicks two ways: (a) the clicked object
  // is the toolbar or a descendant (parent-chain walk), and (b) the click's
  // global position falls within the toolbar's global geometry. Both return false
  // (let Qt handle the toolbar) and emit neither widgetClicked nor
  // clearSelectionRequested.
  QStackedWidget stack;
  auto *itemsPage = new QWidget;
  stack.addWidget(itemsPage);
  stack.setCurrentWidget(itemsPage);
  auto *gridContainer = new QWidget(itemsPage);
  gridContainer->resize(400, 400);
  auto *itemsTopBar = new QWidget(itemsPage);
  itemsTopBar->setGeometry(0, 0, 400, 40);
  auto *toolbarChild = new QWidget(itemsTopBar);
  QScrollArea scrollArea;
  stack.show(); // realize so itemsTopBar->isVisible() and mapToGlobal are valid
  QVERIFY(itemsTopBar->isVisible());

  m_scroll.activeWidgets.clear();
  wireWithUi(&scrollArea, gridContainer, &stack, itemsPage, itemsTopBar);

  QSignalSpy clicked(&m_mgr, &EventManager::widgetClicked);
  QSignalSpy clearSel(&m_mgr, &EventManager::clearSelectionRequested);

  // (a) clicked object is inside the toolbar -> parent-chain walk rejects.
  QMouseEvent childEvt(QEvent::MouseButtonPress, QPointF(0, 0), QPointF(0, 0), Qt::LeftButton,
                       Qt::LeftButton, Qt::NoModifier);
  QVERIFY(!m_mgr.handleMousePress(toolbarChild, &childEvt));

  // (b) object is outside the toolbar but the click's global pos is over it.
  const QPoint overToolbar = itemsPage->mapToGlobal(itemsTopBar->geometry().center());
  QMouseEvent posEvt(QEvent::MouseButtonPress, QPointF(0, 0), QPointF(overToolbar), Qt::LeftButton,
                     Qt::LeftButton, Qt::NoModifier);
  QVERIFY(!m_mgr.handleMousePress(gridContainer, &posEvt));

  QCOMPARE(clicked.count(), 0);
  QCOMPARE(clearSel.count(), 0);
}

QTEST_MAIN(TestEventManagerMouse)
#include "test_eventmanagermouse.moc"
