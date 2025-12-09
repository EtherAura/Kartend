/**
 * @file test_gridutils.cpp
 * @brief Unit tests for GridUtils namespace functions
 *
 * Tests the grid layout utility functions used for item positioning.
 */

#include "gridutils.h"
#include <QTest>

class TestGridUtils : public QObject {
  Q_OBJECT

private slots:
  // computeItemRow tests
  void testComputeItemRow_firstRow();
  void testComputeItemRow_secondRow();
  void testComputeItemRow_zeroGridWidth();
  void testComputeItemRow_singleColumnGrid();

  // computeItemCol tests
  void testComputeItemCol_firstColumn();
  void testComputeItemCol_lastColumn();
  void testComputeItemCol_zeroGridWidth();
  void testComputeItemCol_wrapsToNextRow();

  // computeItemY tests
  void testComputeItemY_firstRow();
  void testComputeItemY_secondRow();
  void testComputeItemY_includesMargins();

  // computeItemX tests
  void testComputeItemX_firstColumn();
  void testComputeItemX_secondColumn();
  void testComputeItemX_includesMargins();

  // computeCenterTarget tests
  void testComputeCenterTarget_centeredItem();
  void testComputeCenterTarget_clampedToZero();
  void testComputeCenterTarget_clampedToMax();

  // calculateGridMetrics tests
  void testCalculateGridMetrics_basicGrid();
  void testCalculateGridMetrics_singleRow();
  void testCalculateGridMetrics_singleColumn();
  void testCalculateGridMetrics_emptyGrid();
  void testCalculateGridMetrics_minimumDimensions();
};

// ─────────────────────────────────────────────────────────────────────────────
// computeItemRow tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridUtils::testComputeItemRow_firstRow() {
  // Items 0-3 in a 4-wide grid are all in row 0
  QCOMPARE(GridUtils::computeItemRow(0, 4), 0);
  QCOMPARE(GridUtils::computeItemRow(1, 4), 0);
  QCOMPARE(GridUtils::computeItemRow(3, 4), 0);
}

void TestGridUtils::testComputeItemRow_secondRow() {
  // Items 4-7 in a 4-wide grid are in row 1
  QCOMPARE(GridUtils::computeItemRow(4, 4), 1);
  QCOMPARE(GridUtils::computeItemRow(7, 4), 1);
  // Item 8 is in row 2
  QCOMPARE(GridUtils::computeItemRow(8, 4), 2);
}

void TestGridUtils::testComputeItemRow_zeroGridWidth() {
  // Zero grid width should return 0 (safe default)
  QCOMPARE(GridUtils::computeItemRow(5, 0), 0);
  QCOMPARE(GridUtils::computeItemRow(100, 0), 0);
}

void TestGridUtils::testComputeItemRow_singleColumnGrid() {
  // Single column grid: each item is on its own row
  QCOMPARE(GridUtils::computeItemRow(0, 1), 0);
  QCOMPARE(GridUtils::computeItemRow(1, 1), 1);
  QCOMPARE(GridUtils::computeItemRow(5, 1), 5);
}

// ─────────────────────────────────────────────────────────────────────────────
// computeItemCol tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridUtils::testComputeItemCol_firstColumn() {
  // Items 0, 4, 8 in a 4-wide grid are in column 0
  QCOMPARE(GridUtils::computeItemCol(0, 4), 0);
  QCOMPARE(GridUtils::computeItemCol(4, 4), 0);
  QCOMPARE(GridUtils::computeItemCol(8, 4), 0);
}

void TestGridUtils::testComputeItemCol_lastColumn() {
  // Items 3, 7, 11 in a 4-wide grid are in column 3
  QCOMPARE(GridUtils::computeItemCol(3, 4), 3);
  QCOMPARE(GridUtils::computeItemCol(7, 4), 3);
  QCOMPARE(GridUtils::computeItemCol(11, 4), 3);
}

void TestGridUtils::testComputeItemCol_zeroGridWidth() {
  // Zero grid width should return 0 (safe default)
  QCOMPARE(GridUtils::computeItemCol(5, 0), 0);
  QCOMPARE(GridUtils::computeItemCol(100, 0), 0);
}

void TestGridUtils::testComputeItemCol_wrapsToNextRow() {
  // Verify column wrapping
  QCOMPARE(GridUtils::computeItemCol(0, 3), 0);
  QCOMPARE(GridUtils::computeItemCol(1, 3), 1);
  QCOMPARE(GridUtils::computeItemCol(2, 3), 2);
  QCOMPARE(GridUtils::computeItemCol(3, 3), 0); // Wraps
  QCOMPARE(GridUtils::computeItemCol(4, 3), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// computeItemY tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridUtils::testComputeItemY_firstRow() {
  // First row Y = margins (10)
  int y = GridUtils::computeItemY(0, 4, 100, 10, 10);
  QCOMPARE(y, 10);
}

void TestGridUtils::testComputeItemY_secondRow() {
  // Second row Y = margins + (itemHeight + spacing)
  // 10 + 1 * (100 + 10) = 120
  int y = GridUtils::computeItemY(4, 4, 100, 10, 10);
  QCOMPARE(y, 120);
}

void TestGridUtils::testComputeItemY_includesMargins() {
  // With larger margins
  int y = GridUtils::computeItemY(0, 4, 100, 10, 50);
  QCOMPARE(y, 50);
}

// ─────────────────────────────────────────────────────────────────────────────
// computeItemX tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridUtils::testComputeItemX_firstColumn() {
  // First column X = margins (10)
  int x = GridUtils::computeItemX(0, 4, 200, 10, 10);
  QCOMPARE(x, 10);
}

