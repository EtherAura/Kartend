/**
 * @file test_gridlayoutcalculator.cpp
 * @brief Unit tests for GridLayoutCalculator
 *
 * Tests the stateless grid layout calculation functions extracted from ScrollManager.
 */

#include "collection/collectionconfig.h"
#include "gridlayoutcalculator.h"
#include <QTest>

class TestGridLayoutCalculator : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  // calculateMetrics tests
  void testCalculateMetrics_basicGrid();
  void testCalculateMetrics_singleColumn();
  void testCalculateMetrics_emptyCollection();

  // adjustForFilter tests
  void testAdjustForFilter_smallerThanRow();
  void testAdjustForFilter_exactlyOneRow();
  void testAdjustForFilter_multipleRows();

  // getItemPosition tests
  void testGetItemPosition_firstItem();
  void testGetItemPosition_endOfFirstRow();
  void testGetItemPosition_secondRow();
  void testGetItemPosition_filteredCentered();

  // getItemRect tests
  void testGetItemRect_basic();

  // indexAtPosition tests
  void testIndexAtPosition_firstItem();
  void testIndexAtPosition_secondRow();
  void testIndexAtPosition_outOfBounds();

  // getVisibleRowRange tests
  void testGetVisibleRowRange_topOfView();
  void testGetVisibleRowRange_scrolledDown();
  void testGetVisibleRowRange_emptyMetrics();

  // Horizontal view-mode tests (axis-flipped layout).
  void testHorizontal_calculateMetrics_dimensionsAreTransposed();
  void testHorizontal_getItemPosition_columnMajor();
  void testHorizontal_getVisibleRowRange_isColumnRange();
  void testHorizontal_calculateCenterScrollTarget_centersAlongX();
  void testHorizontal_indexAtPosition_columnMajor();
  void testHorizontal_horizontalGridHeightOverridesGridWidth();
  void testHorizontal_horizontalGridHeightZeroFallsBackToGridWidth();

private:
  CollectionConfig m_config;
  GridMetrics m_metrics;
};

void TestGridLayoutCalculator::initTestCase() {
  // Setup a standard test configuration
  m_config.gridLayout.gridWidth = 4;
  m_config.gridLayout.itemWidth = 200;
  m_config.gridLayout.itemHeight = 280;
  m_config.gridLayout.horizontalSpacing = 10;
  m_config.gridLayout.verticalSpacing = 10;
}

// ─────────────────────────────────────────────────────────────────────────────
// calculateMetrics tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridLayoutCalculator::testCalculateMetrics_basicGrid() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);

  QCOMPARE(m_metrics.itemsPerRow, 4);
  QCOMPARE(m_metrics.itemWidth, 200);
  QCOMPARE(m_metrics.itemHeight, 280);
  QCOMPARE(m_metrics.horizontalSpacing, 10);
  QCOMPARE(m_metrics.verticalSpacing, 10);
  QCOMPARE(m_metrics.totalRows, 5); // 20 items / 4 per row = 5 rows
}

void TestGridLayoutCalculator::testCalculateMetrics_singleColumn() {
  CollectionConfig singleCol = m_config;
  singleCol.gridLayout.gridWidth = 1;

  GridMetrics metrics = GridLayoutCalculator::calculateMetrics(singleCol, 10);

  QCOMPARE(metrics.itemsPerRow, 1);
  QCOMPARE(metrics.totalRows, 10);
}

void TestGridLayoutCalculator::testCalculateMetrics_emptyCollection() {
  GridMetrics metrics = GridLayoutCalculator::calculateMetrics(m_config, 0);

  QCOMPARE(metrics.totalRows, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// adjustForFilter tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridLayoutCalculator::testAdjustForFilter_smallerThanRow() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);
  int filteredCount = 3; // Less than itemsPerRow (4)

  GridMetrics filtered = GridLayoutCalculator::adjustForFilter(m_metrics, filteredCount);

  // When filtered items < itemsPerRow, they should be centered
  QCOMPARE(filtered.totalRows, 1);
}

void TestGridLayoutCalculator::testAdjustForFilter_exactlyOneRow() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);
  int filteredCount = 4; // Exactly itemsPerRow

  GridMetrics filtered = GridLayoutCalculator::adjustForFilter(m_metrics, filteredCount);

  QCOMPARE(filtered.totalRows, 1);
}

