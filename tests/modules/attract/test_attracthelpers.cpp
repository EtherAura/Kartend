// Tests for AttractHelpers — pure helpers extracted from AttractManager.
// These cover the selection-advance pick (linear + random with
// direction-biased range), the sub-pixel scroll accumulator, and the
// boundary-aware position update.

#include <QTest>

#include "attracthelpers.h"

using AttractHelpers::accumulateScrollDelta;
using AttractHelpers::linearAdvanceIndex;
using AttractHelpers::nextScrollPosition;
using AttractHelpers::randomAdvanceRange;

class TestAttractHelpers : public QObject {
  Q_OBJECT
private slots:
  // linearAdvanceIndex
  void linearAdvanceIndex_zeroOrNegativeTotalReturnsMinusOne();
  void linearAdvanceIndex_noSelectionStartsAtHead();
  void linearAdvanceIndex_advancesByOne();
  void linearAdvanceIndex_wrapsAtEnd();

  // randomAdvanceRange — forward direction
  void randomAdvanceRange_forwardPicksFromAfterCurrent();
  void randomAdvanceRange_forwardAtTailFallsBackToHead();
  void randomAdvanceRange_forwardFromUnselectedScansFullList();

  // randomAdvanceRange — reverse direction
  void randomAdvanceRange_reversePicksBeforeCurrent();
  void randomAdvanceRange_reverseAtHeadFallsBackToTail();

  // randomAdvanceRange — edge cases
  void randomAdvanceRange_zeroTotalIsEmpty();
  void randomAdvanceRange_singleItemCollectionIsEmpty();
  void randomAdvanceRange_neverProducesNegativeStart();
  void randomAdvanceRange_neverExceedsTotal();

  // accumulateScrollDelta
  void accumulateScrollDelta_subPixelSpeedAccumulates();
  void accumulateScrollDelta_pixelSpeedFiresEachTick();
  void accumulateScrollDelta_largeSpeedYieldsMultiPixelStep();
  void accumulateScrollDelta_returnsFractionalResidual();

  // nextScrollPosition
  void nextScrollPosition_movesForward();
  void nextScrollPosition_movesBackward();
  void nextScrollPosition_clampsToMaxAndFlagsHit();
  void nextScrollPosition_clampsToMinAndFlagsHit();
  void nextScrollPosition_exactlyAtBoundaryHitsBoundary();
};

// --------------------------- linearAdvanceIndex -----------------------------

void TestAttractHelpers::linearAdvanceIndex_zeroOrNegativeTotalReturnsMinusOne() {
  QCOMPARE(linearAdvanceIndex(0, 0), -1);
  QCOMPARE(linearAdvanceIndex(5, 0), -1);
  QCOMPARE(linearAdvanceIndex(0, -3), -1);
}

void TestAttractHelpers::linearAdvanceIndex_noSelectionStartsAtHead() {
  QCOMPARE(linearAdvanceIndex(-1, 10), 0);
}

void TestAttractHelpers::linearAdvanceIndex_advancesByOne() {
  QCOMPARE(linearAdvanceIndex(0, 10), 1);
  QCOMPARE(linearAdvanceIndex(4, 10), 5);
  QCOMPARE(linearAdvanceIndex(8, 10), 9);
}

void TestAttractHelpers::linearAdvanceIndex_wrapsAtEnd() {
  QCOMPARE(linearAdvanceIndex(9, 10), 0);
}

// --------------------------- randomAdvanceRange (forward) -------------------

void TestAttractHelpers::randomAdvanceRange_forwardPicksFromAfterCurrent() {
  const auto range = randomAdvanceRange(/*current=*/3, /*total=*/10, /*direction=*/1);
  QCOMPARE(range.rangeStart, 4);
  QCOMPARE(range.rangeEnd, 10);
  QVERIFY(!range.isEmpty());
}

void TestAttractHelpers::randomAdvanceRange_forwardAtTailFallsBackToHead() {
  // At the end of the list in the forward direction → the helper flips to
  // the opposite half so the pick still produces a visible change.
  const auto range = randomAdvanceRange(/*current=*/9, /*total=*/10, /*direction=*/1);
  QCOMPARE(range.rangeStart, 0);
  QCOMPARE(range.rangeEnd, 9);
}

void TestAttractHelpers::randomAdvanceRange_forwardFromUnselectedScansFullList() {
  // current=-1 means "no selection yet"; forward range becomes [0, total).
  const auto range = randomAdvanceRange(/*current=*/-1, /*total=*/10, /*direction=*/1);
  QCOMPARE(range.rangeStart, 0);
  QCOMPARE(range.rangeEnd, 10);
}

// --------------------------- randomAdvanceRange (reverse) -------------------

void TestAttractHelpers::randomAdvanceRange_reversePicksBeforeCurrent() {
  const auto range = randomAdvanceRange(/*current=*/7, /*total=*/10, /*direction=*/-1);
  QCOMPARE(range.rangeStart, 0);
  QCOMPARE(range.rangeEnd, 7);
}

void TestAttractHelpers::randomAdvanceRange_reverseAtHeadFallsBackToTail() {
  // Reverse direction with current at the head → falls back to the
  // forward half so the pick isn't trapped.
  const auto range = randomAdvanceRange(/*current=*/0, /*total=*/10, /*direction=*/-1);
  QCOMPARE(range.rangeStart, 1);
  QCOMPARE(range.rangeEnd, 10);
}

