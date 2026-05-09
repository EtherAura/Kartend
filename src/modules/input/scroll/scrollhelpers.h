#ifndef SCROLLHELPERS_H
#define SCROLLHELPERS_H

#include "collectionutils.h" // for HorizontalAlignment

// Pure helpers extracted from scroll-module classes so movement-direction and
// alignment-resolution rules can be unit-tested without instantiating
// SelectionDisplayManager / VirtualContainerManager (which depend on the
// full UI graph).
//
// See bd for the broader extraction effort.
namespace ScrollHelpers {

// Returns true if the selection move from prevIndex -> selectedIndex should be
// treated as a horizontal move (single-row hop) for animation purposes.
//
// Mirrors SelectionDisplayManager::calculateMovementDirection semantics:
// - prevIndex < 0 -> false (no prior selection)
// - Same row -> true (allows rapid click-hold advancing)
// - Adjacent index (|delta| == 1) crossing a row boundary by wrapping at the
//   first/last column counts as horizontal (visually wraps in the grid)
// - Otherwise false
//
// itemsPerRow <= 0 is treated as "no horizontal move" (defensive); the wrap
// detection requires itemsPerRow > 1 to avoid degenerate single-column grids.
[[nodiscard]] auto movementDirection(int selectedIndex, int prevIndex, int itemsPerRow) -> bool;

// Resolves the effective horizontal alignment for the virtual container.
//
// When a search filter is active and the result count is small enough that
// the row would not fill the grid (totalItems < itemsPerRow - 2), forces
// Center alignment so the few visible items aren't stranded on one side.
// Otherwise returns the requested alignment unchanged.
[[nodiscard]] auto effectiveAlignment(HorizontalAlignment requested, bool isFiltered,
                                      int totalItems, int itemsPerRow) -> HorizontalAlignment;

// Picks the database range-chunk size for prefetching as the user drags the
// scrollbar. showAllSubcollectionItems with very large flattened lists
// switches to the larger chunk so we issue fewer DB round-trips.
[[nodiscard]] auto chunkSizeFor(bool showAllSubcollectionItems, int totalItems,
                                int largeThreshold, int defaultChunk, int largeChunk) -> int;

// Result of the slider-position prefetch math. `chunkStart` and `chunkSize`
// are passed straight to ItemWidgetFactory::prefetchRangeAt; `valid` is
// false when the inputs were degenerate (no item height, no items-per-row,
// non-positive chunk constants) and the caller should skip prefetching.
struct SliderPrefetchPlan {
  int chunkStart = 0;
  int chunkSize = 0;
  bool valid = false;
};

// Computes the chunk-aligned media offset to prefetch when the scrollbar
// slider is at `sliderValue` for a grid with the given metrics.
//
// Inputs:
//   sliderValue           - vertical scrollbar value in pixels
//   itemHeight            - row height in pixels (must be > 0)
//   itemsPerRow           - row width (must be > 0)
//   subcollectionCount    - number of subcollection tiles preceding media
//   virtualFolderCount    - number of virtual-folder tiles preceding media
//   showAllSubcollectionItems / totalItems / large-threshold / chunk sizes
//                         - delegated to chunkSizeFor for chunk picking.
//
// The plan's `chunkStart` is clamped to a non-negative multiple of chunkSize.
[[nodiscard]] auto computeSliderPrefetchPlan(int sliderValue, int itemHeight, int itemsPerRow,
                                              int subcollectionCount, int virtualFolderCount,
                                              bool showAllSubcollectionItems, int totalItems,
                                              int largeThreshold, int defaultChunk,
                                              int largeChunk) -> SliderPrefetchPlan;

} // namespace ScrollHelpers

#endif // SCROLLHELPERS_H