void TestGridLayoutCalculator::testAdjustForFilter_multipleRows() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);
  int filteredCount = 10;

  GridMetrics filtered = GridLayoutCalculator::adjustForFilter(m_metrics, filteredCount);

  QCOMPARE(filtered.totalRows, 3); // ceil(10/4) = 3
}

// ─────────────────────────────────────────────────────────────────────────────
// getItemPosition tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridLayoutCalculator::testGetItemPosition_firstItem() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);

  QPoint pos = GridLayoutCalculator::getItemPosition(0, m_metrics, false, 20);

  // First item should be at margins offset
  QCOMPARE(pos.x(), m_metrics.margins);
  QCOMPARE(pos.y(), 0);
}

void TestGridLayoutCalculator::testGetItemPosition_endOfFirstRow() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);

  QPoint pos = GridLayoutCalculator::getItemPosition(3, m_metrics, false, 20);

  // Fourth item (index 3) should be at column 3
  int expectedX = m_metrics.margins + 3 * (m_metrics.itemWidth + m_metrics.horizontalSpacing);
  QCOMPARE(pos.x(), expectedX);
  QCOMPARE(pos.y(), 0);
}

void TestGridLayoutCalculator::testGetItemPosition_secondRow() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);

  QPoint pos = GridLayoutCalculator::getItemPosition(4, m_metrics, false, 20);

  // Fifth item (index 4) should be at row 1, column 0
  int expectedY = 1 * (m_metrics.itemHeight + m_metrics.verticalSpacing);
  QCOMPARE(pos.x(), m_metrics.margins);
  QCOMPARE(pos.y(), expectedY);
}

void TestGridLayoutCalculator::testGetItemPosition_filteredCentered() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);

  // When filtered to 2 items (less than itemsPerRow of 4), items should be centered
  QPoint pos = GridLayoutCalculator::getItemPosition(0, m_metrics, true, 2);

  // First item in centered single row - x position depends on centering logic
  QCOMPARE(pos.y(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// getItemRect tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridLayoutCalculator::testGetItemRect_basic() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);

  QRect rect = GridLayoutCalculator::getItemRect(0, m_metrics, false, 20);

  QCOMPARE(rect.x(), m_metrics.margins);
  QCOMPARE(rect.y(), 0);
  QCOMPARE(rect.width(), m_metrics.itemWidth);
  QCOMPARE(rect.height(), m_metrics.itemHeight);
}

// ─────────────────────────────────────────────────────────────────────────────
// indexAtPosition tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridLayoutCalculator::testIndexAtPosition_firstItem() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);

  QPoint pos(m_metrics.margins + 50, 50); // Inside first item
  int index = GridLayoutCalculator::indexAtPosition(pos, m_metrics, 20);

  QCOMPARE(index, 0);
}

void TestGridLayoutCalculator::testIndexAtPosition_secondRow() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);

  int rowHeight = m_metrics.itemHeight + m_metrics.verticalSpacing;
  QPoint pos(m_metrics.margins + 50, rowHeight + 50); // Inside first item of second row
  int index = GridLayoutCalculator::indexAtPosition(pos, m_metrics, 20);

  QCOMPARE(index, 4); // First item of second row
}

void TestGridLayoutCalculator::testIndexAtPosition_outOfBounds() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 20);

  QPoint pos(10000, 10000); // Way outside
  int index = GridLayoutCalculator::indexAtPosition(pos, m_metrics, 20);

  QCOMPARE(index, -1);
}

// ─────────────────────────────────────────────────────────────────────────────
// getVisibleRowRange tests
// ─────────────────────────────────────────────────────────────────────────────

