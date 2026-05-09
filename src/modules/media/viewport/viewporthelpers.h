#ifndef VIEWPORTHELPERS_H
#define VIEWPORTHELPERS_H

// Pure helpers extracted from ViewportManager so the small-movement /
// force-immediate decisions can be unit-tested without instantiating
// ViewportManager (which depends on ScrollManager / SelectionManager /
// AnimationManager / InteractionStateHolder / a real QScrollArea).
namespace ViewportHelpers {

// Returns the row-distance threshold below which a centering operation is
// treated as a "small movement" (no animation, just snap). Same-row hops
// get a more permissive threshold so left/right within a row never
// re-centers; cross-row hops are stricter.
//
// lastSelectedRow == -1 means "no prior row known" -> use the cross-row
// threshold so first selections don't get the same-row bonus.
[[nodiscard]] auto smallMovementThreshold(int lastSelectedRow, int currentRow) -> int;

// Combines the six "treat this center as immediate" boolean signals into a
// single decision. Mirrors ViewportManager::computeForceImmediate without
// the SelectionManager pointer indirection — that pointer is collapsed to
// the boolean it ends up resolving to.
//
// Returns true when ANY of:
// - immediate (caller-supplied flag)
// - forceImmediateCenter (manager state)
// - isWrappingNavigation (mid-wrap-around)
// - restoringSelection (mid-restore)
// - instantPositioning (initial positioning)
// - wrapSequenceActive (active wrap sequence)
[[nodiscard]] auto computeForceImmediate(bool immediate, bool forceImmediateCenter,
                                          bool isWrappingNavigation, bool restoringSelection,
                                          bool instantPositioning, bool wrapSequenceActive) -> bool;

} // namespace ViewportHelpers

#endif // VIEWPORTHELPERS_H
