// Tests for MouseHelpers — pure helpers extracted from MouseManager.
//
// Covers wheel-step accumulation (angle and pixel deltas with sub-step
// guarantees) and the viewport-based vertical scroll direction decision.

#include <QTest>

#include "mousehelpers.h"

class TestMouseHelpers : public QObject {
  Q_OBJECT
private slots:
  // wheel steps from angle delta
  void wheelAngleZeroReturnsZero();
  void wheelAngleInvalidStepReturnsZero();
  void wheelAngleExactStep();
  void wheelAngleMultipleSteps();
  void wheelAngleSubStepClampsToOne();
  void wheelAngleNegativeSubStepClampsToMinusOne();
  void wheelAngleNegativeMultipleSteps();

  // wheel steps from pixel delta
  void wheelPixelZeroReturnsZero();
  void wheelPixelInvalidStepReturnsZero();
  void wheelPixelExactStep();
  void wheelPixelSubStepClampsToOne();
  void wheelPixelNegativeSubStepClampsToMinusOne();

  // verticalScrollDirection
  void verticalNearTopEdgeReturnsMinusOne();
  void verticalNearBottomEdgeReturnsPlusOne();
  void verticalAboveCenterReturnsMinusOne();
  void verticalBelowCenterReturnsPlusOne();
  void verticalAtCenterReturnsZero();

  // computeHorizontalCandidate
  void horizontalCandidateNoMovementIsIneligible();
  void horizontalCandidateNegativeIndicesIneligible();
  void horizontalCandidateZeroGridWidthIneligible();
  void horizontalCandidateDifferentRowIneligible();
  void horizontalCandidateRightwardSameRowEligible();
  void horizontalCandidateLeftwardSameRowEligible();
  void horizontalCandidateLastColumnFirstColumnNextRowIsDifferentRow();
};

// --------------------------- wheel angle ------------------------------------

void TestMouseHelpers::wheelAngleZeroReturnsZero() {
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(0, 120), 0);
}

void TestMouseHelpers::wheelAngleInvalidStepReturnsZero() {
  // A non-positive step is defensive — caller bug. Must not divide by zero.
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(120, 0), 0);
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(120, -1), 0);
}

void TestMouseHelpers::wheelAngleExactStep() {
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(120, 120), 1);
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(-120, 120), -1);
}

void TestMouseHelpers::wheelAngleMultipleSteps() {
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(360, 120), 3);
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(240, 120), 2);
}

void TestMouseHelpers::wheelAngleSubStepClampsToOne() {
  // Sub-step trackpad gestures (delta < step) must still produce a step.
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(1, 120), 1);
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(50, 120), 1);
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(119, 120), 1);
}

void TestMouseHelpers::wheelAngleNegativeSubStepClampsToMinusOne() {
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(-1, 120), -1);
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(-50, 120), -1);
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(-119, 120), -1);
}

void TestMouseHelpers::wheelAngleNegativeMultipleSteps() {
  QCOMPARE(MouseHelpers::wheelStepsFromAngle(-360, 120), -3);
}

// --------------------------- wheel pixel ------------------------------------

void TestMouseHelpers::wheelPixelZeroReturnsZero() {
  QCOMPARE(MouseHelpers::wheelStepsFromPixel(0, 120), 0);
}

void TestMouseHelpers::wheelPixelInvalidStepReturnsZero() {
  QCOMPARE(MouseHelpers::wheelStepsFromPixel(120, 0), 0);
  QCOMPARE(MouseHelpers::wheelStepsFromPixel(120, -1), 0);
}

void TestMouseHelpers::wheelPixelExactStep() {
  QCOMPARE(MouseHelpers::wheelStepsFromPixel(120, 120), 1);
  QCOMPARE(MouseHelpers::wheelStepsFromPixel(240, 120), 2);
}

void TestMouseHelpers::wheelPixelSubStepClampsToOne() {
  QCOMPARE(MouseHelpers::wheelStepsFromPixel(1, 120), 1);
  QCOMPARE(MouseHelpers::wheelStepsFromPixel(50, 120), 1);
}

void TestMouseHelpers::wheelPixelNegativeSubStepClampsToMinusOne() {
  QCOMPARE(MouseHelpers::wheelStepsFromPixel(-1, 120), -1);
  QCOMPARE(MouseHelpers::wheelStepsFromPixel(-50, 120), -1);
}

// --------------------------- verticalScrollDirection ------------------------

void TestMouseHelpers::verticalNearTopEdgeReturnsMinusOne() {
  // Item at y=50, viewport top=0, bottom=600, rowHeight=100.
  // top + rowHeight = 100, item (50) is below that boundary so triggers
  // "near top" branch -> -1.
  QCOMPARE(MouseHelpers::verticalScrollDirection(/*itemY=*/50,
                                                 /*top=*/0, /*bottom=*/600,
                                                 /*center=*/300,
                                                 /*rowHeight=*/100),
           -1);
}