void TestGridLayoutCalculator::testGetVisibleRowRange_topOfView() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 100);

  auto [first, last] =
      GridLayoutCalculator::getVisibleRowRange(0, 600, m_metrics); // scrollY=0, viewportHeight=600

  QCOMPARE(first, 0);
  QVERIFY(last >= 0);
  QVERIFY(last <= m_metrics.totalRows);
}

void TestGridLayoutCalculator::testGetVisibleRowRange_scrolledDown() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 100);

  int rowHeight = m_metrics.itemHeight + m_metrics.verticalSpacing;
  auto [first, last] = GridLayoutCalculator::getVisibleRowRange(rowHeight * 5, 600,
                                                                m_metrics); // Scrolled down 5 rows

  // With buffer rows, first visible row should be around row 3-5
  QVERIFY(first >= 0);
  QVERIFY(first <= 5); // Should start at or before row 5
  QVERIFY(last > first);
}

void TestGridLayoutCalculator::testGetVisibleRowRange_emptyMetrics() {
  GridMetrics emptyMetrics;
  emptyMetrics.itemHeight = 0;
  emptyMetrics.verticalSpacing = 0;
  emptyMetrics.totalRows = 0;

  auto [first, last] = GridLayoutCalculator::getVisibleRowRange(0, 600, emptyMetrics);

  QCOMPARE(first, 0);
  QCOMPARE(last, 0);
}

// ─── Horizontal view tests ─────────────────────────────────────

void TestGridLayoutCalculator::testHorizontal_calculateMetrics_dimensionsAreTransposed() {
  CollectionConfig cfg = m_config;
  cfg.viewType = ViewType::Horizontal;
  cfg.gridLayout.gridWidth = 3; // items per column

  GridMetrics metrics = GridLayoutCalculator::calculateMetrics(cfg, /*totalItems=*/10);

  QVERIFY(metrics.isHorizontal);
  QCOMPARE(metrics.itemsPerRow, 3); // reinterpreted as items-per-column
  QCOMPARE(metrics.totalRows, 4);   // ceil(10 / 3) columns along the long axis

  // The fixed (Y) dimension should equal margins*2 + 3 items + 2 spacings.
  const int rowHeight = metrics.itemHeight + metrics.verticalSpacing;
  const int expectedHeight =
      metrics.margins * 2 + 3 * metrics.itemHeight + 2 * metrics.verticalSpacing;
  QCOMPARE(metrics.totalHeight, expectedHeight);
  Q_UNUSED(rowHeight);

  // The long (X) dimension should grow with column count.
  const int expectedWidth =
      metrics.margins * 2 + 4 * metrics.itemWidth + 3 * metrics.horizontalSpacing;
  QCOMPARE(metrics.totalWidth, expectedWidth);
}

void TestGridLayoutCalculator::testHorizontal_getItemPosition_columnMajor() {
  CollectionConfig cfg = m_config;
  cfg.viewType = ViewType::Horizontal;
  cfg.gridLayout.gridWidth = 3;

  GridMetrics m = GridLayoutCalculator::calculateMetrics(cfg, /*totalItems=*/9);

  // Item 0: col 0, row 0 → top-left of grid
  QPoint p0 = GridLayoutCalculator::getItemPosition(0, m);
  QCOMPARE(p0.x(), m.margins);
  QCOMPARE(p0.y(), m.margins);

  // Item 2: col 0, row 2 → bottom of first column
  QPoint p2 = GridLayoutCalculator::getItemPosition(2, m);
  QCOMPARE(p2.x(), m.margins);
  QCOMPARE(p2.y(), m.margins + 2 * (m.itemHeight + m.verticalSpacing));

  // Item 3: col 1, row 0 → top of second column
  QPoint p3 = GridLayoutCalculator::getItemPosition(3, m);
  QCOMPARE(p3.x(), m.margins + (m.itemWidth + m.horizontalSpacing));
  QCOMPARE(p3.y(), m.margins);
}

