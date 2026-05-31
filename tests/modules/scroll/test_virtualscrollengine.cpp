// Unit tests for VirtualScrollEngine's visible-range math (Kartend-tacgk).
//
// virtualscrollengine.cpp drives every large-collection scroll but had zero
// tests — its visible-range math, recycle decisions, and edge clamping were
// unguarded. The engine itself needs a live ScrollManager + widget viewport,
// so the pure arithmetic was extracted into VirtualScrollMath::neededIndexBand
// (overscan + clamp -> the index band of widgets to materialize). The
// scroll-offset -> visible-row mapping it consumes already lives in the pure,
// tested GridLayoutCalculator::getVisibleRowRange; the cases the issue called
// out (item taller than the viewport, fractional scroll offsets) are exercised
// against it here too.
#include <utility>

#include <QTest>

#include "gridlayoutcalculator.h"
#include "virtualscrollmath.h"

using VirtualScrollMath::IndexBand;
using VirtualScrollMath::neededIndexBand;

class TestVirtualScrollEngine : public QObject {
  Q_OBJECT
private slots:
  // neededIndexBand — band/overscan/clamp arithmetic.
  void neededIndexBand_zeroItems_isEmpty();
  void neededIndexBand_zeroColumns_isEmpty();
  void neededIndexBand_singleItem_isLoneIndex();
  void neededIndexBand_partialFinalRow_clampsToLastItem();
  void neededIndexBand_overscanClampedAtTopRow();
  void neededIndexBand_visibleRangePastCollection_isEmpty();
  void neededIndexBand_fullSpan_coversEveryItem();

  // getVisibleRowRange — scroll-offset -> visible-row mapping.
  void getVisibleRowRange_fractionalOffset_floorsToRow();
  void getVisibleRowRange_itemTallerThanViewport_showsOneRow();
};

void TestVirtualScrollEngine::neededIndexBand_zeroItems_isEmpty() {
  const IndexBand band = neededIndexBand(/*firstVisibleRow=*/0, /*lastVisibleRow=*/5,
                                         /*itemsPerRow=*/4, /*totalItems=*/0);
  QVERIFY(band.isEmpty());
  QCOMPARE(band.size(), 0);
}

void TestVirtualScrollEngine::neededIndexBand_zeroColumns_isEmpty() {
  // itemsPerRow <= 0 can happen mid-relayout before metrics are computed.
  const IndexBand band = neededIndexBand(0, 5, /*itemsPerRow=*/0, /*totalItems=*/10);
  QVERIFY(band.isEmpty());
}

void TestVirtualScrollEngine::neededIndexBand_singleItem_isLoneIndex() {
  const IndexBand band = neededIndexBand(0, 0, /*itemsPerRow=*/1, /*totalItems=*/1);
  QCOMPARE(band.firstIndex, 0);
  QCOMPARE(band.lastIndex, 0);
  QCOMPARE(band.size(), 1);
  QVERIFY(band.contains(0));
  QVERIFY(!band.contains(1));
}

void TestVirtualScrollEngine::neededIndexBand_partialFinalRow_clampsToLastItem() {
  // 10 items, 4 per row -> rows {0:0-3, 1:4-7, 2:8-9} (last row half full).
  // Only row 2 visible, no overscan: the band must stop at index 9, not 11.
  const IndexBand band = neededIndexBand(/*firstVisibleRow=*/2, /*lastVisibleRow=*/2,
                                         /*itemsPerRow=*/4, /*totalItems=*/10, /*overscanRows=*/0);
  QCOMPARE(band.firstIndex, 8);
  QCOMPARE(band.lastIndex, 9);
  QCOMPARE(band.size(), 2);
}

void TestVirtualScrollEngine::neededIndexBand_overscanClampedAtTopRow() {
  // firstVisibleRow 0 with overscan 1 must not produce a negative start row.
  const IndexBand band = neededIndexBand(/*firstVisibleRow=*/0, /*lastVisibleRow=*/1,
                                         /*itemsPerRow=*/4, /*totalItems=*/100, /*overscanRows=*/1);
  QCOMPARE(band.firstIndex, 0); // startRow clamped to 0, not -1
  QCOMPARE(band.lastIndex, 11); // rows 0..2 (1 + overscan) -> indices 0..11
}

void TestVirtualScrollEngine::neededIndexBand_visibleRangePastCollection_isEmpty() {
  // 12 items, 4 per row -> max row 2. A viewport scrolled to rows 5..100 lands
  // entirely past the collection, so nothing needs materializing.
  const IndexBand band = neededIndexBand(/*firstVisibleRow=*/5, /*lastVisibleRow=*/100,
                                         /*itemsPerRow=*/4, /*totalItems=*/12, /*overscanRows=*/1);
  QVERIFY(band.isEmpty());
}

void TestVirtualScrollEngine::neededIndexBand_fullSpan_coversEveryItem() {
  // A viewport covering every row clamps to the real last index (11), not the
  // padded row's nominal end.
  const IndexBand band = neededIndexBand(/*firstVisibleRow=*/0, /*lastVisibleRow=*/100,
                                         /*itemsPerRow=*/4, /*totalItems=*/12, /*overscanRows=*/0);
  QCOMPARE(band.firstIndex, 0);
  QCOMPARE(band.lastIndex, 11);
  QCOMPARE(band.size(), 12);
}

void TestVirtualScrollEngine::getVisibleRowRange_fractionalOffset_floorsToRow() {
  // Row stride 100px; scroll to 250px (2.5 rows down). The first visible row is
  // the one containing the offset — floor(250/100) == 2 — not 3.
  GridMetrics metrics;
  metrics.itemHeight = 100;
  metrics.verticalSpacing = 0;
  metrics.itemsPerRow = 1;
  metrics.totalRows = 10;

  const auto [first, last] = GridLayoutCalculator::getVisibleRowRange(
      /*scrollPos=*/250, /*viewportSize=*/100, metrics, /*bufferRows=*/0);
  QCOMPARE(first, 2);
  QVERIFY(last >= first);
  QVERIFY(last <= metrics.totalRows - 1);
}

void TestVirtualScrollEngine::getVisibleRowRange_itemTallerThanViewport_showsOneRow() {
  // A single item taller (1000px) than the viewport (300px) must still resolve
  // to exactly one visible row, never an empty range.
  GridMetrics metrics;
  metrics.itemHeight = 1000;
  metrics.verticalSpacing = 0;
  metrics.itemsPerRow = 1;
  metrics.totalRows = 5;

  const auto [first, last] = GridLayoutCalculator::getVisibleRowRange(
      /*scrollPos=*/0, /*viewportSize=*/300, metrics, /*bufferRows=*/0);
  QCOMPARE(first, 0);
  QCOMPARE(last, 0);
}

QTEST_MAIN(TestVirtualScrollEngine)
#include "test_virtualscrollengine.moc"
