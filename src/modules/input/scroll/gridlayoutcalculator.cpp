// Stateless calculator for grid layout metrics and item positioning.
#include "gridlayoutcalculator.h"
#include "collection/collectionconfig.h"
#include "collection/validationhelpers.h"

#include <algorithm>
#include <climits>
#include <uiconstants/grid.h>
#include <uiconstants/listview.h>

auto GridLayoutCalculator::calculateMetrics(const CollectionConfig &config, int totalItems,
                                            bool sidebarShrinkingActive) -> GridMetrics {
  GridMetrics metrics;
  metrics.isHorizontal = (config.viewType == ViewType::Horizontal);

  // List mode uses different dimensions: full width, single row height
  bool isListMode = (config.viewType == ViewType::List);

  if (isListMode) {
    // List mode: full width items, custom or default row height, 1 item per row
    metrics.itemWidth = config.gridLayout.itemWidth;    // Will be set to viewport width by caller
    metrics.itemHeight = config.listView.listRowHeight; // Use custom row height from config
    metrics.itemsPerRow = 1;                            // List is always single column
    metrics.horizontalSpacing = 0;
    metrics.verticalSpacing = UIConstants::ListView::ROW_SPACING;
    metrics.headerOffset = UIConstants::ListView::HEADER_HEIGHT;
  } else {
    // Grid / Horizontal / CoverFlow: use collection config
    metrics.itemWidth = config.gridLayout.itemWidth;
    metrics.itemHeight = config.gridLayout.itemHeight;
    // in Horizontal mode the fixed dimension is items-per-column,
    // controlled by horizontalGridHeight (falling back to gridWidth when 0 so
    // existing collections that flip to Horizontal mode without configuring
    // the new field still get a sane layout).
    // route both gridWidth and horizontalGridHeight through the
    // effective-value helpers so sidebar-hidden alternates apply when the
    // sidebar is hidden in Expand mode.
    const int effectiveHorizontal =
        CollectionUtils::effectiveHorizontalGridHeight(config, sidebarShrinkingActive);
    if (metrics.isHorizontal && effectiveHorizontal > 0) {
      metrics.itemsPerRow = qMax(1, effectiveHorizontal);
    } else {
      metrics.itemsPerRow =
          qMax(1, CollectionUtils::effectiveGridWidth(config, sidebarShrinkingActive));
    }
    metrics.horizontalSpacing = config.gridLayout.horizontalSpacing;
    metrics.verticalSpacing = config.gridLayout.verticalSpacing;
    metrics.headerOffset = 0;
  }

  metrics.margins = UIConstants::Grid::MARGINS;

  if (metrics.isHorizontal) {
    // axis-flipped layout. itemsPerRow is reinterpreted as
    // items-per-column (the fixed Y axis); totalRows becomes the column
    // count (the long, scrollable X axis).
    int itemsPerCol = metrics.itemsPerRow;
    int totalCols = (itemsPerCol > 0) ? ((totalItems + itemsPerCol - 1) / itemsPerCol) : 1;

    int verticalSpacingContribution =
        (itemsPerCol > 1 ? (itemsPerCol - 1) * metrics.verticalSpacing : 0);
    metrics.totalHeight =
        metrics.margins * 2 + itemsPerCol * metrics.itemHeight + verticalSpacingContribution;
    metrics.logicalHeight = metrics.totalHeight;

    int horizontalSpacingContribution =
        (totalCols > 1 ? (totalCols - 1) * metrics.horizontalSpacing : 0);
    qint64 rawWidth = static_cast<qint64>(metrics.margins) * 2 +
                      static_cast<qint64>(totalCols) * metrics.itemWidth +
                      horizontalSpacingContribution;
    constexpr int MAX_WIDTH = kQtMaxWidgetSize - 1000;
    if (rawWidth > MAX_WIDTH) {
      metrics.totalWidth = MAX_WIDTH;
      metrics.scrollScale = static_cast<double>(rawWidth) / static_cast<double>(MAX_WIDTH);
      metrics.isClipped = true;
      metrics.overflowAmount = static_cast<int>(
          qMin<qint64>(rawWidth - static_cast<qint64>(MAX_WIDTH), static_cast<qint64>(INT_MAX)));
    } else {
      metrics.totalWidth = static_cast<int>(rawWidth);
      metrics.scrollScale = 1.0;
      metrics.isClipped = false;
    }
    metrics.actualGridWidth = metrics.totalWidth;
    metrics.totalRows = totalCols;

    int minHeight = metrics.margins + metrics.itemHeight;
    if (metrics.totalHeight < minHeight) metrics.totalHeight = minHeight;
    if (metrics.logicalHeight < minHeight) metrics.logicalHeight = minHeight;
    int minWidth = metrics.margins * 2 + metrics.itemWidth;
    if (metrics.totalWidth < minWidth) metrics.totalWidth = minWidth;
    return metrics;
  }

  GridUtils::calculateGridMetrics(totalItems, metrics.itemsPerRow, metrics.itemWidth,
                                  metrics.itemHeight, metrics.horizontalSpacing,
                                  metrics.verticalSpacing, metrics.margins, metrics.totalWidth,
                                  metrics.totalHeight, metrics.actualGridWidth,
                                  metrics.logicalHeight, metrics.scrollScale, metrics.isClipped);

  // Add header offset to total height in list mode
  if (metrics.headerOffset > 0) {
    metrics.totalHeight += metrics.headerOffset;
    metrics.logicalHeight += metrics.headerOffset;
  }

  if (metrics.isClipped) {
    metrics.overflowAmount = metrics.logicalHeight - metrics.totalHeight;
  }

  metrics.totalRows = (totalItems + metrics.itemsPerRow - 1) / metrics.itemsPerRow;

  return metrics;
}

