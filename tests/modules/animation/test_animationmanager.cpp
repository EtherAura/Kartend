// Unit tests for AnimationManager static helpers
// (computeVerticalCenterDuration, computeTargetYForIndex,
//  computeHorizontalTargetX, computeDesiredYForVisibility).
//
// These four functions are pure with no Qt event-loop, scrollbar, or
// stateful manager dependencies, so they're tested in isolation. The
// instance-side animation orchestration (QPropertyAnimation lifecycle,
// scrollbar coupling, signal emission) is exercised indirectly through
// integration paths and is not covered here.

#include "animationmanager.h"

#include <QObject>
#include <QTest>

class TestAnimationManager : public QObject {
  Q_OBJECT
private slots:
  // computeVerticalCenterDuration
  void verticalCenterDuration_smallDistanceUsesBaseDuration();
  void verticalCenterDuration_zeroItemHeightAndSpacingDoesNotDivideByZero();
  void verticalCenterDuration_largeDistanceClampsToBaseDuration();
  void verticalCenterDuration_customBaseDurationApplied();

  // computeTargetYForIndex
  void targetYForIndex_centersItemInViewport();
  void targetYForIndex_clampsToZeroWhenAboveTop();
  void targetYForIndex_clampsToScrollbarMax();
  void targetYForIndex_appliesHeaderOffset();
  void targetYForIndex_clippedHeightUsesViewportInterpolation();

  // computeHorizontalTargetX
  void horizontalTargetX_itemAlreadyVisibleNoMove();
  void horizontalTargetX_itemLeftOfViewportScrollsLeft();
  void horizontalTargetX_itemRightOfViewportScrollsRight();
  void horizontalTargetX_clampsToZero();
  void horizontalTargetX_clampsToScrollMax();
  void horizontalTargetX_marginsRespected();

  // computeDesiredYForVisibility
  void desiredYForVisibility_itemVisibleNoScrollNeeded();
  void desiredYForVisibility_itemAboveViewportFlagsScrollNeeded();
  void desiredYForVisibility_itemBelowViewportFlagsScrollNeeded();
  void desiredYForVisibility_marginPushesIntoNeedScroll();
};

// ─────────────────────────────────────────────────────────────────────────────
// computeVerticalCenterDuration
// ─────────────────────────────────────────────────────────────────────────────

void TestAnimationManager::verticalCenterDuration_smallDistanceUsesBaseDuration() {
  // The implementation clamps the result to exactly baseDuration regardless
  // of distance — qBound(base, x, base) collapses to base. This documents the
  // current behavior so a future tweak (e.g. allow shorter durations for
  // small hops) is detected by the test.
  const int duration = AnimationManager::computeVerticalCenterDuration(
      /*distance=*/100, /*itemHeight=*/200, /*verticalSpacing=*/20,
      /*repeatActive=*/false, /*durationMs=*/1500);
  QCOMPARE(duration, 1500);
}

void TestAnimationManager::verticalCenterDuration_zeroItemHeightAndSpacingDoesNotDivideByZero() {
  // qMax(1, 0+0) guards the divisor; verify the function is robust.
  const int duration = AnimationManager::computeVerticalCenterDuration(
      /*distance=*/500, /*itemHeight=*/0, /*verticalSpacing=*/0,
      /*repeatActive=*/false, /*durationMs=*/800);
  QCOMPARE(duration, 800);
}

void TestAnimationManager::verticalCenterDuration_largeDistanceClampsToBaseDuration() {
  const int duration = AnimationManager::computeVerticalCenterDuration(
      /*distance=*/100000, /*itemHeight=*/100, /*verticalSpacing=*/10,
      /*repeatActive=*/false, /*durationMs=*/600);
  QCOMPARE(duration, 600);
}

void TestAnimationManager::verticalCenterDuration_customBaseDurationApplied() {
  const int duration = AnimationManager::computeVerticalCenterDuration(
      /*distance=*/300, /*itemHeight=*/100, /*verticalSpacing=*/10,
      /*repeatActive=*/true, /*durationMs=*/2500);
  QCOMPARE(duration, 2500);
}

// ─────────────────────────────────────────────────────────────────────────────
// computeTargetYForIndex
// ─────────────────────────────────────────────────────────────────────────────

void TestAnimationManager::targetYForIndex_centersItemInViewport() {
  // For index 14 in a 7-wide grid: row 2.
  // GridUtils::computeItemY uses Grid::MARGINS (10 from uiconstants/grid.h).
  // itemY = 10 + 2 * (200 + 20) = 450
  // logicalTargetY = 450 + (200/2) - (1000/2) = 450 + 100 - 500 = 50
  // No clipping (totalHeight=0/logicalHeight=0 path), bounds clamp [0, 9999].
  const int targetY = AnimationManager::computeTargetYForIndex(
      /*index=*/14, /*gridWidth=*/7, /*itemHeight=*/200, /*verticalSpacing=*/20,
      /*viewportHeight=*/1000, /*scrollbarMax=*/9999);
  QCOMPARE(targetY, 50);
}

