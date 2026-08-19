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

void TestVirtualContainerManager::alignmentMovesContainerWhenContentFits() {
  VirtualContainerManager mgr;
  // Content narrower than the viewport: the three alignments must land the
  // container at three different x positions, left-to-right in order.
  constexpr int kAvailable = 1000;
  constexpr int kContent = 600;
  const int left = mgr.calculateContainerPosition(kAvailable, kContent, /*overflow=*/false,
                                                  HorizontalAlignment::Left, 0, 0, 0);
  const int centre = mgr.calculateContainerPosition(kAvailable, kContent, /*overflow=*/false,
                                                    HorizontalAlignment::Center, 0, 0, 0);
  const int right = mgr.calculateContainerPosition(kAvailable, kContent, /*overflow=*/false,
                                                   HorizontalAlignment::Right, 0, 0, 0);
  QVERIFY2(left < centre, qPrintable(QStringLiteral("left %1 vs centre %2").arg(left).arg(centre)));
  QVERIFY2(centre < right,
           qPrintable(QStringLiteral("centre %1 vs right %2").arg(centre).arg(right)));
}

void TestVirtualContainerManager::alignmentMovesContainerWhenContentOverflows() {
  VirtualContainerManager mgr;
  // Content WIDER than the viewport — the everyday case for a full grid,
  // where a row of tiles slightly exceeds the available width. Alignment
  // must still decide which edge is anchored; centring regardless is what
  // made the setting look dead.
  constexpr int kAvailable = 1000;
  constexpr int kContent = 1400;
  const int left = mgr.calculateContainerPosition(kAvailable, kContent, /*overflow=*/true,
                                                  HorizontalAlignment::Left, 0, 0, 0);
  const int centre = mgr.calculateContainerPosition(kAvailable, kContent, /*overflow=*/true,
                                                    HorizontalAlignment::Center, 0, 0, 0);
  const int right = mgr.calculateContainerPosition(kAvailable, kContent, /*overflow=*/true,
                                                   HorizontalAlignment::Right, 0, 0, 0);
  QVERIFY2(left > centre, qPrintable(QStringLiteral("left %1 vs centre %2").arg(left).arg(centre)));
  QVERIFY2(centre > right,
           qPrintable(QStringLiteral("centre %1 vs right %2").arg(centre).arg(right)));
}
