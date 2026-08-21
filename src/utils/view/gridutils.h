#ifndef GRIDUTILS_H
#define GRIDUTILS_H

#include <climits>

#include <QtGlobal>

// Qt's QWIDGETSIZE_MAX is 16,777,215 (2^24 - 1), defined in QWidget.
// We define it here to avoid pulling in the entire QtWidgets header.
constexpr int kQtMaxWidgetSize = 16777215;

namespace GridUtils {

[[nodiscard]] inline int computeItemRow(int index, int gridWidth) {
  return (gridWidth > 0) ? (index / gridWidth) : 0;
}

[[nodiscard]] inline int computeItemCol(int index, int gridWidth) {
  return (gridWidth > 0) ? (index % gridWidth) : 0;
}

[[nodiscard]] inline int computeItemY(int index, int gridWidth, int itemHeight, int verticalSpacing,
                                      int margins) {
  int row = computeItemRow(index, gridWidth);
  return margins + row * (itemHeight + verticalSpacing);
}

[[nodiscard]] inline int computeItemX(int index, int gridWidth, int itemWidth,
                                      int horizontalSpacing, int margins) {
  int col = computeItemCol(index, gridWidth);
  return margins + col * (itemWidth + horizontalSpacing);
}

[[nodiscard]] inline int computeCenterTarget(int itemCoord, int itemSize, int viewportSize,
                                             int maxScroll) {
  int t = itemCoord + itemSize / 2 - viewportSize / 2;
  if (t < 0)
    t = 0;
  else if (t > maxScroll)
    t = maxScroll;
  return t;
}

/// Calculate grid metrics with height clamping for very large collections.
/// When totalHeight exceeds QWIDGETSIZE_MAX, it's clamped and scrollScale is
/// set to map scroll positions to the full logical range.
/// @param[out] scrollScale Scale factor (logicalHeight / clampedHeight), 1.0 if
/// no clamping needed
/// @param[out] isClipped True if height was clamped
inline void calculateGridMetrics(int totalItems, int itemsPerRow, int itemWidth, int itemHeight,
                                 int horizontalSpacing, int verticalSpacing, int margins,
                                 int &totalWidth, int &totalHeight, int &actualGridWidth,
                                 int &logicalHeight, double &scrollScale, bool &isClipped) {
  int totalRows = (itemsPerRow > 0) ? ((totalItems + itemsPerRow - 1) / itemsPerRow) : 1;

  // Symmetric with the height clamp below: itemsPerRow has no upper cap
  // (clampValues dropped the width cap), so compute in int64 to avoid silent
  // int overflow, then clamp to Qt's max widget size.
  constexpr int MAX_WIDTH = kQtMaxWidgetSize - 1000; // Safety margin
  if (itemsPerRow > 0) {
    // Width spans the columns actually OCCUPIED, not the configured column
    // count. A collection with fewer items than columns used to report a
    // container one phantom column too wide: right-aligned content hung that
    // column off the right edge and pushed the first item clean off the left,
    // centring was half a column out, and — worst — the inflated width could
    // declare an overflow for a grid that fits, clipping when there was room
    // for everything (reproduced in the VM, 2026-08-18). Full rows are
    // unaffected: occupied == itemsPerRow the moment there are enough items.
    const qint64 occupiedColumns = qBound(1, totalItems, itemsPerRow);
    const qint64 horizontalSpacingContribution =
        (occupiedColumns > 1 ? (occupiedColumns - 1) * horizontalSpacing : 0);
    const qint64 rawWidth = static_cast<qint64>(margins) * 2 + occupiedColumns * itemWidth +
                            horizontalSpacingContribution;
    const int clampedWidth = static_cast<int>(qMin(rawWidth, static_cast<qint64>(MAX_WIDTH)));
    actualGridWidth = clampedWidth;
    totalWidth = clampedWidth;
  } else {
    const qint64 rawWidth = static_cast<qint64>(margins) * 2 + itemWidth;
    const int clampedWidth = static_cast<int>(qMin(rawWidth, static_cast<qint64>(MAX_WIDTH)));
    totalWidth = clampedWidth;
    actualGridWidth = clampedWidth;
  }

  // Use int64 to avoid overflow during calculation
  int verticalSpacingContribution = (totalRows > 1 ? (totalRows - 1) * verticalSpacing : 0);
  qint64 rawHeight = static_cast<qint64>(margins) + static_cast<qint64>(totalRows) * itemHeight +
                     verticalSpacingContribution;

  // Store the true logical height
  logicalHeight = static_cast<int>(qMin(rawHeight, static_cast<qint64>(INT_MAX)));

  // Clamp to Qt's maximum widget size if needed
  constexpr int MAX_HEIGHT = kQtMaxWidgetSize - 1000; // Safety margin
  if (rawHeight > MAX_HEIGHT) {
    totalHeight = MAX_HEIGHT;
    scrollScale = static_cast<double>(rawHeight) / static_cast<double>(MAX_HEIGHT);
    isClipped = true;
  } else {
    totalHeight = static_cast<int>(rawHeight);
    scrollScale = 1.0;
    isClipped = false;
  }

  int minWidth = margins * 2 + itemWidth;
  if (totalWidth < minWidth) totalWidth = minWidth;

  int minHeight = margins + itemHeight;
  if (totalHeight < minHeight) totalHeight = minHeight;
  if (logicalHeight < minHeight) logicalHeight = minHeight;
}

/// Legacy overload for backward compatibility (no scroll scaling output)
inline void calculateGridMetrics(int totalItems, int itemsPerRow, int itemWidth, int itemHeight,
                                 int horizontalSpacing, int verticalSpacing, int margins,
                                 int &totalWidth, int &totalHeight, int &actualGridWidth) {
  int logicalHeight = 0;
  double scrollScale = 1.0;
  bool isClipped = false;
  calculateGridMetrics(totalItems, itemsPerRow, itemWidth, itemHeight, horizontalSpacing,
                       verticalSpacing, margins, totalWidth, totalHeight, actualGridWidth,
                       logicalHeight, scrollScale, isClipped);
}

} // namespace GridUtils

#endif