#ifndef GRIDLAYOUTCALCULATOR_H
#define GRIDLAYOUTCALCULATOR_H

#include "collectionutils.h"
#include "gridutils.h"
#include "uiconstants.h"
#include <QPoint>
#include <QRect>

/**
 * @brief Layout metrics for virtual scrolling grid.
 * 
 * Contains all computed dimensions and spacing for laying out items
 * in a virtual scrolling container. For very large collections that
 * exceed Qt's QWIDGETSIZE_MAX (16,777,215 pixels), the height is clamped
 * and scroll positions are scaled to map the full logical range.
 */
struct GridMetrics {
  int itemWidth = 0;
  int itemHeight = 0;
  int itemsPerRow = 0;
  int horizontalSpacing = 0;
  int verticalSpacing = 0;
  int margins = 0;
  int totalWidth = 0;
  int totalHeight = 0;        // Clamped height for Qt widget (≤ QWIDGETSIZE_MAX)
  int logicalHeight = 0;      // True logical height (may exceed Qt limits)
  int actualGridWidth = 0;
  int totalRows = 0;
  double scrollScale = 1.0;   // Scale factor: logicalHeight / totalHeight
  bool isClipped = false;     // True if logicalHeight > QWIDGETSIZE_MAX
  int overflowAmount = 0;     // logicalHeight - totalHeight when clipped
  
  [[nodiscard]] bool isValid() const {
    return itemWidth > 0 && itemHeight > 0 && itemsPerRow > 0;
  }
  
  /// Convert widget scroll position to logical scroll position.
  /// When viewportHeight is provided, ensures scrollbar max maps exactly to
  /// (logicalHeight - viewport) for precise endpoint mapping.
  [[nodiscard]] int toLogicalScrollY(int widgetScrollY, int viewportHeight = 0) const {
    if (scrollScale <= 1.0) return widgetScrollY;
    
    // With viewport, use proper linear interpolation for exact endpoint mapping:
    // widget 0 -> logical 0, widgetMax -> logicalMax
    // where widgetMax = totalHeight - viewport, logicalMax = logicalHeight - viewport
    if (viewportHeight > 0) {
      int widgetMax = totalHeight - viewportHeight;
      int logicalMax = logicalHeight - viewportHeight;
      if (widgetMax <= 0) return widgetScrollY;
      // Linear interpolation: logicalScrollY = widgetScrollY * logicalMax / widgetMax
      return static_cast<int>(static_cast<double>(widgetScrollY) * logicalMax / widgetMax);
    }
    
    // Fallback: simple scaling when viewport not available
    return static_cast<int>(static_cast<double>(widgetScrollY) * scrollScale);
  }
  
  /// Convert logical scroll position to widget scroll position.
  /// When viewportHeight is provided, ensures exact endpoint mapping.
  [[nodiscard]] int toWidgetScrollY(int logicalScrollY, int viewportHeight = 0) const {
    if (scrollScale <= 1.0) return logicalScrollY;
    
    // With viewport, use proper linear interpolation for exact endpoint mapping
    if (viewportHeight > 0) {
      int widgetMax = totalHeight - viewportHeight;
      int logicalMax = logicalHeight - viewportHeight;
      if (logicalMax <= 0) return logicalScrollY;
      // Linear interpolation: widgetScrollY = logicalScrollY * widgetMax / logicalMax
      return static_cast<int>(static_cast<double>(logicalScrollY) * widgetMax / logicalMax);
    }
    
    // Fallback: simple scaling when viewport not available
    return static_cast<int>(static_cast<double>(logicalScrollY) / scrollScale);
  }
};

/**
 * @brief Stateless calculator for grid layout metrics and item positions.
 * 
 * Extracted from ScrollManager to centralize all grid layout calculations.
 * All methods are const/static and produce no side effects.
 */
class GridLayoutCalculator {
public:
  // ─────────────────────────────────────────────────────────────────────────
  // Quick helpers that work directly with CollectionConfig
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Calculate row height from collection config.
   * @param config Collection configuration.
   * @return Height of one row including vertical spacing.
   */
  [[nodiscard]] static int getRowHeight(const CollectionConfig &config) {
    return config.itemHeight + config.verticalSpacing;
  }