// --------------------------- randomAdvanceRange (edges) ---------------------

void TestAttractHelpers::randomAdvanceRange_zeroTotalIsEmpty() {
  const auto range = randomAdvanceRange(0, 0, 1);
  QVERIFY(range.isEmpty());
}

void TestAttractHelpers::randomAdvanceRange_singleItemCollectionIsEmpty() {
  // current=0, total=1, forward: (0,1) → start=1,end=1 → empty primary half.
  // Reverse fallback: start=0,end=0 → also empty. Net result: degenerate.
  const auto range = randomAdvanceRange(/*current=*/0, /*total=*/1, /*direction=*/1);
  QVERIFY(range.isEmpty());
}

void TestAttractHelpers::randomAdvanceRange_neverProducesNegativeStart() {
  // Defensive: a negative `current` in the reverse direction must not
  // produce a negative start (caller passes the start into a bounded()
  // call that would assert on negatives).
  const auto range = randomAdvanceRange(/*current=*/-5, /*total=*/10, /*direction=*/-1);
  QVERIFY(range.rangeStart >= 0);
}

void TestAttractHelpers::randomAdvanceRange_neverExceedsTotal() {
  const auto range = randomAdvanceRange(/*current=*/100, /*total=*/10, /*direction=*/1);
  QVERIFY(range.rangeEnd <= 10);
}

// --------------------------- accumulateScrollDelta --------------------------

void TestAttractHelpers::accumulateScrollDelta_subPixelSpeedAccumulates() {
  // 0.4 px/tick: after one tick delta=0, residual=0.4; after two,
  // accumulator (0.4) + 0.4 = 0.8, still 0, residual 0.8; after three
  // it crosses 1.0 and fires a single-pixel step.
  auto t1 = accumulateScrollDelta(0.0, 0.4);
  QCOMPARE(t1.delta, 0);
  QCOMPARE(t1.newAccumulator, 0.4);

  auto t2 = accumulateScrollDelta(t1.newAccumulator, 0.4);
  QCOMPARE(t2.delta, 0);
  QCOMPARE(t2.newAccumulator, 0.8);

  auto t3 = accumulateScrollDelta(t2.newAccumulator, 0.4);
  QCOMPARE(t3.delta, 1);
  // 0.8 + 0.4 = 1.2; residual is 0.2 within fp tolerance.
  QVERIFY(qFuzzyCompare(t3.newAccumulator + 1.0, 1.2));
}

void TestAttractHelpers::accumulateScrollDelta_pixelSpeedFiresEachTick() {
  const auto step = accumulateScrollDelta(0.0, 1.0);
  QCOMPARE(step.delta, 1);
  QCOMPARE(step.newAccumulator, 0.0);
}

void TestAttractHelpers::accumulateScrollDelta_largeSpeedYieldsMultiPixelStep() {
  const auto step = accumulateScrollDelta(0.0, 5.0);
  QCOMPARE(step.delta, 5);
  QCOMPARE(step.newAccumulator, 0.0);
}

void TestAttractHelpers::accumulateScrollDelta_returnsFractionalResidual() {
  const auto step = accumulateScrollDelta(0.7, 0.6);
  QCOMPARE(step.delta, 1);
  QVERIFY(qFuzzyCompare(step.newAccumulator + 1.0, 1.3));
}

// --------------------------- nextScrollPosition -----------------------------

void TestAttractHelpers::nextScrollPosition_movesForward() {
  const auto p = nextScrollPosition(/*current=*/50, /*delta=*/3, /*direction=*/1,
                                     /*min=*/0, /*max=*/100);
  QCOMPARE(p.next, 53);
  QVERIFY(!p.hitMax);
  QVERIFY(!p.hitMin);
}

void TestAttractHelpers::nextScrollPosition_movesBackward() {
  const auto p = nextScrollPosition(50, 3, /*direction=*/-1, 0, 100);
  QCOMPARE(p.next, 47);
  QVERIFY(!p.hitMax);
  QVERIFY(!p.hitMin);
}

void TestAttractHelpers::nextScrollPosition_clampsToMaxAndFlagsHit() {
  const auto p = nextScrollPosition(/*current=*/98, /*delta=*/5, /*direction=*/1, 0, 100);
  QCOMPARE(p.next, 100);
  QVERIFY(p.hitMax);
  QVERIFY(!p.hitMin);
}

void TestAttractHelpers::nextScrollPosition_clampsToMinAndFlagsHit() {
  const auto p = nextScrollPosition(/*current=*/2, /*delta=*/5, /*direction=*/-1, 0, 100);
  QCOMPARE(p.next, 0);
  QVERIFY(p.hitMin);
  QVERIFY(!p.hitMax);
}

void TestAttractHelpers::nextScrollPosition_exactlyAtBoundaryHitsBoundary() {
  // Equal-to-max counts as hitting max (matches the bar->maximum() bounce
  // semantics where reaching exactly max should pause and reverse).
  const auto p = nextScrollPosition(/*current=*/95, /*delta=*/5, /*direction=*/1, 0, 100);
  QCOMPARE(p.next, 100);
  QVERIFY(p.hitMax);
}

QTEST_APPLESS_MAIN(TestAttractHelpers)
#include "test_attracthelpers.moc"