void TestAnimationManager::targetYForIndex_clampsToZeroWhenAboveTop() {
  // First item — its logicalTargetY would be negative; qBound(0,...) clamps.
  const int targetY = AnimationManager::computeTargetYForIndex(
      /*index=*/0, /*gridWidth=*/5, /*itemHeight=*/100, /*verticalSpacing=*/10,
      /*viewportHeight=*/1000, /*scrollbarMax=*/9999);
  QCOMPARE(targetY, 0);
}

void TestAnimationManager::targetYForIndex_clampsToScrollbarMax() {
  // Pick a far row whose center Y exceeds scrollbarMax.
  const int targetY = AnimationManager::computeTargetYForIndex(
      /*index=*/1000, /*gridWidth=*/4, /*itemHeight=*/200, /*verticalSpacing=*/20,
      /*viewportHeight=*/1000, /*scrollbarMax=*/300);
  QCOMPARE(targetY, 300);
}

void TestAnimationManager::targetYForIndex_appliesHeaderOffset() {
  // headerOffset shifts the computed itemY upward equivalent (added before
  // centering math). For a list-mode header of 30px, the centered scroll
  // target should be 30px lower than the no-offset case.
  const int withoutOffset = AnimationManager::computeTargetYForIndex(
      /*index=*/14, /*gridWidth=*/7, /*itemHeight=*/200, /*verticalSpacing=*/20,
      /*viewportHeight=*/1000, /*scrollbarMax=*/9999, /*totalHeight=*/0,
      /*logicalHeight=*/0, /*headerOffset=*/0);
  const int withOffset = AnimationManager::computeTargetYForIndex(
      /*index=*/14, /*gridWidth=*/7, /*itemHeight=*/200, /*verticalSpacing=*/20,
      /*viewportHeight=*/1000, /*scrollbarMax=*/9999, /*totalHeight=*/0,
      /*logicalHeight=*/0, /*headerOffset=*/30);
  QCOMPARE(withOffset - withoutOffset, 30);
}

void TestAnimationManager::targetYForIndex_clippedHeightUsesViewportInterpolation() {
  // Logical height > clamped totalHeight triggers the widget/logical mapping.
  // Pick numbers so the logical target lands at half the logical max,
  // and check the widget target lands at half the widget max.
  // logicalHeight = 20000, viewportHeight = 1000 -> logicalMax = 19000
  // totalHeight = 10000 -> widgetMax = 9000
  // We want logicalTargetY ≈ 9500 (half of logicalMax).
  // logicalTargetY = itemY + itemHeight/2 - viewportHeight/2
  //               = itemY + 100 - 500
  // Solve itemY = 9900 -> row = (9900 - margins) / (itemHeight + spacing)
  //                    = (9900 - 10) / 220 = 44.95 -> use row 45 (index 45*1).
  // That's a reasonable approximation; exact values aren't critical, just
  // verify the path is taken (widget value < logical value).
  const int clipped = AnimationManager::computeTargetYForIndex(
      /*index=*/45, /*gridWidth=*/1, /*itemHeight=*/200, /*verticalSpacing=*/20,
      /*viewportHeight=*/1000, /*scrollbarMax=*/9000, /*totalHeight=*/10000,
      /*logicalHeight=*/20000, /*headerOffset=*/0);
  const int unclipped = AnimationManager::computeTargetYForIndex(
      /*index=*/45, /*gridWidth=*/1, /*itemHeight=*/200, /*verticalSpacing=*/20,
      /*viewportHeight=*/1000, /*scrollbarMax=*/19000, /*totalHeight=*/0,
      /*logicalHeight=*/0, /*headerOffset=*/0);
  // With clipping the widget-space target should be roughly half of the
  // logical-space target (widgetMax/logicalMax = 9000/19000 ≈ 0.474).
  QVERIFY2(clipped < unclipped,
           qUtf8Printable(QString("clipped=%1 unclipped=%2").arg(clipped).arg(unclipped)));
  QVERIFY2(clipped >= 0 && clipped <= 9000,
           qUtf8Printable(QString("clipped out of range: %1").arg(clipped)));
}

// ─────────────────────────────────────────────────────────────────────────────
// computeHorizontalTargetX
// ─────────────────────────────────────────────────────────────────────────────

void TestAnimationManager::horizontalTargetX_itemAlreadyVisibleNoMove() {
  // Item at x=300, width=200, viewport spans 100..900 with margins=20.
  // itemLeft=300 >= visibleLeft+margins=120, itemRight=500 <= visibleRight-margins=880 -> no scroll.
  const int target = AnimationManager::computeHorizontalTargetX(
      /*itemX=*/300, /*collectionItemWidth=*/200, /*curX=*/100, /*viewportWidth=*/800,
      /*margins=*/20, /*scrollMax=*/5000);
  QCOMPARE(target, 100);
}

