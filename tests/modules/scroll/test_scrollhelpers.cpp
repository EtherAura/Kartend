#include <QTest>

#include "scrollhelpers.h"

class TestScrollHelpers : public QObject {
  Q_OBJECT

private slots:
  // movementDirection
  void movementDirection_negativePrev_returnsFalse();
  void movementDirection_zeroItemsPerRow_returnsFalse();
  void movementDirection_negativeItemsPerRow_returnsFalse();
  void movementDirection_sameRow_returnsTrue();
  void movementDirection_sameRowWithSkip_returnsTrue();
  void movementDirection_differentRowNonAdjacent_returnsFalse();
  void movementDirection_singleColumnGrid_returnsFalse();
  void movementDirection_wrapForward_returnsTrue();
  void movementDirection_wrapBackward_returnsTrue();
  void movementDirection_adjacentDifferentRowNoWrap_returnsFalse();

  // effectiveAlignment
  void effectiveAlignment_unfiltered_returnsRequested();
  void effectiveAlignment_filteredEmpty_returnsRequested();
  void effectiveAlignment_filteredZeroItemsPerRow_returnsRequested();
  void effectiveAlignment_filteredFewItems_returnsCenter();
  void effectiveAlignment_filteredManyItems_returnsRequested();
  void effectiveAlignment_filteredBoundary_returnsRequested();
};

void TestScrollHelpers::movementDirection_negativePrev_returnsFalse() {
  QVERIFY(!ScrollHelpers::movementDirection(5, -1, 6));
}

void TestScrollHelpers::movementDirection_zeroItemsPerRow_returnsFalse() {
  QVERIFY(!ScrollHelpers::movementDirection(5, 4, 0));
}

void TestScrollHelpers::movementDirection_negativeItemsPerRow_returnsFalse() {
  QVERIFY(!ScrollHelpers::movementDirection(5, 4, -3));
}

void TestScrollHelpers::movementDirection_sameRow_returnsTrue() {
  QVERIFY(ScrollHelpers::movementDirection(2, 1, 6));
  QVERIFY(ScrollHelpers::movementDirection(8, 9, 6));
}

void TestScrollHelpers::movementDirection_sameRowWithSkip_returnsTrue() {
  QVERIFY(ScrollHelpers::movementDirection(5, 1, 6));
  QVERIFY(ScrollHelpers::movementDirection(0, 4, 6));
}

void TestScrollHelpers::
    movementDirection_differentRowNonAdjacent_returnsFalse() {
  // 0 -> 12: row 0 -> row 2, |delta| = 12
  QVERIFY(!ScrollHelpers::movementDirection(12, 0, 6));
}

void TestScrollHelpers::movementDirection_singleColumnGrid_returnsFalse() {
  // itemsPerRow == 1: every move is a row change; wrap detection disabled
  QVERIFY(!ScrollHelpers::movementDirection(2, 1, 1));
}

void TestScrollHelpers::movementDirection_wrapForward_returnsTrue() {
  // gridWidth 6: index 5 (col 5, row 0) -> index 6 (col 0, row 1)
  QVERIFY(ScrollHelpers::movementDirection(6, 5, 6));
}

void TestScrollHelpers::movementDirection_wrapBackward_returnsTrue() {
  // gridWidth 6: index 6 (col 0, row 1) -> index 5 (col 5, row 0)
  QVERIFY(ScrollHelpers::movementDirection(5, 6, 6));
}

void TestScrollHelpers::
    movementDirection_adjacentDifferentRowNoWrap_returnsFalse() {
  // gridWidth 4: index 4 (col 0, row 1) -> index 3 (col 3, row 0).
  // Adjacent (|delta|==1) AND wrap (prevCol 0 -> currCol 3) -> isHorizontal.
  QVERIFY(ScrollHelpers::movementDirection(3, 4, 4));
  // gridWidth 4: index 3 (col 3) -> index 4 (col 0): wrap forward
  QVERIFY(ScrollHelpers::movementDirection(4, 3, 4));
}

void TestScrollHelpers::effectiveAlignment_unfiltered_returnsRequested() {
  QCOMPARE(ScrollHelpers::effectiveAlignment(HorizontalAlignment::Left, false,
                                             5, 8),
           HorizontalAlignment::Left);
  QCOMPARE(ScrollHelpers::effectiveAlignment(HorizontalAlignment::Right, false,
                                             1, 8),
           HorizontalAlignment::Right);
}

void TestScrollHelpers::effectiveAlignment_filteredEmpty_returnsRequested() {
  // totalItems == 0: helper returns requested unchanged
  QCOMPARE(
      ScrollHelpers::effectiveAlignment(HorizontalAlignment::Left, true, 0, 8),
      HorizontalAlignment::Left);
}

void TestScrollHelpers::
    effectiveAlignment_filteredZeroItemsPerRow_returnsRequested() {
  QCOMPARE(
      ScrollHelpers::effectiveAlignment(HorizontalAlignment::Left, true, 3, 0),
      HorizontalAlignment::Left);
}

void TestScrollHelpers::effectiveAlignment_filteredFewItems_returnsCenter() {
  // itemsPerRow 8 -> threshold is 6 (8 - 2). totalItems 5 < 6 -> Center.
  QCOMPARE(
      ScrollHelpers::effectiveAlignment(HorizontalAlignment::Left, true, 5, 8),
      HorizontalAlignment::Center);
}

void TestScrollHelpers::effectiveAlignment_filteredManyItems_returnsRequested() {
  // totalItems 7 >= threshold 6 -> requested unchanged
  QCOMPARE(
      ScrollHelpers::effectiveAlignment(HorizontalAlignment::Right, true, 7, 8),
      HorizontalAlignment::Right);
}

void TestScrollHelpers::effectiveAlignment_filteredBoundary_returnsRequested() {
  // totalItems == itemsPerRow - 2 (boundary, NOT less-than) -> requested
  QCOMPARE(
      ScrollHelpers::effectiveAlignment(HorizontalAlignment::Right, true, 6, 8),
      HorizontalAlignment::Right);
}

QTEST_APPLESS_MAIN(TestScrollHelpers)
#include "test_scrollhelpers.moc"
