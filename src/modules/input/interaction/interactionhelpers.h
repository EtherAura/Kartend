#ifndef INTERACTIONHELPERS_H
#define INTERACTIONHELPERS_H

#include "collectiontypes.h"

// Pure helpers extracted from InteractionManager so they can be unit-tested
// without instantiating the full UI/manager graph.
//
// All functions are stateless and free of Qt object dependencies; they take
// their inputs explicitly so tests can construct minimal fixtures.
namespace InteractionHelpers {

// Returns the stepSize the mouse-hold vertical scroller should advance by.
//
// List and CoverFlow views step by one item per tick. Grid and Horizontal
// views step by a full row (gridWidth). When `gridWidth <= 0` the helper
// returns 1 to keep the scroller usable rather than freezing on an unknown
// layout.
[[nodiscard]] auto stepSizeForViewType(ViewType viewType, int gridWidth) -> int;

// Result of a single vertical hold-scroll tick: the new selection index and
// whether the index wrapped around (so callers can apply force-immediate
// centering and wrap-sequence flags).
struct HoldScrollStep {
  int nextIndex = -1;
  bool didWrap = false;
};

// Computes one vertical mouse-hold-scroll step.
//
// - `direction` is +1 (down) or -1 (up).
// - `stepSize` is the number of items to advance per tick (see stepSizeForViewType).
// - When `wrap` is true, an out-of-range step modulo-wraps to the other end
//   and `didWrap` is set. When false, the result clamps to [0, totalItems-1].
// - When totalItems <= 0 the helper returns {-1, false} (caller stops scroll).
// - When the computed nextIndex equals currentIndex (e.g. clamped at the
//   edge) the helper still returns the value; the caller decides whether a
//   no-op step should short-circuit downstream work.
[[nodiscard]] auto computeVerticalHoldStep(int currentIndex, int direction, int stepSize,
                                           int totalItems, bool wrap) -> HoldScrollStep;

// Picks the effective collection index for an item launch / preview action.
//
// Production call sites pass `dbIndex` from
// DatabaseManager::getCollectionIndexForFile (the file's owning collection,
// or -1 if unmapped) and `fallbackIndex` from the currently-viewed collection.
//
// Returns `dbIndex` when it points into the collections list; otherwise the
// `fallbackIndex` if that is in range; otherwise -1.
[[nodiscard]] auto resolveOwnerIndex(int dbIndex, int fallbackIndex, int collectionsSize) -> int;

} // namespace InteractionHelpers

#endif // INTERACTIONHELPERS_H
