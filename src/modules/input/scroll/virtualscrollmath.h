#ifndef VIRTUALSCROLLMATH_H
#define VIRTUALSCROLLMATH_H

#include <algorithm>

// Pure visible-index-band arithmetic extracted from
// VirtualScrollEngine::calculateNeededIndices (Kartend-tacgk).
//
// The engine itself can only be exercised with a live ScrollManager + widget
// viewport, which buried the band/overscan/clamp math behind untestable
// state. These functions take plain integers — no Qt, no ScrollManager — so
// the edge cases (no items, a single item, an item taller than the viewport,
// the partially-filled final row) can be unit-tested directly.
namespace VirtualScrollMath {

// Inclusive visual-index band. lastIndex < firstIndex denotes an empty band.
struct IndexBand {
  int firstIndex = 0;
  int lastIndex = -1;
  [[nodiscard]] bool isEmpty() const { return lastIndex < firstIndex; }
  [[nodiscard]] int size() const { return isEmpty() ? 0 : (lastIndex - firstIndex + 1); }
  [[nodiscard]] bool contains(int idx) const { return idx >= firstIndex && idx <= lastIndex; }
};

// Maps a visible *row* range [firstVisibleRow, lastVisibleRow] to the
// contiguous band of item indices that must be materialized, padded by
// `overscanRows` on each side and clamped to the collection bounds. Returns an
// empty band when there is nothing to show: no columns (itemsPerRow <= 0), no
// items, or a padded range that falls entirely past the last populated row.
[[nodiscard]] inline IndexBand neededIndexBand(int firstVisibleRow, int lastVisibleRow,
                                               int itemsPerRow, int totalItems,
                                               int overscanRows = 1) {
  const int startRow = std::max(0, firstVisibleRow - overscanRows);
  int endRow = lastVisibleRow + overscanRows;

  if (itemsPerRow <= 0 || totalItems <= 0) {
    return {};
  }
  // Last row that actually holds an item (ceil(totalItems / itemsPerRow) - 1).
  const int maxRow = ((totalItems + itemsPerRow - 1) / itemsPerRow) - 1;
  if (maxRow < 0) {
    return {};
  }
  endRow = std::min(endRow, maxRow);
  if (endRow < startRow) {
    return {};
  }

  IndexBand band;
  band.firstIndex = startRow * itemsPerRow;
  // The final row may be partially filled, so clamp to the last valid index.
  band.lastIndex = std::min(((endRow + 1) * itemsPerRow) - 1, totalItems - 1);
  return band;
}

} // namespace VirtualScrollMath

#endif // VIRTUALSCROLLMATH_H
