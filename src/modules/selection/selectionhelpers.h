#ifndef SELECTIONHELPERS_H
#define SELECTIONHELPERS_H

// Pure helpers extracted from SelectionManager so click-row detection and
// horizontal-hop animation rules can be unit-tested without instantiating the
// SelectionManager (which depends on ScrollManager + the full UI graph).
//
// See bd Kartend-tty for the broader extraction effort.
namespace SelectionHelpers {

// Returns true if the target index lands on a different row than the
// currently-tracked lastSelectedRow.
//
// - When lastSelectedRow is negative (no row tracked yet), returns true
//   so the first click is always treated as a row change.
// - When gridWidth is <= 0, returns false (defensive).
[[nodiscard]] auto shouldTreatAsNewRow(int targetIndex, int lastSelectedRow,
                                       int gridWidth) -> bool;

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
[[nodiscard]] auto shouldAnimateHorizontalHop(int fromIndex, int toIndex,
                                              int gridWidth) -> bool;

// Returns true if newSelection is on a different row than currentSelection.
//
// - When currentSelection < 0, treats current row as -1 so any non-negative
//   newSelection is reported as a row change.
// - When gridWidth <= 0, returns false (defensive).
[[nodiscard]] auto isNewRow(int currentSelection, int newSelection,
                            int gridWidth) -> bool;

} // namespace SelectionHelpers

#endif // SELECTIONHELPERS_H
