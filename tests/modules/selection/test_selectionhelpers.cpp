#include <QTest>

#include "selectionhelpers.h"

class TestSelectionHelpers : public QObject {
  Q_OBJECT

private slots:
  // shouldTreatAsNewRow
  void shouldTreatAsNewRow_zeroGridWidth_returnsFalse();
  void shouldTreatAsNewRow_negativeGridWidth_returnsFalse();
  void shouldTreatAsNewRow_lastRowNegative_returnsTrue();
  void shouldTreatAsNewRow_sameRow_returnsFalse();
  void shouldTreatAsNewRow_differentRow_returnsTrue();
  void shouldTreatAsNewRow_rowBoundary();

  // shouldAnimateHorizontalHop
  void shouldAnimateHorizontalHop_negativeFromIndex_returnsFalse();
  void shouldAnimateHorizontalHop_zeroGridWidth_returnsFalse();
  void shouldAnimateHorizontalHop_adjacentSameRow_returnsFalse();
  void shouldAnimateHorizontalHop_skipSameRow_returnsTrue();
  void shouldAnimateHorizontalHop_skipBackwardSameRow_returnsTrue();
  void shouldAnimateHorizontalHop_differentRow_returnsFalse();
  void shouldAnimateHorizontalHop_sameIndex_returnsFalse();

  // isNewRow
  void isNewRow_zeroGridWidth_returnsFalse();
  void isNewRow_currentNegative_newPositive_returnsTrue();
  void isNewRow_sameRow_returnsFalse();
  void isNewRow_differentRow_returnsTrue();

  // hopDirection
  void hopDirection_targetGreater_returnsPositiveOne();
  void hopDirection_targetLess_returnsNegativeOne();
  void hopDirection_targetEqual_returnsZero();

  // hopStepCount
  void hopStepCount_sameIndex_returnsZero();
  void hopStepCount_returnsAbsoluteDelta();

  // hopIntermediateIndex
  void hopIntermediateIndex_zeroSpan_returnsMinusOne();
  void hopIntermediateIndex_outOfRangeStep_returnsMinusOne();
  void hopIntermediateIndex_forwardWalk_advancesByOnePerStep();
  void hopIntermediateIndex_backwardWalk_advancesByOnePerStep();
  void hopIntermediateIndex_finalStep_returnsTarget();

  // isRowChangePendingValid
  void rowChangePending_negativeIndex_returnsFalse();
  void rowChangePending_nonPositiveInterval_returnsFalse();
  void rowChangePending_withinWindow_returnsTrue();
  void rowChangePending_atBoundary_returnsTrue();
  void rowChangePending_pastWindow_returnsFalse();
  void rowChangePending_negativeDelta_returnsFalse();
};

void TestSelectionHelpers::shouldTreatAsNewRow_zeroGridWidth_returnsFalse() {
  QVERIFY(!SelectionHelpers::shouldTreatAsNewRow(5, 0, 0));
}

void TestSelectionHelpers::shouldTreatAsNewRow_negativeGridWidth_returnsFalse() {
  QVERIFY(!SelectionHelpers::shouldTreatAsNewRow(5, 0, -3));
}

void TestSelectionHelpers::shouldTreatAsNewRow_lastRowNegative_returnsTrue() {
  QVERIFY(SelectionHelpers::shouldTreatAsNewRow(0, -1, 6));
  QVERIFY(SelectionHelpers::shouldTreatAsNewRow(17, -1, 6));
}

void TestSelectionHelpers::shouldTreatAsNewRow_sameRow_returnsFalse() {
  // index 7 -> row 1 with gridWidth 6
  QVERIFY(!SelectionHelpers::shouldTreatAsNewRow(7, 1, 6));
}

void TestSelectionHelpers::shouldTreatAsNewRow_differentRow_returnsTrue() {
  // index 13 -> row 2 with gridWidth 6, last was row 1
  QVERIFY(SelectionHelpers::shouldTreatAsNewRow(13, 1, 6));
}

void TestSelectionHelpers::shouldTreatAsNewRow_rowBoundary() {
  // gridWidth 6: index 5 -> row 0, index 6 -> row 1
  QVERIFY(!SelectionHelpers::shouldTreatAsNewRow(5, 0, 6));
  QVERIFY(SelectionHelpers::shouldTreatAsNewRow(6, 0, 6));
}

void TestSelectionHelpers::shouldAnimateHorizontalHop_negativeFromIndex_returnsFalse() {
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(-1, 5, 6));
}

void TestSelectionHelpers::shouldAnimateHorizontalHop_zeroGridWidth_returnsFalse() {
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(2, 5, 0));
}

void TestSelectionHelpers::shouldAnimateHorizontalHop_adjacentSameRow_returnsFalse() {
  // Both on row 0, adjacent
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(2, 3, 6));
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(3, 2, 6));
}

void TestSelectionHelpers::shouldAnimateHorizontalHop_skipSameRow_returnsTrue() {
  // Both on row 0, skipping
  QVERIFY(SelectionHelpers::shouldAnimateHorizontalHop(0, 3, 6));
  QVERIFY(SelectionHelpers::shouldAnimateHorizontalHop(1, 5, 6));
}

void TestSelectionHelpers::shouldAnimateHorizontalHop_skipBackwardSameRow_returnsTrue() {
  QVERIFY(SelectionHelpers::shouldAnimateHorizontalHop(5, 1, 6));
}