void TestGridUtils::testComputeItemX_secondColumn() {
  // Second column X = margins + (itemWidth + spacing)
  // 10 + 1 * (200 + 10) = 220
  int x = GridUtils::computeItemX(1, 4, 200, 10, 10);
  QCOMPARE(x, 220);
}

void TestGridUtils::testComputeItemX_includesMargins() {
  // With larger margins
  int x = GridUtils::computeItemX(0, 4, 200, 10, 50);
  QCOMPARE(x, 50);
}

// ─────────────────────────────────────────────────────────────────────────────
// computeCenterTarget tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridUtils::testComputeCenterTarget_centeredItem() {
  // Item at coord 500, size 100, viewport 400
  // Center = 500 + 50 - 200 = 350
  int target = GridUtils::computeCenterTarget(500, 100, 400, 1000);
  QCOMPARE(target, 350);
}

void TestGridUtils::testComputeCenterTarget_clampedToZero() {
  // Item near top, centering would go negative
  // Item at coord 50, size 100, viewport 400
  // Center = 50 + 50 - 200 = -100 -> clamped to 0
  int target = GridUtils::computeCenterTarget(50, 100, 400, 1000);
  QCOMPARE(target, 0);
}

void TestGridUtils::testComputeCenterTarget_clampedToMax() {
  // Item near bottom, centering would exceed max
  // Item at coord 950, size 100, viewport 400
  // Center = 950 + 50 - 200 = 800 -> clamped to 500
  int target = GridUtils::computeCenterTarget(950, 100, 400, 500);
  QCOMPARE(target, 500);
}

// ─────────────────────────────────────────────────────────────────────────────
// calculateGridMetrics tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridUtils::testCalculateGridMetrics_basicGrid() {
  int totalWidth = 0, totalHeight = 0, actualGridWidth = 0;

  // 12 items, 4 per row, 200x100 items, 10 spacing, 20 margin
  // 3 rows
  GridUtils::calculateGridMetrics(12, 4, 200, 100, 10, 10, 20,
                                  totalWidth, totalHeight, actualGridWidth);

  // Width: 20*2 + 4*200 + 3*10 = 40 + 800 + 30 = 870
  QCOMPARE(totalWidth, 870);
  QCOMPARE(actualGridWidth, 870);

  // Height: 20 + 3*100 + 2*10 = 20 + 300 + 20 = 340
  QCOMPARE(totalHeight, 340);
}

void TestGridUtils::testCalculateGridMetrics_singleRow() {
  int totalWidth = 0, totalHeight = 0, actualGridWidth = 0;

  // 3 items, 5 per row = only 1 row
  GridUtils::calculateGridMetrics(3, 5, 100, 100, 10, 10, 10,
                                  totalWidth, totalHeight, actualGridWidth);

  // Width: 10*2 + 5*100 + 4*10 = 20 + 500 + 40 = 560
  QCOMPARE(totalWidth, 560);

  // Height: 10 + 1*100 + 0*10 = 110
  QCOMPARE(totalHeight, 110);
}

void TestGridUtils::testCalculateGridMetrics_singleColumn() {
  int totalWidth = 0, totalHeight = 0, actualGridWidth = 0;

  // 5 items, 1 per row = 5 rows
  GridUtils::calculateGridMetrics(5, 1, 100, 100, 10, 10, 10,
                                  totalWidth, totalHeight, actualGridWidth);

  // Width: 10*2 + 1*100 + 0*10 = 120
  QCOMPARE(totalWidth, 120);

  // Height: 10 + 5*100 + 4*10 = 10 + 500 + 40 = 550
  QCOMPARE(totalHeight, 550);
}

void TestGridUtils::testCalculateGridMetrics_emptyGrid() {
  int totalWidth = 0, totalHeight = 0, actualGridWidth = 0;

  // 0 items
  GridUtils::calculateGridMetrics(0, 4, 100, 100, 10, 10, 10,
                                  totalWidth, totalHeight, actualGridWidth);

  // Minimum dimensions apply
  QVERIFY(totalWidth >= 120);  // 10*2 + 100 = 120
  QVERIFY(totalHeight >= 110); // 10 + 100 = 110
}

void TestGridUtils::testCalculateGridMetrics_minimumDimensions() {
  int totalWidth = 0, totalHeight = 0, actualGridWidth = 0;

  // Very small margins but minimum should still apply
  GridUtils::calculateGridMetrics(1, 10, 100, 100, 5, 5, 5,
                                  totalWidth, totalHeight, actualGridWidth);

  int minWidth = 5 * 2 + 100;   // 110
  int minHeight = 5 + 100;      // 105

  QVERIFY(totalWidth >= minWidth);
  QVERIFY(totalHeight >= minHeight);
}

QTEST_MAIN(TestGridUtils)
#include "test_gridutils.moc"