auto GridLayoutCalculator::adjustForFilter(const GridMetrics &baseMetrics, int filteredItemCount)
    -> GridMetrics {
  GridMetrics adjusted = baseMetrics;

  if (baseMetrics.isHorizontal) {
    // horizontal partial-fit collapses the long-axis (column
    // count). itemsPerRow stays as items-per-column — we just shrink the
    // scrollable width when the filtered set fits in fewer than one column.
    if (filteredItemCount > 0 && filteredItemCount < baseMetrics.itemsPerRow) {
      int rowsUsed = filteredItemCount;
      int verticalSpacingContribution =
          (rowsUsed > 1) ? (rowsUsed - 1) * baseMetrics.verticalSpacing : 0;
      adjusted.totalHeight =
          baseMetrics.margins * 2 + rowsUsed * baseMetrics.itemHeight + verticalSpacingContribution;
      adjusted.logicalHeight = adjusted.totalHeight;
      adjusted.totalRows = 1;
    } else {
      adjusted.totalRows =
          (filteredItemCount + baseMetrics.itemsPerRow - 1) / baseMetrics.itemsPerRow;
    }
    int horizontalSpacingContribution =
        (adjusted.totalRows > 1) ? (adjusted.totalRows - 1) * baseMetrics.horizontalSpacing : 0;
    adjusted.totalWidth = baseMetrics.margins * 2 + adjusted.totalRows * baseMetrics.itemWidth +
                          horizontalSpacingContribution;
    adjusted.actualGridWidth = adjusted.totalWidth;
    return adjusted;
  }

  // If filtered items fit in a single partial row, adjust width
  if (filteredItemCount > 0 && filteredItemCount < baseMetrics.itemsPerRow) {
    int used = filteredItemCount;
    int horizontalSpacingContribution = (used > 1) ? (used - 1) * baseMetrics.horizontalSpacing : 0;
    adjusted.totalWidth =
        baseMetrics.margins * 2 + used * baseMetrics.itemWidth + horizontalSpacingContribution;
    adjusted.actualGridWidth = adjusted.totalWidth;
    adjusted.totalRows = 1;
  } else {
    adjusted.totalRows =
        (filteredItemCount + baseMetrics.itemsPerRow - 1) / baseMetrics.itemsPerRow;
  }

  // Recalculate total height for filtered count
  int verticalSpacingContribution =
      (adjusted.totalRows > 1) ? (adjusted.totalRows - 1) * baseMetrics.verticalSpacing : 0;
  adjusted.totalHeight = baseMetrics.margins + adjusted.totalRows * baseMetrics.itemHeight +
                         verticalSpacingContribution;

  return adjusted;
}

auto GridLayoutCalculator::getItemPosition(int visualIndex, const GridMetrics &metrics,
                                           bool isFiltered, int filteredItemCount) -> QPoint {
  // When filtered to a single partial row, center by using actual item count
  bool centerSinglePartial =
      isFiltered && filteredItemCount > 0 && filteredItemCount < metrics.itemsPerRow;
  int itemsPerFixedDim = centerSinglePartial ? filteredItemCount : metrics.itemsPerRow;

  if (metrics.isHorizontal) {
    int colIndex = (itemsPerFixedDim > 0) ? visualIndex / itemsPerFixedDim : 0;
    int rowIndex = (itemsPerFixedDim > 0) ? visualIndex % itemsPerFixedDim : 0;
    int xPos = metrics.margins + (colIndex * (metrics.itemWidth + metrics.horizontalSpacing));
    int yPos = metrics.margins + (rowIndex * (metrics.itemHeight + metrics.verticalSpacing));
    return {xPos, yPos};
  }

  int rowIndex = (itemsPerFixedDim > 0) ? visualIndex / itemsPerFixedDim : 0;
  int columnIndex = (itemsPerFixedDim > 0) ? visualIndex % itemsPerFixedDim : 0;

  int xPos = metrics.margins + (columnIndex * (metrics.itemWidth + metrics.horizontalSpacing));
  // Add header offset for list view mode
  int yPos = metrics.headerOffset + (rowIndex * (metrics.itemHeight + metrics.verticalSpacing));

  return {xPos, yPos};
}

