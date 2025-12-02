/**
 * @file test_gridlayoutcalculator.cpp
 * @brief Unit tests for GridLayoutCalculator
 *
 * Tests the stateless grid layout calculation functions extracted from ScrollManager.
 */

#include "gridlayoutcalculator.h"
#include "collectionutils.h"
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

private:
  CollectionConfig m_config;
  GridMetrics m_metrics;
};

void TestGridLayoutCalculator::initTestCase() {
  // Setup a standard test configuration
  m_config.gridWidth = 4;
  m_config.itemWidth = 200;
  m_config.itemHeight = 280;
  m_config.horizontalSpacing = 10;
  m_config.verticalSpacing = 10;
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
  singleCol.gridWidth = 1;

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

  auto [first, last] = GridLayoutCalculator::getVisibleRowRange(
      0, 600, m_metrics); // scrollY=0, viewportHeight=600

  QCOMPARE(first, 0);
  QVERIFY(last >= 0);
  QVERIFY(last <= m_metrics.totalRows);
}

void TestGridLayoutCalculator::testGetVisibleRowRange_scrolledDown() {
  m_metrics = GridLayoutCalculator::calculateMetrics(m_config, 100);

  int rowHeight = m_metrics.itemHeight + m_metrics.verticalSpacing;
  auto [first, last] = GridLayoutCalculator::getVisibleRowRange(
      rowHeight * 5, 600, m_metrics); // Scrolled down 5 rows

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

QTEST_MAIN(TestGridLayoutCalculator)
#include "test_gridlayoutcalculator.moc"