  /**
   * @brief Calculate column width from collection config.
   * @param config Collection configuration.
   * @return Width of one column including horizontal spacing.
   */
  [[nodiscard]] static int getColumnWidth(const CollectionConfig &config) {
    return config.itemWidth + config.horizontalSpacing;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Full metric calculation
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Calculate grid metrics from collection configuration.
   * @param config Collection configuration with grid settings.
   * @param totalItems Total number of items to lay out.
   * @return Computed grid metrics.
   */
  [[nodiscard]] static GridMetrics calculateMetrics(
      const CollectionConfig &config, int totalItems);

  /**
   * @brief Recalculate metrics for filtered view with fewer items.
   * @param baseMetrics Original metrics.
   * @param filteredItemCount Number of items after filtering.
   * @return Adjusted metrics for filtered view.
   */
  [[nodiscard]] static GridMetrics adjustForFilter(
      const GridMetrics &baseMetrics, int filteredItemCount);

  /**
   * @brief Calculate position for an item at the given visual index.
   * @param visualIndex Index of the item in visual order.
   * @param metrics Grid metrics to use for calculation.
   * @param isFiltered Whether view is currently filtered.
   * @param filteredItemCount Number of items when filtered.
   * @return Top-left position of the item.
   */
  [[nodiscard]] static QPoint getItemPosition(
      int visualIndex, const GridMetrics &metrics,
      bool isFiltered = false, int filteredItemCount = 0);

  /**
   * @brief Calculate the bounding rectangle for an item.
   * @param visualIndex Index of the item in visual order.
   * @param metrics Grid metrics to use for calculation.
   * @param isFiltered Whether view is currently filtered.
   * @param filteredItemCount Number of items when filtered.
   * @return Bounding rectangle of the item.
   */
  [[nodiscard]] static QRect getItemRect(
      int visualIndex, const GridMetrics &metrics,
      bool isFiltered = false, int filteredItemCount = 0);

  /**
   * @brief Calculate which item index is at a given position.
   * @param pos Position in container coordinates.
   * @param metrics Grid metrics to use for calculation.
   * @param totalItems Total number of items (for bounds checking).
   * @return Item index at position, or -1 if none.
   */
  [[nodiscard]] static int indexAtPosition(
      const QPoint &pos, const GridMetrics &metrics, int totalItems);

  /**
   * @brief Calculate the range of visible rows for a viewport.
   * @param scrollY Current vertical scroll position.
   * @param viewportHeight Height of the visible viewport.
   * @param metrics Grid metrics to use for calculation.
   * @param bufferRows Number of extra rows to include as buffer.
   * @return Pair of (firstVisibleRow, lastVisibleRow).
   */
  [[nodiscard]] static std::pair<int, int> getVisibleRowRange(
      int scrollY, int viewportHeight, const GridMetrics &metrics,
      int bufferRows = UIConstants::Grid::BUFFER_ROWS);

  /**
   * @brief Calculate the range of visible item indices.
   * @param scrollY Current vertical scroll position.
   * @param viewportHeight Height of the visible viewport.
   * @param metrics Grid metrics to use for calculation.
   * @param totalItems Total number of items.
   * @param bufferRows Number of extra rows to include as buffer.
   * @return Pair of (firstVisibleIndex, lastVisibleIndex).
   */
  [[nodiscard]] static std::pair<int, int> getVisibleIndexRange(
      int scrollY, int viewportHeight, const GridMetrics &metrics,
      int totalItems, int bufferRows = UIConstants::Grid::BUFFER_ROWS);

  /**
   * @brief Calculate scroll target to center an item vertically.
   * @param itemIndex Index of the item to center.
   * @param viewportHeight Height of the visible viewport.
   * @param maxScroll Maximum scroll value.
   * @param metrics Grid metrics to use for calculation.
   * @return Target scroll position.
   */
  [[nodiscard]] static int calculateCenterScrollTarget(
      int itemIndex, int viewportHeight, int maxScroll,
      const GridMetrics &metrics);

  /**
   * @brief Calculate row height including spacing.
   * @param metrics Grid metrics.
   * @return Height of one row including vertical spacing.
   */
  [[nodiscard]] static int getRowHeight(const GridMetrics &metrics) {
    return metrics.itemHeight + metrics.verticalSpacing;
  }

  /**
   * @brief Calculate column width including spacing.
   * @param metrics Grid metrics.
   * @return Width of one column including horizontal spacing.
   */
  [[nodiscard]] static int getColumnWidth(const GridMetrics &metrics) {
    return metrics.itemWidth + metrics.horizontalSpacing;
  }
};

#endif // GRIDLAYOUTCALCULATOR_H
