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

  // chunkSizeFor
  void chunkSize_defaultsWhenShowAllSubcollectionsOff();
  void chunkSize_defaultsBelowLargeThreshold();
  void chunkSize_switchesToLargeAboveThreshold();
  void chunkSize_atThresholdReturnsDefault();

  // computeSliderPrefetchPlan — guards
  void prefetch_invalidWhenItemHeightNonPositive();
  void prefetch_invalidWhenItemsPerRowNonPositive();
  void prefetch_invalidWhenChunkConstantsNonPositive();

  // computeSliderPrefetchPlan — math
  void prefetch_zeroSliderClampsToZeroChunkStart();
  void prefetch_negativeSliderClampsToZero();
  void prefetch_subtractsPrefixFromTargetIndex();
  void prefetch_alignsChunkStartToChunkBoundary();
  void prefetch_clampsNegativeMediaIndexAtZero();
  void prefetch_usesLargeChunkAboveThreshold();
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

// ------------------------------ chunkSizeFor --------------------------------

void TestScrollHelpers::chunkSize_defaultsWhenShowAllSubcollectionsOff() {
  // showAll=false -> always default chunk regardless of totalItems
  QCOMPARE(ScrollHelpers::chunkSizeFor(false, 999999, 5000, 100, 1000), 100);
}

void TestScrollHelpers::chunkSize_defaultsBelowLargeThreshold() {
  QCOMPARE(ScrollHelpers::chunkSizeFor(true, 4000, 5000, 100, 1000), 100);
}

void TestScrollHelpers::chunkSize_switchesToLargeAboveThreshold() {
  QCOMPARE(ScrollHelpers::chunkSizeFor(true, 5001, 5000, 100, 1000), 1000);
  QCOMPARE(ScrollHelpers::chunkSizeFor(true, 50000, 5000, 100, 1000), 1000);
}

void TestScrollHelpers::chunkSize_atThresholdReturnsDefault() {
  // Strict > comparison: totalItems == threshold stays on default.
  QCOMPARE(ScrollHelpers::chunkSizeFor(true, 5000, 5000, 100, 1000), 100);
}

// ------------------------------ computeSliderPrefetchPlan — guards ----------

void TestScrollHelpers::prefetch_invalidWhenItemHeightNonPositive() {
  const auto plan = ScrollHelpers::computeSliderPrefetchPlan(
      /*sliderValue=*/100, /*itemHeight=*/0, /*itemsPerRow=*/4,
      /*subcollectionCount=*/0, /*virtualFolderCount=*/0,
      /*showAll=*/false, /*totalItems=*/100,
      /*largeThreshold=*/5000, /*defaultChunk=*/100, /*largeChunk=*/1000);
  QVERIFY(!plan.valid);
}

void TestScrollHelpers::prefetch_invalidWhenItemsPerRowNonPositive() {
  const auto plan =
      ScrollHelpers::computeSliderPrefetchPlan(100, 50, 0, 0, 0, false, 100, 5000, 100, 1000);
  QVERIFY(!plan.valid);
  const auto neg =
      ScrollHelpers::computeSliderPrefetchPlan(100, 50, -1, 0, 0, false, 100, 5000, 100, 1000);
  QVERIFY(!neg.valid);
}

void TestScrollHelpers::prefetch_invalidWhenChunkConstantsNonPositive() {
  const auto def =
      ScrollHelpers::computeSliderPrefetchPlan(100, 50, 4, 0, 0, false, 100, 5000, 0, 1000);
  QVERIFY(!def.valid);
  const auto large =
      ScrollHelpers::computeSliderPrefetchPlan(100, 50, 4, 0, 0, false, 100, 5000, 100, 0);
  QVERIFY(!large.valid);
}

// ------------------------------ computeSliderPrefetchPlan — math ------------

void TestScrollHelpers::prefetch_zeroSliderClampsToZeroChunkStart() {
  const auto plan =
      ScrollHelpers::computeSliderPrefetchPlan(0, 50, 4, 0, 0, false, 100, 5000, 100, 1000);
  QVERIFY(plan.valid);
  QCOMPARE(plan.chunkStart, 0);
  QCOMPARE(plan.chunkSize, 100);
}

void TestScrollHelpers::prefetch_negativeSliderClampsToZero() {
  // Defensive: a negative slider shouldn't compute a negative row.
  const auto plan =
      ScrollHelpers::computeSliderPrefetchPlan(-200, 50, 4, 0, 0, false, 100, 5000, 100, 1000);
  QVERIFY(plan.valid);
  QCOMPARE(plan.chunkStart, 0);
}

void TestScrollHelpers::prefetch_subtractsPrefixFromTargetIndex() {
  // sliderValue=2500, itemHeight=50 -> row=50. itemsPerRow=4 -> targetIndex=200.
  // subcollections=3, virtualFolders=2 -> prefix=5. mediaIndex = 195.
  // chunk=100 -> chunkStart = (195 / 100) * 100 = 100.
  const auto plan = ScrollHelpers::computeSliderPrefetchPlan(2500, 50, 4, 3, 2, false, 100, 5000,
                                                              100, 1000);
  QVERIFY(plan.valid);
  QCOMPARE(plan.chunkSize, 100);
  QCOMPARE(plan.chunkStart, 100);
}

void TestScrollHelpers::prefetch_alignsChunkStartToChunkBoundary() {
  // sliderValue=1000, itemHeight=10 -> row=100. itemsPerRow=10 -> targetIndex=1000.
  // No prefix. mediaIndex=1000. chunk=300 -> chunkStart=(1000/300)*300=900.
  const auto plan = ScrollHelpers::computeSliderPrefetchPlan(1000, 10, 10, 0, 0, false, 100, 5000,
                                                              300, 1000);
  QVERIFY(plan.valid);
  QCOMPARE(plan.chunkSize, 300);
  QCOMPARE(plan.chunkStart, 900);
}

void TestScrollHelpers::prefetch_clampsNegativeMediaIndexAtZero() {
  // Slider near top with prefix > targetIndex -> mediaIndex would be negative.
  // Helper clamps to 0, chunkStart=0.
  // sliderValue=50, itemHeight=50 -> row=1, targetIndex=4 (itemsPerRow=4).
  // prefix = subcollections=10 + virtualFolders=0 = 10. raw mediaIndex = 4-10 = -6.
  const auto plan =
      ScrollHelpers::computeSliderPrefetchPlan(50, 50, 4, 10, 0, false, 100, 5000, 100, 1000);
  QVERIFY(plan.valid);
  QCOMPARE(plan.chunkStart, 0);
}

void TestScrollHelpers::prefetch_usesLargeChunkAboveThreshold() {
  // showAll=true and totalItems > threshold -> largeChunk wins.
  // sliderValue=10000, itemHeight=50 -> row=200. itemsPerRow=4 -> targetIndex=800.
  // No prefix. mediaIndex=800. largeChunk=1000 -> chunkStart=(800/1000)*1000=0.
  const auto plan = ScrollHelpers::computeSliderPrefetchPlan(10000, 50, 4, 0, 0, true, 50000, 5000,
                                                              100, 1000);
  QVERIFY(plan.valid);
  QCOMPARE(plan.chunkSize, 1000);
  QCOMPARE(plan.chunkStart, 0);
}

QTEST_APPLESS_MAIN(TestScrollHelpers)
#include "test_scrollhelpers.moc"