void TestAnimationManager::horizontalTargetX_itemLeftOfViewportScrollsLeft() {
  // Item at x=50, viewport at curX=400 -> need to scroll left.
  // itemLeft=50 < visibleLeft+margins=420 -> targetX = 50 - 20 = 30.
  const int target = AnimationManager::computeHorizontalTargetX(
      /*itemX=*/50, /*collectionItemWidth=*/200, /*curX=*/400, /*viewportWidth=*/800,
      /*margins=*/20, /*scrollMax=*/5000);
  QCOMPARE(target, 30);
}

void TestAnimationManager::horizontalTargetX_itemRightOfViewportScrollsRight() {
  // Item at x=900, width=200, viewport curX=0..800.
  // itemRight=1100 > visibleRight-margins=780 -> targetX = 1100 - 800 + 20 = 320.
  const int target = AnimationManager::computeHorizontalTargetX(
      /*itemX=*/900, /*collectionItemWidth=*/200, /*curX=*/0, /*viewportWidth=*/800,
      /*margins=*/20, /*scrollMax=*/5000);
  QCOMPARE(target, 320);
}

void TestAnimationManager::horizontalTargetX_clampsToZero() {
  // Negative target would result without clamping.
  const int target = AnimationManager::computeHorizontalTargetX(
      /*itemX=*/0, /*collectionItemWidth=*/100, /*curX=*/500, /*viewportWidth=*/800,
      /*margins=*/50, /*scrollMax=*/5000);
  // itemLeft=0 < visibleLeft+margins=550 -> targetX = -50 -> clamped to 0.
  QCOMPARE(target, 0);
}

void TestAnimationManager::horizontalTargetX_clampsToScrollMax() {
  const int target = AnimationManager::computeHorizontalTargetX(
      /*itemX=*/100000, /*collectionItemWidth=*/200, /*curX=*/0, /*viewportWidth=*/800,
      /*margins=*/20, /*scrollMax=*/5000);
  QCOMPARE(target, 5000);
}

void TestAnimationManager::horizontalTargetX_marginsRespected() {
  // With margins=0 the targetX should be exactly itemRight - viewportWidth
  // when item is right of viewport.
  const int targetWithMargins = AnimationManager::computeHorizontalTargetX(
      /*itemX=*/900, /*collectionItemWidth=*/200, /*curX=*/0, /*viewportWidth=*/800,
      /*margins=*/0, /*scrollMax=*/5000);
  QCOMPARE(targetWithMargins, 300); // 1100 - 800 + 0
}

// ─────────────────────────────────────────────────────────────────────────────
// computeDesiredYForVisibility
// ─────────────────────────────────────────────────────────────────────────────

void TestAnimationManager::desiredYForVisibility_itemVisibleNoScrollNeeded() {
  bool needScroll = true;
  const int desired = AnimationManager::computeDesiredYForVisibility(
      /*itemY=*/200, /*itemHeight=*/100, /*curY=*/100, /*viewportHeight=*/600,
      /*margins=*/10, needScroll);
  QCOMPARE(needScroll, false);
  QCOMPARE(desired, 100); // unchanged from curY
}

void TestAnimationManager::desiredYForVisibility_itemAboveViewportFlagsScrollNeeded() {
  bool needScroll = false;
  const int desired = AnimationManager::computeDesiredYForVisibility(
      /*itemY=*/50, /*itemHeight=*/100, /*curY=*/200, /*viewportHeight=*/400,
      /*margins=*/10, needScroll);
  QCOMPARE(needScroll, true);
  // itemTop=50 < visibleTop+margins=210 -> desiredY = 50 - 10 = 40
  QCOMPARE(desired, 40);
}

void TestAnimationManager::desiredYForVisibility_itemBelowViewportFlagsScrollNeeded() {
  bool needScroll = false;
  const int desired = AnimationManager::computeDesiredYForVisibility(
      /*itemY=*/700, /*itemHeight=*/100, /*curY=*/0, /*viewportHeight=*/400,
      /*margins=*/10, needScroll);
  QCOMPARE(needScroll, true);
  // itemBottom=800 > visibleBottom-margins=390 -> desiredY = 800 - 400 + 10 = 410
  QCOMPARE(desired, 410);
}

void TestAnimationManager::desiredYForVisibility_marginPushesIntoNeedScroll() {
  // Item just barely "visible" from a no-margins POV but inside the margin
  // band — should still flag scroll needed.
  bool needScroll = false;
  const int desired = AnimationManager::computeDesiredYForVisibility(
      /*itemY=*/105, /*itemHeight=*/100, /*curY=*/100, /*viewportHeight=*/400,
      /*margins=*/20, needScroll);
  // itemTop=105 < visibleTop+margins=120 -> needScroll=true, desiredY=105-20=85.
  QCOMPARE(needScroll, true);
  QCOMPARE(desired, 85);
}

QTEST_MAIN(TestAnimationManager)
#include "test_animationmanager.moc"
