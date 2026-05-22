#include "test_virtualcontainermanager.h"

#include "virtualcontainermanager.h"

#include <QObject>
#include <QPointer>
#include <QTest>
#include <QWidget>

void TestVirtualContainerManager::testConstructionWithoutWiring() {
  VirtualContainerManager mgr;
  // No grid container, scroll area, or downstream managers set.
  QVERIFY(!mgr.hasContainer());
  QVERIFY(mgr.container() == nullptr);
  QCOMPARE(mgr.getEffectiveViewportWidth(), 0);
  QCOMPARE(mgr.getScrollbarWidth(), 0);
  QVERIFY(!mgr.willNeedVerticalScrollbar(/*totalHeight=*/10000));
}

void TestVirtualContainerManager::testCreateContainerRequiresGridContainer() {
  VirtualContainerManager mgr;
  mgr.createContainer();
  // Without a grid container the implementation logs a warning and bails;
  // no container should materialise.
  QVERIFY(!mgr.hasContainer());
}

void TestVirtualContainerManager::testCreateContainerProducesChildOfGrid() {
  QWidget grid;
  grid.resize(640, 480);

  VirtualContainerManager mgr;
  mgr.setGridContainer(&grid);
  mgr.createContainer();

  QVERIFY(mgr.hasContainer());
  QWidget *vc = mgr.container();
  QVERIFY(vc != nullptr);
  QCOMPARE(vc->parent(), static_cast<QObject *>(&grid));
  QCOMPARE(vc->objectName(), QStringLiteral("virtualContainer"));
}

void TestVirtualContainerManager::testCleanupContainerDropsTheContainerPointer() {
  QWidget grid;
  grid.resize(640, 480);

  VirtualContainerManager mgr;
  mgr.setGridContainer(&grid);
  mgr.createContainer();
  QVERIFY(mgr.hasContainer());

  mgr.cleanupContainer();
  QVERIFY(!mgr.hasContainer());
  QVERIFY(mgr.container() == nullptr);
}

void TestVirtualContainerManager::testCleanupIsIdempotent() {
  VirtualContainerManager mgr;
  // No container exists at all — cleanup should be a no-op, not a crash.
  mgr.cleanupContainer();
  mgr.cleanupContainer();
  QVERIFY(!mgr.hasContainer());
}

void TestVirtualContainerManager::testEffectiveViewportWidthWithoutScrollAreaIsZero() {
  VirtualContainerManager mgr;
  // Documented contract: returns 0 when m_scrollArea is null — callers
  // must short-circuit before requesting viewport math.
  QCOMPARE(mgr.getEffectiveViewportWidth(), 0);
}

void TestVirtualContainerManager::testScrollbarWidthWithoutScrollAreaIsZero() {
  VirtualContainerManager mgr;
  // Documented contract: returns 0 when m_scrollArea is null.
  QCOMPARE(mgr.getScrollbarWidth(), 0);
}

void TestVirtualContainerManager::testWillNeedVerticalScrollbarFalseWithoutScrollArea() {
  VirtualContainerManager mgr;
  // Documented contract: without a scroll area there's no viewport to compare
  // against, so the answer is always false regardless of totalHeight.
  QVERIFY(!mgr.willNeedVerticalScrollbar(0));
  QVERIFY(!mgr.willNeedVerticalScrollbar(100));
  QVERIFY(!mgr.willNeedVerticalScrollbar(std::numeric_limits<int>::max()));
}
