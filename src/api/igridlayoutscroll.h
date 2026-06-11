#ifndef IGRIDLAYOUTSCROLL_H
#define IGRIDLAYOUTSCROLL_H

#include "collection/collectionconfig.h"
#include <QList>

struct GridMetrics;

/**
 * @brief Grid-metrics / layout role of the scroll layer (Kartend-h1l8f).
 *
 * One of the six role interfaces IScrollManager unions: grid dimensions,
 * layout (re)calculation, view-type switching, container centering, and the
 * virtual-view refresh + active-widget teardown that layout changes drive.
 * Matches the VirtualScrollEngine / GridLayoutCalculator side of the
 * implementation split.
 *
 * Plain abstract class, not a QObject — ScrollManager derives QObject once,
 * through IScrollManager. Reached via ctx->scrollGrid().
 */
class IGridLayoutScroll {
public:
  virtual ~IGridLayoutScroll() = default;

  virtual void updateGridWidth(int newGridWidth) = 0;
  /// Live-apply a change to the per-collection items-per-column for the
  /// Horizontal view mode. No-op when the active view is not Horizontal.
  /// Pass 0 to mean "fall back to gridWidth".
  virtual void updateHorizontalGridHeight(int newHorizontalGridHeight) = 0;
  /// Track whether the sidebar is currently hidden AND its mode would shrink
  /// the grid (Expand). When true and the active collection configures
  /// sidebar-hidden grid sizes, those override the primary ones.
  virtual void setSidebarShrinkingActive(bool active) = 0;
  [[nodiscard]] virtual bool sidebarShrinkingActive() const = 0;
  virtual void updateViewType(ViewType viewType) = 0;
  virtual void updateVirtualView() = 0;
  virtual void forceVirtualViewUpdate() = 0;
  [[nodiscard]] virtual int getEffectiveHorizontalSpacing() const = 0;
  [[nodiscard]] virtual const GridMetrics &getMetrics() const = 0;
  [[nodiscard]] virtual int getCurrentGridWidth() const = 0;
  virtual void recreateLayout() = 0;
  virtual void recalculateContainerMetrics() = 0;
  virtual void preCalculateLayout() = 0;
  virtual void primeLayoutFor(const CollectionConfig &config) = 0;
  virtual void handleLayoutChange() = 0;
  virtual void centerHorizontalScrollbar(int currentCollectionIndex,
                                         const QList<CollectionConfig> &collections) = 0;
  virtual void recenterVirtualContainer() = 0;
  /// Release every active widget back to the pool and clear the active set
  /// (view-preserving teardown; the data model stays intact).
  virtual void cleanupActiveWidgets() = 0;
};

#endif // IGRIDLAYOUTSCROLL_H
