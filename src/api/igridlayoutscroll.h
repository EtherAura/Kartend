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

  /// True when Cover Flow is the active view and its carousel has somewhere to
  /// go (more than one card). Attract mode's autoscroll drives the item scroll
  /// area's scrollbar, which Cover Flow hides with both policies forced off, so
  /// `bar->maximum() > 0` is always false there — this is the equivalent
  /// "is there room to move" test for the carousel (Kartend-wmxwg).
  [[nodiscard]] virtual bool coverFlowDriftable() const = 0;

  /// Move the Cover Flow carousel by @p px pixels of horizontal travel, signed,
  /// carrying the canonical selection with it. Returns false once the drift
  /// reaches either end — the caller's cue to bounce, matching what
  /// AttractHelpers::nextScrollPosition reports for a scrollbar. No-op
  /// returning false when Cover Flow is not the active view.
  virtual bool driftCoverFlow(qreal px) = 0;

  /// Glide the Cover Flow carousel back onto its selected card, cancelling any
  /// fractional offset a drift left behind. Called when attract stops, because
  /// a drift halts on whatever sub-card position the last tick reached — which
  /// leaves the selected card visibly off-centre until the next selection
  /// change (Kartend-wmxwg). No-op when Cover Flow is not the active view or
  /// the carousel is already centred.
  virtual void settleCoverFlow() = 0;
};

#endif // IGRIDLAYOUTSCROLL_H