auto GridLayoutCalculator::getItemRect(int visualIndex, const GridMetrics &metrics, bool isFiltered,
                                       int filteredItemCount) -> QRect {
  QPoint pos = getItemPosition(visualIndex, metrics, isFiltered, filteredItemCount);
  return {pos.x(), pos.y(), metrics.itemWidth, metrics.itemHeight};
}

auto GridLayoutCalculator::indexAtPosition(const QPoint &pos, const GridMetrics &metrics,
                                           int totalItems) -> int {
  int columnWidth = metrics.itemWidth + metrics.horizontalSpacing;
  int rowHeight = metrics.itemHeight + metrics.verticalSpacing;
  if (columnWidth <= 0 || rowHeight <= 0) {
    return -1;
  }

  if (metrics.isHorizontal) {
    if (pos.x() < metrics.margins || pos.y() < metrics.margins) {
      return -1;
    }
    int colIdx = (pos.x() - metrics.margins) / columnWidth;
    int rowIdx = (pos.y() - metrics.margins) / rowHeight;
    if (rowIdx < 0 || rowIdx >= metrics.itemsPerRow) {
      return -1;
    }
    int index = colIdx * metrics.itemsPerRow + rowIdx;
    if (index < 0 || index >= totalItems) {
      return -1;
    }
    return index;
  }

  // Account for header offset in list mode
  int adjustedY = pos.y() - metrics.headerOffset;
  if (pos.x() < metrics.margins || adjustedY < 0) {
    return -1;
  }

  int col = (pos.x() - metrics.margins) / columnWidth;
  int row = adjustedY / rowHeight;

  if (col < 0 || col >= metrics.itemsPerRow) {
    return -1;
  }

  int index = row * metrics.itemsPerRow + col;
  if (index < 0 || index >= totalItems) {
    return -1;
  }

  return index;
}

auto GridLayoutCalculator::getVisibleRowRange(int scrollPos, int viewportSize,
                                              const GridMetrics &metrics, int bufferRows)
    -> std::pair<int, int> {
  // In Horizontal mode "row" is reinterpreted as "long-axis
  // index" — i.e. column index — and the caller passes scrollX/viewportWidth.
  if (metrics.isHorizontal) {
    int columnWidth = metrics.itemWidth + metrics.horizontalSpacing;
    if (columnWidth <= 0) {
      return {0, 0};
    }
    int logicalScroll = metrics.toLogicalScrollY(scrollPos, viewportSize);
    int adjustedScroll = qMax(0, logicalScroll - metrics.margins);
    int firstCol = qMax(0, adjustedScroll / columnWidth - bufferRows);
    int lastCol = (logicalScroll + viewportSize - metrics.margins) / columnWidth + bufferRows;
    lastCol = qMax(firstCol, qMin(lastCol, metrics.totalRows - 1));
    return {firstCol, lastCol};
  }

  int rowHeight = metrics.itemHeight + metrics.verticalSpacing;
  if (rowHeight <= 0) {
    return {0, 0};
  }

  // scrollY is in clamped/widget coordinates. Convert to logical coordinates
  // to determine which rows should be visible. Pass viewportHeight for precise
  // endpoint mapping so scrollbar max reaches the end of the collection.
  int logicalScrollY = metrics.toLogicalScrollY(scrollPos, viewportSize);

  // Account for top margin and header offset when calculating first visible row
  int adjustedScrollY = qMax(0, logicalScrollY - metrics.margins - metrics.headerOffset);
  int firstRow = qMax(0, adjustedScrollY / rowHeight - bufferRows);
  int lastRow =
      (logicalScrollY + viewportSize - metrics.margins - metrics.headerOffset) / rowHeight +
      bufferRows;
  lastRow = qMax(firstRow, qMin(lastRow, metrics.totalRows - 1));

  return {firstRow, lastRow};
}

auto GridLayoutCalculator::getVisibleIndexRange(int scrollPos, int viewportSize,
                                                const GridMetrics &metrics, int totalItems,
                                                int bufferRows) -> std::pair<int, int> {
  auto [firstRow, lastRow] = getVisibleRowRange(scrollPos, viewportSize, metrics, bufferRows);

  int firstIndex = firstRow * metrics.itemsPerRow;
  int lastIndex = qMin((lastRow + 1) * metrics.itemsPerRow - 1, totalItems - 1);

  return {qMax(0, firstIndex), qMax(0, lastIndex)};
}

auto GridLayoutCalculator::calculateCenterScrollTarget(int itemIndex, int viewportSize,
                                                       int maxScroll, const GridMetrics &metrics)
    -> int {
  if (metrics.isHorizontal) {
    int col = GridUtils::computeItemRow(itemIndex, metrics.itemsPerRow);
    int itemX = metrics.margins + col * (metrics.itemWidth + metrics.horizontalSpacing);
    return GridUtils::computeCenterTarget(itemX, metrics.itemWidth, viewportSize, maxScroll);
  }
  int row = GridUtils::computeItemRow(itemIndex, metrics.itemsPerRow);
  int itemY = row * (metrics.itemHeight + metrics.verticalSpacing);

  return GridUtils::computeCenterTarget(itemY, metrics.itemHeight, viewportSize, maxScroll);
}
