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
  /// Items along the *fixed* dimension. In Grid/List/CoverFlow this is
  /// items-per-row (column count). In Horizontal mode the axis
  /// flips and this is items-per-column (row count) — the field name is kept
  /// for source compatibility across the hundreds of callers.
  int itemsPerRow = 0;
  int horizontalSpacing = 0;
  int verticalSpacing = 0;
  int margins = 0;
  int totalWidth = 0;
  int totalHeight = 0;   // Clamped height for Qt widget (≤ QWIDGETSIZE_MAX)
  int logicalHeight = 0; // True logical height (may exceed Qt limits)
  int actualGridWidth = 0;
  /// Rows along the *long/scrolling* dimension. In Horizontal mode this is the
  /// column count instead; helpers that compute "first/last row" treat it as
  /// "first/last long-axis index" regardless.
  int totalRows = 0;
  double scrollScale = 1.0; // Scale factor: logicalHeight / totalHeight
  bool isClipped = false;   // True if logicalHeight > QWIDGETSIZE_MAX
  int overflowAmount = 0;   // logicalHeight - totalHeight when clipped
  int headerOffset = 0;     // Offset for list header in list view mode
  /// when true, the items area scrolls along x instead of y.
  /// Set during `calculateMetrics` from `config.viewType == Horizontal`.
  /// Downstream code uses this in lieu of re-reading the config.
  bool isHorizontal = false;

  [[nodiscard]] bool isValid() const { return itemWidth > 0 && itemHeight > 0 && itemsPerRow > 0; }

  /// Convert widget scroll position to logical scroll position.
  /// When viewportHeight is provided, ensures scrollbar max maps exactly to
  /// (logicalHeight - viewport) for precise endpoint mapping.
  [[nodiscard]] int toLogicalScrollY(int widgetScrollY, int viewportHeight = 0) const {
    if (scrollScale <= 1.0) return widgetScrollY;

    // With viewport, use proper linear interpolation for exact endpoint
    // mapping: widget 0 -> logical 0, widgetMax -> logicalMax where widgetMax =
    // totalHeight - viewport, logicalMax = logicalHeight - viewport
    if (viewportHeight > 0) {
      int widgetMax = totalHeight - viewportHeight;
      int logicalMax = logicalHeight - viewportHeight;
      if (widgetMax <= 0) return widgetScrollY;
      // Linear interpolation: logicalScrollY = widgetScrollY * logicalMax /
      // widgetMax
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
      // Linear interpolation: widgetScrollY = logicalScrollY * widgetMax /
      // logicalMax
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
    if (config.viewType == ViewType::List) {
      return UIConstants::ListView::DEFAULT_ROW_HEIGHT + UIConstants::ListView::ROW_SPACING;
    }
    return config.gridLayout.itemHeight + config.gridLayout.verticalSpacing;
  }

  /**
   * @brief Calculate column width from collection config.
   * @param config Collection configuration.
   * @return Width of one column including horizontal spacing.
   */
  [[nodiscard]] static int getColumnWidth(const CollectionConfig &config) {
    return config.gridLayout.itemWidth + config.gridLayout.horizontalSpacing;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Full metric calculation
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Calculate grid metrics from collection configuration.
   * @param config Collection configuration with grid settings.
   * @param totalItems Total number of items to lay out.
   * @param sidebarShrinkingActive: when true, the sidebar is hidden
   *        AND its mode would shrink the grid (Expand) — apply the alternate
   *        gridWidthSidebarHidden / horizontalGridHeightSidebarHidden values
   *        when configured. Defaults to false so existing call sites that don't
   *        care about sidebar state keep using the primary fields.
   * @return Computed grid metrics.
   */
  [[nodiscard]] static GridMetrics calculateMetrics(const CollectionConfig &config, int totalItems,
                                                    bool sidebarShrinkingActive = false);

  /**
   * @brief Recalculate metrics for filtered view with fewer items.
   * @param baseMetrics Original metrics.
   * @param filteredItemCount Number of items after filtering.
   * @return Adjusted metrics for filtered view.
   */
  [[nodiscard]] static GridMetrics adjustForFilter(const GridMetrics &baseMetrics,
                                                   int filteredItemCount);

  /**
   * @brief Calculate position for an item at the given visual index.
   * @param visualIndex Index of the item in visual order.
   * @param metrics Grid metrics to use for calculation.
   * @param isFiltered Whether view is currently filtered.
   * @param filteredItemCount Number of items when filtered.
   * @return Top-left position of the item.
   */
  [[nodiscard]] static QPoint getItemPosition(int visualIndex, const GridMetrics &metrics,
                                              bool isFiltered = false, int filteredItemCount = 0);

  /**
   * @brief Calculate the bounding rectangle for an item.
   * @param visualIndex Index of the item in visual order.
   * @param metrics Grid metrics to use for calculation.
   * @param isFiltered Whether view is currently filtered.
   * @param filteredItemCount Number of items when filtered.
   * @return Bounding rectangle of the item.
   */
  [[nodiscard]] static QRect getItemRect(int visualIndex, const GridMetrics &metrics,
                                         bool isFiltered = false, int filteredItemCount = 0);

  /**
   * @brief Calculate which item index is at a given position.
   * @param pos Position in container coordinates.
   * @param metrics Grid metrics to use for calculation.
   * @param totalItems Total number of items (for bounds checking).
   * @return Item index at position, or -1 if none.
   */
  [[nodiscard]] static int indexAtPosition(const QPoint &pos, const GridMetrics &metrics,
                                           int totalItems);

  /**
   * @brief Calculate the range of visible "rows" along the long axis.
   *
   * In Grid/List/CoverFlow this is rows (vertical scroll position +
   * viewport height). In Horizontal mode the same call
   * is reinterpreted as columns (horizontal scroll position + viewport
   * width); the caller picks which scrollbar to read from `metrics.isHorizontal`.
   *
   * @param scrollPos Current scroll position along the long axis.
   * @param viewportSize Size of the viewport along the long axis.
   * @param metrics Grid metrics (drives the axis interpretation).
   * @param bufferRows Number of extra rows/columns to include as buffer.
   * @return Pair of (firstVisible, lastVisible) along the long axis.
   */
  [[nodiscard]] static std::pair<int, int>
  getVisibleRowRange(int scrollPos, int viewportSize, const GridMetrics &metrics,
                     int bufferRows = UIConstants::Grid::BUFFER_ROWS);

  /**
   * @brief Calculate the range of visible item indices.
   * @param scrollPos Current scroll position along the long axis.
   * @param viewportSize Size of the viewport along the long axis.
   * @param metrics Grid metrics to use for calculation.
   * @param totalItems Total number of items.
   * @param bufferRows Number of extra rows to include as buffer.
   * @return Pair of (firstVisibleIndex, lastVisibleIndex).
   */
  [[nodiscard]] static std::pair<int, int>
  getVisibleIndexRange(int scrollPos, int viewportSize, const GridMetrics &metrics, int totalItems,
                       int bufferRows = UIConstants::Grid::BUFFER_ROWS);

  /**
   * @brief Calculate scroll target to center an item along the long axis.
   *
   * In Grid this centers vertically; in Horizontal it centers horizontally.
   *
   * @param itemIndex Index of the item to center.
   * @param viewportSize Size of the viewport along the long axis.
   * @param maxScroll Maximum scroll value.
   * @param metrics Grid metrics to use for calculation.
   * @return Target scroll position.
   */
  [[nodiscard]] static int calculateCenterScrollTarget(int itemIndex, int viewportSize,
                                                       int maxScroll, const GridMetrics &metrics);

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
