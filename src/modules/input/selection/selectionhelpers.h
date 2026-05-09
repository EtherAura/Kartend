#ifndef SELECTIONHELPERS_H
#define SELECTIONHELPERS_H

#include <QtGlobal>

// Pure helpers extracted from SelectionManager so click-row detection and
// horizontal-hop animation rules can be unit-tested without instantiating the
// SelectionManager (which depends on ScrollManager + the full UI graph).
//
// See bd for the broader extraction effort.
namespace SelectionHelpers {

// Returns true if the target index lands on a different row than the
// currently-tracked lastSelectedRow.
//
// - When lastSelectedRow is negative (no row tracked yet), returns true
//   so the first click is always treated as a row change.
// - When gridWidth is <= 0, returns false (defensive).
[[nodiscard]] auto shouldTreatAsNewRow(int targetIndex, int lastSelectedRow, int gridWidth) -> bool;

// Returns true if a click should run the horizontal-hop selection animation.
//
// The hop animation is reserved for selection moves that:
// - Stay on the same row (same `index / gridWidth`), AND
// - Skip over at least one item (|to - from| > 1).
//
// Returns false for:
// - fromIndex < 0 (no prior selection)
// - gridWidth <= 0 (defensive)
// - Different row
// - Adjacent click (|to - from| == 1) — the animation only applies to skips
[[nodiscard]] auto shouldAnimateHorizontalHop(int fromIndex, int toIndex, int gridWidth) -> bool;

// Returns true if newSelection is on a different row than currentSelection.
//
// - When currentSelection < 0, treats current row as -1 so any non-negative
//   newSelection is reported as a row change.
// - When gridWidth <= 0, returns false (defensive).
[[nodiscard]] auto isNewRow(int currentSelection, int newSelection, int gridWidth) -> bool;

// Direction sign for a horizontal hop animation between `from` and `to`.
// Returns +1 when target is to the right, -1 when to the left, 0 when equal.
[[nodiscard]] auto hopDirection(int fromIndex, int toIndex) -> int;

// Total number of intermediate ticks a hop animation will fire (the count
// passed to the for-loop in runHorizontalHopAnimation). Returns 0 for
// degenerate hops (from == to).
[[nodiscard]] auto hopStepCount(int fromIndex, int toIndex) -> int;

// Computes the i-th intermediate index for a hop animation, with `i` in the
// 1-based range [1, hopStepCount(...)]. Out-of-range values return -1.
//
// At i == hopStepCount(...) the helper returns `toIndex` itself; the caller
// uses that as the "final landing" tick.
[[nodiscard]] auto hopIntermediateIndex(int fromIndex, int toIndex, int stepIndex) -> int;

// Returns true when a remembered row-change first-click is still within the
// system's double-click window relative to `nowMs`.
//
// `pendingIndex < 0` means "no pending click" and always returns false.
// `doubleClickIntervalMs <= 0` is treated as no window (false).
// Negative deltas (clock skew, wrap) collapse to false.
[[nodiscard]] auto isRowChangePendingValid(int pendingIndex, qint64 pendingMs, qint64 nowMs,
                                            int doubleClickIntervalMs) -> bool;

} // namespace SelectionHelpers

#endif // SELECTIONHELPERS_H
