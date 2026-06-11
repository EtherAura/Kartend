// Tests for InteractionHelpers — pure helpers extracted from InteractionManager.
//
// These tests use plain integers / view-type values and exercise the helpers
// without instantiating any Qt object infrastructure.

#include <QTest>

#include "collectiontypes.h"
#include "interactionhelpers.h"

class TestInteractionHelpers : public QObject {
  Q_OBJECT
private slots:
  // stepSizeForViewType
  void stepSizeListIsOne();
  void stepSizeCoverFlowIsOne();
  void stepSizeGridIsGridWidth();
  void stepSizeHorizontalIsGridWidth();
  void stepSizeFallsBackToOneForNonPositiveGridWidth();

  // computeVerticalHoldStep — guard rails
  void holdStepReturnsInvalidWhenTotalItemsZeroOrNegative();
  void holdStepReturnsInvalidWhenStepSizeZeroOrNegative();

  // computeVerticalHoldStep — clamp behaviour (wrap == false)
  void holdStepClampsToZeroAtTopEdge();
  void holdStepClampsToLastIndexAtBottomEdge();
  void holdStepReturnsCurrentWhenAlreadyAtEdgeWithoutWrap();
  void holdStepNormalAdvanceDownDoesNotWrap();
  void holdStepNormalAdvanceUpDoesNotWrap();

  // computeVerticalHoldStep — wrap behaviour (wrap == true)
  void holdStepWrapsBelowZeroToEnd();
  void holdStepWrapsAboveLastIndexToStart();
  void holdStepDoesNotWrapWhenStepStaysInRange();
  void holdStepWrapsLargeNegativeStep();
  void holdStepWrapsLargePositiveStep();

  // resolveOwnerIndex
  void resolvePrefersDbIndexWhenInRange();
  void resolveFallsBackToCurrentIndexWhenDbInvalid();
  void resolveReturnsMinusOneWhenBothInvalid();
  void resolveTreatsOutOfRangeDbIndexAsInvalid();
  void resolveTreatsOutOfRangeFallbackAsInvalid();
  void resolveReturnsMinusOneWhenCollectionsEmpty();
};

// ------------------------------ stepSizeForViewType -------------------------

void TestInteractionHelpers::stepSizeListIsOne() {
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::List, 5), 1);
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::List, 1), 1);
}

void TestInteractionHelpers::stepSizeCoverFlowIsOne() {
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::CoverFlow, 5), 1);
}

void TestInteractionHelpers::stepSizeGridIsGridWidth() {
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Grid, 5), 5);
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Grid, 12), 12);
}

void TestInteractionHelpers::stepSizeHorizontalIsGridWidth() {
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Horizontal, 4), 4);
}

void TestInteractionHelpers::stepSizeFallsBackToOneForNonPositiveGridWidth() {
  // Defensive fallback: gridWidth <= 0 should never happen at the call site
  // (the manager short-circuits earlier), but the helper must remain usable.
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Grid, 0), 1);
  QCOMPARE(InteractionHelpers::stepSizeForViewType(ViewType::Grid, -3), 1);
}

// ------------------------------ computeVerticalHoldStep — guards ------------

void TestInteractionHelpers::holdStepReturnsInvalidWhenTotalItemsZeroOrNegative() {
  const auto a = InteractionHelpers::computeVerticalHoldStep(/*currentIndex=*/0,
                                                             /*direction=*/1,
                                                             /*stepSize=*/1,
                                                             /*totalItems=*/0,
                                                             /*wrap=*/true);
  QCOMPARE(a.nextIndex, -1);
  QVERIFY(!a.didWrap);

  const auto b = InteractionHelpers::computeVerticalHoldStep(0, 1, 1, -5, false);
  QCOMPARE(b.nextIndex, -1);
  QVERIFY(!b.didWrap);
}

void TestInteractionHelpers::holdStepReturnsInvalidWhenStepSizeZeroOrNegative() {
  const auto a = InteractionHelpers::computeVerticalHoldStep(0, 1, 0, 10, true);
  QCOMPARE(a.nextIndex, -1);
  const auto b = InteractionHelpers::computeVerticalHoldStep(0, 1, -2, 10, true);
  QCOMPARE(b.nextIndex, -1);
}

// ------------------------------ computeVerticalHoldStep — clamp -------------

void TestInteractionHelpers::holdStepClampsToZeroAtTopEdge() {
  // currentIndex=2, direction=-1, stepSize=5, totalItems=10, wrap=false
  // raw next = 2 - 5 = -3 → clamp to 0.
  const auto step = InteractionHelpers::computeVerticalHoldStep(2, -1, 5, 10, false);
  QCOMPARE(step.nextIndex, 0);
  QVERIFY(!step.didWrap);
}