void TestSelectionHelpers::shouldAnimateHorizontalHop_differentRow_returnsFalse() {
  // 2 -> 8 = row 0 to row 1
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(2, 8, 6));
}

void TestSelectionHelpers::shouldAnimateHorizontalHop_sameIndex_returnsFalse() {
  // |delta| == 0, not > 1
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(3, 3, 6));
}

void TestSelectionHelpers::isNewRow_zeroGridWidth_returnsFalse() {
  QVERIFY(!SelectionHelpers::isNewRow(0, 5, 0));
  QVERIFY(!SelectionHelpers::isNewRow(0, 5, -1));
}

void TestSelectionHelpers::isNewRow_currentNegative_newPositive_returnsTrue() {
  QVERIFY(SelectionHelpers::isNewRow(-1, 0, 6));
  QVERIFY(SelectionHelpers::isNewRow(-1, 5, 6));
}

void TestSelectionHelpers::isNewRow_sameRow_returnsFalse() {
  QVERIFY(!SelectionHelpers::isNewRow(2, 5, 6));
  QVERIFY(!SelectionHelpers::isNewRow(7, 11, 6));
}

void TestSelectionHelpers::isNewRow_differentRow_returnsTrue() {
  QVERIFY(SelectionHelpers::isNewRow(2, 8, 6));
  QVERIFY(SelectionHelpers::isNewRow(11, 5, 6));
}

// ------------------------------ hopDirection -------------------------------

void TestSelectionHelpers::hopDirection_targetGreater_returnsPositiveOne() {
  QCOMPARE(SelectionHelpers::hopDirection(2, 5), 1);
}

void TestSelectionHelpers::hopDirection_targetLess_returnsNegativeOne() {
  QCOMPARE(SelectionHelpers::hopDirection(5, 2), -1);
}

void TestSelectionHelpers::hopDirection_targetEqual_returnsZero() {
  QCOMPARE(SelectionHelpers::hopDirection(3, 3), 0);
}

// ------------------------------ hopStepCount -------------------------------

void TestSelectionHelpers::hopStepCount_sameIndex_returnsZero() {
  QCOMPARE(SelectionHelpers::hopStepCount(4, 4), 0);
}

void TestSelectionHelpers::hopStepCount_returnsAbsoluteDelta() {
  QCOMPARE(SelectionHelpers::hopStepCount(2, 7), 5);
  QCOMPARE(SelectionHelpers::hopStepCount(7, 2), 5);
}

// ------------------------------ hopIntermediateIndex -----------------------

void TestSelectionHelpers::hopIntermediateIndex_zeroSpan_returnsMinusOne() {
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(3, 3, 1), -1);
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(3, 3, 0), -1);
}

void TestSelectionHelpers::hopIntermediateIndex_outOfRangeStep_returnsMinusOne() {
  // Span 4 (from=2, to=6) -> valid steps are 1..4. 0 and 5 are invalid.
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(2, 6, 0), -1);
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(2, 6, 5), -1);
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(2, 6, -1), -1);
}

void TestSelectionHelpers::hopIntermediateIndex_forwardWalk_advancesByOnePerStep() {
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(2, 6, 1), 3);
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(2, 6, 2), 4);
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(2, 6, 3), 5);
}

void TestSelectionHelpers::hopIntermediateIndex_backwardWalk_advancesByOnePerStep() {
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(6, 2, 1), 5);
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(6, 2, 2), 4);
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(6, 2, 3), 3);
}

void TestSelectionHelpers::hopIntermediateIndex_finalStep_returnsTarget() {
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(2, 6, 4), 6);
  QCOMPARE(SelectionHelpers::hopIntermediateIndex(6, 2, 4), 2);
}

// ------------------------------ isRowChangePendingValid --------------------

void TestSelectionHelpers::rowChangePending_negativeIndex_returnsFalse() {
  QVERIFY(!SelectionHelpers::isRowChangePendingValid(-1, 1000, 1100, 500));
}

void TestSelectionHelpers::rowChangePending_nonPositiveInterval_returnsFalse() {
  QVERIFY(!SelectionHelpers::isRowChangePendingValid(3, 1000, 1100, 0));
  QVERIFY(!SelectionHelpers::isRowChangePendingValid(3, 1000, 1100, -50));
}

void TestSelectionHelpers::rowChangePending_withinWindow_returnsTrue() {
  // delta = 100, interval = 500 -> within.
  QVERIFY(SelectionHelpers::isRowChangePendingValid(3, 1000, 1100, 500));
}

void TestSelectionHelpers::rowChangePending_atBoundary_returnsTrue() {
  // delta == interval is inclusive (the production check uses <=).
  QVERIFY(SelectionHelpers::isRowChangePendingValid(3, 1000, 1500, 500));
}

void TestSelectionHelpers::rowChangePending_pastWindow_returnsFalse() {
  QVERIFY(!SelectionHelpers::isRowChangePendingValid(3, 1000, 1501, 500));
}

void TestSelectionHelpers::rowChangePending_negativeDelta_returnsFalse() {
  // Clock skew (now < pending) — guard against returning a stale-but-tiny-delta true.
  QVERIFY(!SelectionHelpers::isRowChangePendingValid(3, 2000, 1500, 500));
}

QTEST_APPLESS_MAIN(TestSelectionHelpers)
#include "test_selectionhelpers.moc"