void TestGridLayoutCalculator::testHorizontal_getVisibleRowRange_isColumnRange() {
  CollectionConfig cfg = m_config;
  cfg.viewType = ViewType::Horizontal;
  cfg.gridLayout.gridWidth = 3;

  GridMetrics m = GridLayoutCalculator::calculateMetrics(cfg, /*totalItems=*/30);
  // Viewport ~600 wide; with itemWidth=200 + hSpacing=10 → 210 col stride →
  // ~3 columns visible at a time (with buffer).
  auto [first, last] = GridLayoutCalculator::getVisibleRowRange(0, 600, m, /*bufferRows=*/0);
  QCOMPARE(first, 0);
  QVERIFY(last >= 2);
  QVERIFY(last <= 3);

  // Scrolled to ~middle of the strip.
  auto [first2, last2] = GridLayoutCalculator::getVisibleRowRange(800, 600, m, /*bufferRows=*/0);
  QVERIFY(first2 >= 3);
  QVERIFY(last2 > first2);
}

void TestGridLayoutCalculator::testHorizontal_calculateCenterScrollTarget_centersAlongX() {
  CollectionConfig cfg = m_config;
  cfg.viewType = ViewType::Horizontal;
  cfg.gridLayout.gridWidth = 3;

  GridMetrics m = GridLayoutCalculator::calculateMetrics(cfg, /*totalItems=*/30);
  const int colStride = m.itemWidth + m.horizontalSpacing;
  // Item 9 lives at column 3 (9 / 3). centerTarget should aim at colX + half - vw/2.
  int target = GridLayoutCalculator::calculateCenterScrollTarget(/*idx=*/9, /*viewportSize=*/600,
                                                                 /*maxScroll=*/100000, m);
  int itemX = m.margins + 3 * colStride;
  int expected = itemX + (m.itemWidth / 2) - (600 / 2);
  QCOMPARE(target, qMax(0, expected));
}

void TestGridLayoutCalculator::testHorizontal_indexAtPosition_columnMajor() {
  CollectionConfig cfg = m_config;
  cfg.viewType = ViewType::Horizontal;
  cfg.gridLayout.gridWidth = 3;

  GridMetrics m = GridLayoutCalculator::calculateMetrics(cfg, /*totalItems=*/30);
  // A point inside the third item's slot (col 0, row 2).
  QPoint p(m.margins + 5, m.margins + 2 * (m.itemHeight + m.verticalSpacing) + 5);
  int idx = GridLayoutCalculator::indexAtPosition(p, m, /*totalItems=*/30);
  QCOMPARE(idx, 2);
  // A point inside the (col 1, row 0) slot.
  QPoint p2(m.margins + (m.itemWidth + m.horizontalSpacing) + 5, m.margins + 5);
  int idx2 = GridLayoutCalculator::indexAtPosition(p2, m, /*totalItems=*/30);
  QCOMPARE(idx2, 3);
}

void TestGridLayoutCalculator::testHorizontal_horizontalGridHeightOverridesGridWidth() {
  CollectionConfig cfg = m_config;
  cfg.viewType = ViewType::Horizontal;
  cfg.gridLayout.gridWidth = 4;
  cfg.gridLayout.horizontalGridHeight = 6; // explicit override

  GridMetrics m = GridLayoutCalculator::calculateMetrics(cfg, /*totalItems=*/12);

  // itemsPerRow encodes the fixed-axis count → should follow the override, not gridWidth.
  QCOMPARE(m.itemsPerRow, 6);
  QCOMPARE(m.totalRows, 2); // ceil(12 / 6) columns
}

void TestGridLayoutCalculator::testHorizontal_horizontalGridHeightZeroFallsBackToGridWidth() {
  CollectionConfig cfg = m_config;
  cfg.viewType = ViewType::Horizontal;
  cfg.gridLayout.gridWidth = 4;
  cfg.gridLayout.horizontalGridHeight = 0; // sentinel: inherit gridWidth

  GridMetrics m = GridLayoutCalculator::calculateMetrics(cfg, /*totalItems=*/12);

  QCOMPARE(m.itemsPerRow, 4);
  QCOMPARE(m.totalRows, 3); // ceil(12 / 4) columns
}

QTEST_MAIN(TestGridLayoutCalculator)
#include "test_gridlayoutcalculator.moc"