void TestMouseHelpers::verticalNearBottomEdgeReturnsPlusOne() {
  // Item at y=550, viewport bottom=600, rowHeight=100.
  // bottom - rowHeight = 500, item (550) is above that boundary so
  // "near bottom" branch -> +1.
  QCOMPARE(MouseHelpers::verticalScrollDirection(/*itemY=*/550,
                                                 /*top=*/0, /*bottom=*/600,
                                                 /*center=*/300,
                                                 /*rowHeight=*/100),
           1);
}

void TestMouseHelpers::verticalAboveCenterReturnsMinusOne() {
  // Item between rowHeight margins but above center. top=0, bottom=600,
  // rowHeight=50, center=300, item=200.
  QCOMPARE(MouseHelpers::verticalScrollDirection(/*itemY=*/200,
                                                 /*top=*/0, /*bottom=*/600,
                                                 /*center=*/300,
                                                 /*rowHeight=*/50),
           -1);
}

void TestMouseHelpers::verticalBelowCenterReturnsPlusOne() {
  QCOMPARE(MouseHelpers::verticalScrollDirection(/*itemY=*/400,
                                                 /*top=*/0, /*bottom=*/600,
                                                 /*center=*/300,
                                                 /*rowHeight=*/50),
           1);
}

void TestMouseHelpers::verticalAtCenterReturnsZero() {
  QCOMPARE(MouseHelpers::verticalScrollDirection(/*itemY=*/300,
                                                 /*top=*/0, /*bottom=*/600,
                                                 /*center=*/300,
                                                 /*rowHeight=*/50),
           0);
}

// --------------------------- computeHorizontalCandidate ---------------------

void TestMouseHelpers::horizontalCandidateNoMovementIsIneligible() {
  const auto c = MouseHelpers::computeHorizontalCandidate(/*prev=*/3, /*target=*/3,
                                                          /*gridWidth=*/5);
  QVERIFY(!c.eligible);
  QCOMPARE(c.direction, 0);
  QCOMPARE(c.startIndex, -1);
}

void TestMouseHelpers::horizontalCandidateNegativeIndicesIneligible() {
  const auto a = MouseHelpers::computeHorizontalCandidate(/*prev=*/-1, /*target=*/3,
                                                          /*gridWidth=*/5);
  QVERIFY(!a.eligible);
  const auto b = MouseHelpers::computeHorizontalCandidate(/*prev=*/3, /*target=*/-1,
                                                          /*gridWidth=*/5);
  QVERIFY(!b.eligible);
}

void TestMouseHelpers::horizontalCandidateZeroGridWidthIneligible() {
  const auto a = MouseHelpers::computeHorizontalCandidate(/*prev=*/3, /*target=*/4,
                                                          /*gridWidth=*/0);
  QVERIFY(!a.eligible);
  const auto b = MouseHelpers::computeHorizontalCandidate(/*prev=*/3, /*target=*/4,
                                                          /*gridWidth=*/-2);
  QVERIFY(!b.eligible);
}

void TestMouseHelpers::horizontalCandidateDifferentRowIneligible() {
  // gridWidth=5: prev row 0 (idx 3), target row 1 (idx 7) -> not same row.
  const auto c = MouseHelpers::computeHorizontalCandidate(/*prev=*/3, /*target=*/7,
                                                          /*gridWidth=*/5);
  QVERIFY(!c.eligible);
  QCOMPARE(c.direction, 0);
}

void TestMouseHelpers::horizontalCandidateRightwardSameRowEligible() {
  // gridWidth=5: prev=2 (row 0), target=4 (row 0).
  const auto c = MouseHelpers::computeHorizontalCandidate(/*prev=*/2, /*target=*/4,
                                                          /*gridWidth=*/5);
  QVERIFY(c.eligible);
  QCOMPARE(c.direction, 1);
  QCOMPARE(c.startIndex, 4);
}

void TestMouseHelpers::horizontalCandidateLeftwardSameRowEligible() {
  // gridWidth=5: prev=8 (row 1), target=6 (row 1).
  const auto c = MouseHelpers::computeHorizontalCandidate(/*prev=*/8, /*target=*/6,
                                                          /*gridWidth=*/5);
  QVERIFY(c.eligible);
  QCOMPARE(c.direction, -1);
  QCOMPARE(c.startIndex, 6);
}

void TestMouseHelpers::horizontalCandidateLastColumnFirstColumnNextRowIsDifferentRow() {
  // gridWidth=5: idx 4 = row 0, last column. idx 5 = row 1, first column.
  // The "wrap to next row" case must NOT be eligible for horizontal hold.
  const auto c = MouseHelpers::computeHorizontalCandidate(/*prev=*/4, /*target=*/5,
                                                          /*gridWidth=*/5);
  QVERIFY(!c.eligible);
}

QTEST_APPLESS_MAIN(TestMouseHelpers)
#include "test_mousehelpers.moc"