void TestInteractionHelpers::holdStepClampsToLastIndexAtBottomEdge() {
  // currentIndex=8, direction=+1, stepSize=5, totalItems=10, wrap=false
  // raw next = 13 → clamp to 9.
  const auto step = InteractionHelpers::computeVerticalHoldStep(8, 1, 5, 10, false);
  QCOMPARE(step.nextIndex, 9);
  QVERIFY(!step.didWrap);
}

void TestInteractionHelpers::holdStepReturnsCurrentWhenAlreadyAtEdgeWithoutWrap() {
  // currentIndex=0, direction=-1: clamps right back to 0.
  const auto top = InteractionHelpers::computeVerticalHoldStep(0, -1, 5, 10, false);
  QCOMPARE(top.nextIndex, 0);

  // currentIndex=9, direction=+1: clamps to 9.
  const auto bot = InteractionHelpers::computeVerticalHoldStep(9, 1, 5, 10, false);
  QCOMPARE(bot.nextIndex, 9);
}

void TestInteractionHelpers::holdStepNormalAdvanceDownDoesNotWrap() {
  const auto step = InteractionHelpers::computeVerticalHoldStep(0, 1, 5, 100, false);
  QCOMPARE(step.nextIndex, 5);
  QVERIFY(!step.didWrap);
}

void TestInteractionHelpers::holdStepNormalAdvanceUpDoesNotWrap() {
  const auto step = InteractionHelpers::computeVerticalHoldStep(20, -1, 5, 100, false);
  QCOMPARE(step.nextIndex, 15);
  QVERIFY(!step.didWrap);
}

// ------------------------------ computeVerticalHoldStep — wrap --------------

void TestInteractionHelpers::holdStepWrapsBelowZeroToEnd() {
  // currentIndex=2, direction=-1, stepSize=5, totalItems=10, wrap=true
  // raw next = -3 → ((-3 % 10) + 10) % 10 = 7.
  const auto step = InteractionHelpers::computeVerticalHoldStep(2, -1, 5, 10, true);
  QCOMPARE(step.nextIndex, 7);
  QVERIFY(step.didWrap);
}

void TestInteractionHelpers::holdStepWrapsAboveLastIndexToStart() {
  // currentIndex=8, direction=+1, stepSize=5, totalItems=10, wrap=true
  // raw next = 13 → 13 % 10 = 3.
  const auto step = InteractionHelpers::computeVerticalHoldStep(8, 1, 5, 10, true);
  QCOMPARE(step.nextIndex, 3);
  QVERIFY(step.didWrap);
}

void TestInteractionHelpers::holdStepDoesNotWrapWhenStepStaysInRange() {
  const auto step = InteractionHelpers::computeVerticalHoldStep(0, 1, 5, 10, true);
  QCOMPARE(step.nextIndex, 5);
  QVERIFY(!step.didWrap);
}

void TestInteractionHelpers::holdStepWrapsLargeNegativeStep() {
  // raw next = 0 - 23 = -23 → ((-23 % 10) + 10) % 10 = 7.
  const auto step = InteractionHelpers::computeVerticalHoldStep(0, -1, 23, 10, true);
  QCOMPARE(step.nextIndex, 7);
  QVERIFY(step.didWrap);
}

void TestInteractionHelpers::holdStepWrapsLargePositiveStep() {
  // raw next = 0 + 23 = 23 → 23 % 10 = 3.
  const auto step = InteractionHelpers::computeVerticalHoldStep(0, 1, 23, 10, true);
  QCOMPARE(step.nextIndex, 3);
  QVERIFY(step.didWrap);
}

// ------------------------------ resolveOwnerIndex ---------------------------

void TestInteractionHelpers::resolvePrefersDbIndexWhenInRange() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(2, 0, 5), 2);
}

void TestInteractionHelpers::resolveFallsBackToCurrentIndexWhenDbInvalid() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(-1, 3, 5), 3);
}

void TestInteractionHelpers::resolveReturnsMinusOneWhenBothInvalid() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(-1, -1, 5), -1);
}

void TestInteractionHelpers::resolveTreatsOutOfRangeDbIndexAsInvalid() {
  // dbIndex >= collectionsSize is treated as invalid → fallback wins.
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(99, 1, 5), 1);
}

void TestInteractionHelpers::resolveTreatsOutOfRangeFallbackAsInvalid() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(-1, 99, 5), -1);
}

void TestInteractionHelpers::resolveReturnsMinusOneWhenCollectionsEmpty() {
  QCOMPARE(InteractionHelpers::resolveOwnerIndex(0, 0, 0), -1);
}

QTEST_APPLESS_MAIN(TestInteractionHelpers)
#include "test_interactionhelpers.moc"
