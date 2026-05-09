// Tests for ViewportHelpers — pure helpers extracted from ViewportManager.
//
// Covers the small-movement threshold (same-row vs cross-row) and the
// "force immediate center" decision combiner. ViewportManager itself
// can't be unit-tested in isolation because every centering path touches
// AnimationManager / ScrollManager / SelectionManager / a real
// QScrollArea, but these two pure decisions drive the full path and are
// the highest-leverage things to lock in.

#include <QTest>

#include "viewporthelpers.h"

class TestViewportHelpers : public QObject {
  Q_OBJECT
private slots:
  // smallMovementThreshold
  void smallMovementSameRowReturnsLargerThreshold();
  void smallMovementDifferentRowReturnsSmallerThreshold();
  void smallMovementNoLastRowReturnsSmallerThreshold();
  void smallMovementZeroRowSameAsLastIsSameRow();

  // computeForceImmediate
  void forceImmediateAllFalseIsFalse();
  void forceImmediateImmediateFlagWins();
  void forceImmediateForceCenterWins();
  void forceImmediateWrappingNavigationWins();
  void forceImmediateRestoringSelectionWins();
  void forceImmediateInstantPositioningWins();
  void forceImmediateWrapSequenceActiveWins();
  void forceImmediateMultipleTrueStillTrue();
};

// --------------------------- smallMovementThreshold -------------------------

void TestViewportHelpers::smallMovementSameRowReturnsLargerThreshold() {
  // Same row -> 8 (lenient: don't recenter for in-row hops).
  QCOMPARE(ViewportHelpers::smallMovementThreshold(/*lastRow=*/3, /*currentRow=*/3), 8);
}

void TestViewportHelpers::smallMovementDifferentRowReturnsSmallerThreshold() {
  // Different row -> 2 (strict: recenter on row changes).
  QCOMPARE(ViewportHelpers::smallMovementThreshold(/*lastRow=*/3, /*currentRow=*/4), 2);
  QCOMPARE(ViewportHelpers::smallMovementThreshold(/*lastRow=*/10, /*currentRow=*/0), 2);
}

void TestViewportHelpers::smallMovementNoLastRowReturnsSmallerThreshold() {
  // No prior row known (lastRow == -1) -> use cross-row threshold.
  QCOMPARE(ViewportHelpers::smallMovementThreshold(/*lastRow=*/-1, /*currentRow=*/3), 2);
  QCOMPARE(ViewportHelpers::smallMovementThreshold(/*lastRow=*/-1, /*currentRow=*/0), 2);
}

void TestViewportHelpers::smallMovementZeroRowSameAsLastIsSameRow() {
  // Edge case: row 0 vs lastRow 0 — `lastRow >= 0` lets row 0 qualify.
  QCOMPARE(ViewportHelpers::smallMovementThreshold(/*lastRow=*/0, /*currentRow=*/0), 8);
}

// --------------------------- computeForceImmediate --------------------------

void TestViewportHelpers::forceImmediateAllFalseIsFalse() {
  QCOMPARE(ViewportHelpers::computeForceImmediate(false, false, false, false, false, false), false);
}

void TestViewportHelpers::forceImmediateImmediateFlagWins() {
  QCOMPARE(ViewportHelpers::computeForceImmediate(true, false, false, false, false, false), true);
}

void TestViewportHelpers::forceImmediateForceCenterWins() {
  QCOMPARE(ViewportHelpers::computeForceImmediate(false, true, false, false, false, false), true);
}

void TestViewportHelpers::forceImmediateWrappingNavigationWins() {
  QCOMPARE(ViewportHelpers::computeForceImmediate(false, false, true, false, false, false), true);
}

void TestViewportHelpers::forceImmediateRestoringSelectionWins() {
  QCOMPARE(ViewportHelpers::computeForceImmediate(false, false, false, true, false, false), true);
}

void TestViewportHelpers::forceImmediateInstantPositioningWins() {
  QCOMPARE(ViewportHelpers::computeForceImmediate(false, false, false, false, true, false), true);
}

void TestViewportHelpers::forceImmediateWrapSequenceActiveWins() {
  QCOMPARE(ViewportHelpers::computeForceImmediate(false, false, false, false, false, true), true);
}

void TestViewportHelpers::forceImmediateMultipleTrueStillTrue() {
  QCOMPARE(ViewportHelpers::computeForceImmediate(true, true, false, true, false, true), true);
}

QTEST_APPLESS_MAIN(TestViewportHelpers)
#include "test_viewporthelpers.moc"
