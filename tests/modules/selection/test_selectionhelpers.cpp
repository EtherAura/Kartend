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

void TestSelectionHelpers::
    shouldAnimateHorizontalHop_negativeFromIndex_returnsFalse() {
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(-1, 5, 6));
}

void TestSelectionHelpers::
    shouldAnimateHorizontalHop_zeroGridWidth_returnsFalse() {
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(2, 5, 0));
}

void TestSelectionHelpers::
    shouldAnimateHorizontalHop_adjacentSameRow_returnsFalse() {
  // Both on row 0, adjacent
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(2, 3, 6));
  QVERIFY(!SelectionHelpers::shouldAnimateHorizontalHop(3, 2, 6));
}

void TestSelectionHelpers::
    shouldAnimateHorizontalHop_skipSameRow_returnsTrue() {
  // Both on row 0, skipping
  QVERIFY(SelectionHelpers::shouldAnimateHorizontalHop(0, 3, 6));
  QVERIFY(SelectionHelpers::shouldAnimateHorizontalHop(1, 5, 6));
}

void TestSelectionHelpers::
    shouldAnimateHorizontalHop_skipBackwardSameRow_returnsTrue() {
  QVERIFY(SelectionHelpers::shouldAnimateHorizontalHop(5, 1, 6));
}

void TestSelectionHelpers::
    shouldAnimateHorizontalHop_differentRow_returnsFalse() {
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

QTEST_APPLESS_MAIN(TestSelectionHelpers)
#include "test_selectionhelpers.moc"
