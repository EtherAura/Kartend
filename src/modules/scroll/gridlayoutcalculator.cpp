// Stateless calculator for grid layout metrics and item positioning.
#include "gridlayoutcalculator.h"

#include <algorithm>

auto GridLayoutCalculator::calculateMetrics(const CollectionConfig &config,
                                             int totalItems) -> GridMetrics {
  GridMetrics metrics;
  
  metrics.itemWidth = config.itemWidth;
  metrics.itemHeight = config.itemHeight;
  metrics.itemsPerRow = qMax(1, config.gridWidth);
  metrics.horizontalSpacing = config.horizontalSpacing;
  metrics.verticalSpacing = config.verticalSpacing;
  metrics.margins = UIConstants::Grid::MARGINS;

  GridUtils::calculateGridMetrics(
      totalItems, metrics.itemsPerRow, metrics.itemWidth,
      metrics.itemHeight, metrics.horizontalSpacing,
      metrics.verticalSpacing, metrics.margins, metrics.totalWidth,
      metrics.totalHeight, metrics.actualGridWidth,
      metrics.logicalHeight, metrics.scrollScale, metrics.isClipped);
  
  if (metrics.isClipped) {
    metrics.overflowAmount = metrics.logicalHeight - metrics.totalHeight;
  }

  metrics.totalRows =
      (totalItems + metrics.itemsPerRow - 1) / metrics.itemsPerRow;

  return metrics;
}

auto GridLayoutCalculator::adjustForFilter(const GridMetrics &baseMetrics,
                                            int filteredItemCount) -> GridMetrics {
  GridMetrics adjusted = baseMetrics;
  
  // If filtered items fit in a single partial row, adjust width
  if (filteredItemCount > 0 && filteredItemCount < baseMetrics.itemsPerRow) {
    int used = filteredItemCount;
    int horizontalSpacingContribution =
        (used > 1) ? (used - 1) * baseMetrics.horizontalSpacing : 0;
    adjusted.totalWidth = baseMetrics.margins * 2 + 
                          used * baseMetrics.itemWidth +
                          horizontalSpacingContribution;
    adjusted.actualGridWidth = adjusted.totalWidth;
    adjusted.totalRows = 1;
  } else {
    adjusted.totalRows =
        (filteredItemCount + baseMetrics.itemsPerRow - 1) / baseMetrics.itemsPerRow;
  }

  // Recalculate total height for filtered count
  int verticalSpacingContribution =
      (adjusted.totalRows > 1) ? (adjusted.totalRows - 1) * baseMetrics.verticalSpacing : 0;
  adjusted.totalHeight = baseMetrics.margins + 
                         adjusted.totalRows * baseMetrics.itemHeight +
                         verticalSpacingContribution;

  return adjusted;
}

auto GridLayoutCalculator::getItemPosition(int visualIndex,
                                            const GridMetrics &metrics,
                                            bool isFiltered,
                                            int filteredItemCount) -> QPoint {
  // When filtered to a single partial row, center by using actual item count
  bool centerSingleRow = isFiltered && filteredItemCount > 0 && 
                         filteredItemCount < metrics.itemsPerRow;
  int itemsPerRowForLayout = centerSingleRow ? filteredItemCount : metrics.itemsPerRow;

  int rowIndex = (itemsPerRowForLayout > 0) ? visualIndex / itemsPerRowForLayout : 0;
  int columnIndex = (itemsPerRowForLayout > 0) ? visualIndex % itemsPerRowForLayout : 0;

  int xPos = metrics.margins +
             (columnIndex * (metrics.itemWidth + metrics.horizontalSpacing));
  int yPos = (rowIndex * (metrics.itemHeight + metrics.verticalSpacing));

  return {xPos, yPos};
}

auto GridLayoutCalculator::getItemRect(int visualIndex,
                                        const GridMetrics &metrics,
                                        bool isFiltered,
                                        int filteredItemCount) -> QRect {
  QPoint pos = getItemPosition(visualIndex, metrics, isFiltered, filteredItemCount);
  return {pos.x(), pos.y(), metrics.itemWidth, metrics.itemHeight};
}

auto GridLayoutCalculator::indexAtPosition(const QPoint &pos,
                                            const GridMetrics &metrics,
                                            int totalItems) -> int {
  if (pos.x() < metrics.margins || pos.y() < 0) {
    return -1;
  }

  int columnWidth = metrics.itemWidth + metrics.horizontalSpacing;
  int rowHeight = metrics.itemHeight + metrics.verticalSpacing;

  if (columnWidth <= 0 || rowHeight <= 0) {
    return -1;
  }

  int col = (pos.x() - metrics.margins) / columnWidth;
  int row = pos.y() / rowHeight;

  if (col < 0 || col >= metrics.itemsPerRow) {
    return -1;
  }

  int index = row * metrics.itemsPerRow + col;
  if (index < 0 || index >= totalItems) {
    return -1;
  }

  return index;
}

auto GridLayoutCalculator::getVisibleRowRange(int scrollY, int viewportHeight,
                                               const GridMetrics &metrics,
                                               int bufferRows)
    -> std::pair<int, int> {
  int rowHeight = metrics.itemHeight + metrics.verticalSpacing;
  if (rowHeight <= 0) {
    return {0, 0};
  }

  // scrollY is in clamped/widget coordinates. Convert to logical coordinates
  // to determine which rows should be visible. Pass viewportHeight for precise
  // endpoint mapping so scrollbar max reaches the end of the collection.
  int logicalScrollY = metrics.toLogicalScrollY(scrollY, viewportHeight);
  
  // Account for top margin when calculating first visible row
  int adjustedScrollY = qMax(0, logicalScrollY - metrics.margins);
  int firstRow = qMax(0, adjustedScrollY / rowHeight - bufferRows);
  int lastRow = (logicalScrollY + viewportHeight - metrics.margins) / rowHeight + bufferRows;
  lastRow = qMax(firstRow, qMin(lastRow, metrics.totalRows - 1));

  return {firstRow, lastRow};
}

auto GridLayoutCalculator::getVisibleIndexRange(int scrollY, int viewportHeight,
                                                 const GridMetrics &metrics,
                                                 int totalItems, int bufferRows)
    -> std::pair<int, int> {
  auto [firstRow, lastRow] = getVisibleRowRange(scrollY, viewportHeight, 
                                                 metrics, bufferRows);
  
  int firstIndex = firstRow * metrics.itemsPerRow;
  int lastIndex = qMin((lastRow + 1) * metrics.itemsPerRow - 1, totalItems - 1);

  return {qMax(0, firstIndex), qMax(0, lastIndex)};
}

auto GridLayoutCalculator::calculateCenterScrollTarget(int itemIndex,
                                                        int viewportHeight,
                                                        int maxScroll,
                                                        const GridMetrics &metrics)
    -> int {
  int row = GridUtils::computeItemRow(itemIndex, metrics.itemsPerRow);
  int itemY = row * (metrics.itemHeight + metrics.verticalSpacing);
  
  return GridUtils::computeCenterTarget(itemY, metrics.itemHeight, 
                                        viewportHeight, maxScroll);
}
