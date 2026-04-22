#ifndef SCROLLHELPERS_H
#define SCROLLHELPERS_H

#include "collectionutils.h" // for HorizontalAlignment

// Pure helpers extracted from scroll-module classes so movement-direction and
// alignment-resolution rules can be unit-tested without instantiating
// SelectionDisplayManager / VirtualContainerManager (which depend on the
// full UI graph).
//
// See bd Kartend-tty for the broader extraction effort.
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
[[nodiscard]] auto movementDirection(int selectedIndex, int prevIndex,
                                     int itemsPerRow) -> bool;

// Resolves the effective horizontal alignment for the virtual container.
//
// When a search filter is active and the result count is small enough that
// the row would not fill the grid (totalItems < itemsPerRow - 2), forces
// Center alignment so the few visible items aren't stranded on one side.
// Otherwise returns the requested alignment unchanged.
[[nodiscard]] auto effectiveAlignment(HorizontalAlignment requested,
                                      bool isFiltered, int totalItems,
                                      int itemsPerRow) -> HorizontalAlignment;

} // namespace ScrollHelpers

#endif // SCROLLHELPERS_H
