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
  // Content NARROWER than the viewport. Each alignment lands somewhere
  // different, and — the part that was broken — nothing may clip, because
  // every tile has room (user rule 2026-08-18: "if the entire grid fits
  // regardless, there shouldn't be any clipping").
  constexpr int kAvailable = 1000;
  constexpr int kContent = 600;
  const int left = mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Left);
  const int centre =
      mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Center);
  const int right =
      mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Right);

  QCOMPARE(left, 0);
  QCOMPARE(centre, (kAvailable - kContent) / 2);
  QCOMPARE(right, kAvailable - kContent);
  for (const int x : {left, centre, right}) {
    QVERIFY2(x >= 0, qPrintable(QStringLiteral("x %1 is negative — clips on the left").arg(x)));
    QVERIFY2(x + kContent <= kAvailable,
             qPrintable(QStringLiteral("x %1 + content runs past the viewport").arg(x)));
  }
}

void TestVirtualContainerManager::alignmentMovesContainerWhenContentOverflows() {
  VirtualContainerManager mgr;
  // Content WIDER than the viewport: clipping is unavoidable, so alignment
  // decides which edge survives. Left and Right used to compute the same
  // position, which is what made the setting look dead.
  constexpr int kAvailable = 1000;
  constexpr int kContent = 1400;
  const int left = mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Left);
  const int centre =
      mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Center);
  const int right =
      mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Right);

  QCOMPARE(left, 0);                      // left edge stays on screen
  QCOMPARE(right, kAvailable - kContent); // right edge stays on screen
  QCOMPARE(centre, (kAvailable - kContent) / 2);
  QVERIFY2(left > centre && centre > right,
           qPrintable(QStringLiteral("left %1, centre %2, right %3 must all differ")
                          .arg(left)
                          .arg(centre)
                          .arg(right)));
  QVERIFY2(left <= 0, "a fitting-side gap would mean wasted space, not clipping");
}
