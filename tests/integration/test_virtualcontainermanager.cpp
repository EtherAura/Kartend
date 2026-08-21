#include "test_virtualcontainermanager.h"
#include <QScrollArea>

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

void TestVirtualContainerManager::alignmentAnchorsThePaintedEdgeNotTheCellBox() {
  VirtualContainerManager mgr;
  // The first and last cell carry dead width the artwork never paints (the
  // art box is a square capped by the cell HEIGHT, so a wider cell is blank
  // at its edges). Anchoring the CONTAINER left that blank margin pressed
  // against the viewport edge, which is what still read as "the gap between
  // the grid and the details pane is too big" after the container itself was
  // already provably flush. What must end up flush is what is PAINTED.
  constexpr int kAvailable = 1000;
  constexpr int kContent = 600;
  constexpr int kInset = 50; // dead px per side
  constexpr int kPainted = kContent - 2 * kInset;

  const int left =
      mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Left, kInset);
  const int centre =
      mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Center, kInset);
  const int right =
      mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Right, kInset);

  // Assert on the PAINTED band, which is the container plus the dead margin.
  QCOMPARE(left + kInset, 0);                             // painted flush left
  QCOMPARE(right + kInset + kPainted, kAvailable);        // painted flush right
  QCOMPARE(centre + kInset, (kAvailable - kPainted) / 2); // painted centred
  // Equal air on both sides of the painted band when centred.
  QCOMPARE(centre + kInset, kAvailable - (centre + kInset + kPainted));

  // A zero inset must reproduce the old cell-box behaviour exactly, so
  // surfaces without dead margin are untouched.
  QCOMPARE(mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Left, 0), 0);
  QCOMPARE(mgr.calculateContainerPosition(kAvailable, kContent, HorizontalAlignment::Right, 0),
           kAvailable - kContent);
}

void TestVirtualContainerManager::chromeResizeRealignsPerTheSetting() {
  VirtualContainerManager mgr;
  // The alignment setting is CONTINUOUS: whenever the usable width changes —
  // a sidebar drag, a scrollbar lane appearing, a window resize — the grid is
  // re-aligned to honour it (maintainer, 2026-08-20: "the alignment should be
  // recalculated each time any sidebar width changes, according to the
  // specified alignment setting").
  //
  // This REPLACES an earlier hold that pinned the grid in window coordinates
  // through chrome resizes. That existed because an overlay scrollbar lane
  // reserved 21px nothing painted in (Kartend-3o4i4), which made re-alignment
  // look like the grid chasing the details pane. With the lane gone, holding
  // would only make the alignment setting mean "wherever it last landed".
  constexpr int kContent = 600;
  constexpr int kInset = 50;
  constexpr int kPainted = kContent - 2 * kInset;

  for (const int available : {1000, 988, 1000, 840, 1200}) {
    for (const auto align :
         {HorizontalAlignment::Left, HorizontalAlignment::Center, HorizontalAlignment::Right}) {
      const int x = mgr.calculateContainerPosition(available, kContent, align, kInset);
      const int paintedLeft = x + kInset;
      switch (align) {
      case HorizontalAlignment::Left:
        QCOMPARE(paintedLeft, 0);
        break;
      case HorizontalAlignment::Right:
        QCOMPARE(paintedLeft + kPainted, available);
        break;
      case HorizontalAlignment::Center:
        // Symmetric to the pixel, at every width.
        QCOMPARE(paintedLeft, available - (paintedLeft + kPainted));
        break;
      }
    }
  }
}

void TestVirtualContainerManager::anUnmeasuredInsetIsNotALayoutChange() {
  // The painted inset is read off a materialized cell, and the widget pool
  // empties for a beat during any relayout. A details-pane resize therefore
  // reported 47, then 0, then 47 — and reading that 0 as a real inset change
  // discarded the held position twice, so the grid still jumped even with the
  // hold in place (field report 2026-08-20, "the grid still moves/refreshes
  // when changing width of details sidebar"). A negative report means NOT
  // MEASURED and must fall back to the last known value.
  QCOMPARE(VirtualContainerManager::resolveContentInset(-1, 47), 47);
  QCOMPARE(VirtualContainerManager::resolveContentInset(-1, 0), 0);
  // Nothing measured yet at all: zero is the only sane starting point, and it
  // matches the pre-inset behaviour.
  QCOMPARE(VirtualContainerManager::resolveContentInset(-1, -1), 0);
  // A real measurement always wins, including a genuine zero (a cell whose
  // art fills its full width has no dead margin).
  QCOMPARE(VirtualContainerManager::resolveContentInset(0, 47), 0);
  QCOMPARE(VirtualContainerManager::resolveContentInset(12, 47), 12);
}

void TestVirtualContainerManager::hiddenScrollbarDoesNotReserveWidth() {
  // Wire a real scroll area whose vertical bar is hidden while the content
  // is tall enough that the old code PREDICTED a bar and subtracted its
  // width. Left alignment must still put the container flush at x = 0.
  QScrollArea area;
  area.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  area.resize(1000, 400);
  auto *grid = new QWidget(&area);
  area.setWidget(grid);
  area.show();
  QVERIFY(QTest::qWaitForWindowExposed(&area));

  VirtualContainerManager mgr;
  mgr.setScrollArea(&area);
  mgr.setGridContainer(grid);
  mgr.createContainer();
  QVERIFY(mgr.hasContainer());

  ContainerPositionParams params;
  params.totalWidth = 300;   // comfortably narrower than the viewport
  params.totalHeight = 4000; // taller than the viewport → a bar was predicted
  params.itemsPerRow = 3;
  params.totalItems = 9;
  // RIGHT, deliberately: Left returns 0 whatever the available width, so it
  // cannot detect a phantom reservation. Right is derived from the width,
  // so a subtracted-but-absent scrollbar shows up as a missing gap.
  params.alignment = HorizontalAlignment::Right;
  mgr.positionContainer(params);

  const int expected = area.viewport()->width() - params.totalWidth;
  QCOMPARE(mgr.container()->x(), expected);
}
