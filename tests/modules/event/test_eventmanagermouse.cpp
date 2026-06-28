// Unit coverage for EventManager's mouse-dispatch core (Kartend audit 4yktu):
// handleMouseDoubleClick, the right/middle-click branches of handleMousePress,
// the handleWheelEvent modal + foreign-window gates, and the
// itemWidgetForObject / visualIndexForWidget helpers. These are signal-emission
// or QObject-parent-chain logic reachable with real (un-laid-out) ItemWidgets +
// stubs — no widget geometry. The LEFT-click selection path
// (findBestWidgetForClick) is geometry-gated and already covered in
// test_mousemanager, so its two signal outcomes stay in the manual tier.
//
// EventManager's handle* methods are private; this test is a declared friend
// (see eventmanager.h) so it drives them directly, mirroring the friend-access
// pattern test_mousemanager uses.
#include <QMouseEvent>
#include <QPointF>
#include <QSignalSpy>
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
  void handleWheelEventBlockedWhileModalScrapeDialogVisible();
  void handleWheelEventRejectsTargetInForeignWindow();
  void itemWidgetForObjectWalksParentChain();
  void visualIndexForWidgetReturnsScrollDataLookup();

private:
  /// Wire the EventManager against a one-collection list at viewIndex 0. UI
  /// pointers (scroll area / grid container / stack / items page) stay null —
  /// the right / middle / double-click branches return before the left-click
  /// geometry block reads them.
  void wire();

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
  m_ctx.managers.selectionManager = &m_sel;

  EventManagerSetup setup;
  setup.ctx = &m_ctx;
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
  m_ctx.managers.databaseManager = &db;
  wire();
  ItemWidget widget;
  widget.setFilePath(QStringLiteral("video.mp4"));
  m_scroll.activeWidgets = {{0, &widget}};

  QSignalSpy spy(&m_mgr, &EventManager::widgetDoubleClicked);
  auto evt = makeMouseEvent(QEvent::MouseButtonDblClick, Qt::LeftButton);
  m_mgr.handleMouseDoubleClick(&widget, &evt);

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
  m_ctx.managers.selectionManager = &m_sel;
  m_mgr.setupReferences(setup);

  QWidget windowB; // distinct top-level window
  QWheelEvent wheel(QPointF(0, 0), QPointF(0, 0), QPoint(), QPoint(0, 120), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  const bool consumed = m_mgr.handleWheelEvent(&windowB, &wheel);

  QVERIFY(!consumed);
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

QTEST_MAIN(TestEventManagerMouse)
#include "test_eventmanagermouse.moc"
