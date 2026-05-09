#include "scrollhelpers.h"

#include <cstdlib>

namespace ScrollHelpers {

auto movementDirection(int selectedIndex, int prevIndex, int itemsPerRow) -> bool {
  if (prevIndex < 0) {
    return false;
  }

  // Treat itemsPerRow <= 0 as no row partitioning (defensive).
  if (itemsPerRow <= 0) {
    return false;
  }

  const int prevRow = prevIndex / itemsPerRow;
  const int currRow = selectedIndex / itemsPerRow;

  if (prevRow == currRow) {
    return true;
  }

  const int diff = std::abs(selectedIndex - prevIndex);
  if (diff != 1) {
    return false;
  }

  // Wrap detection requires more than one column.
  if (itemsPerRow <= 1) {
    return false;
  }

  int prevCol = prevIndex % itemsPerRow;
  if (prevCol < 0) {
    prevCol += itemsPerRow;
  }
  int currCol = selectedIndex % itemsPerRow;
  if (currCol < 0) {
    currCol += itemsPerRow;
  }

  const bool wrappedForward = (prevCol == itemsPerRow - 1) && (currCol == 0);
  const bool wrappedBackward = (prevCol == 0) && (currCol == itemsPerRow - 1);
  return wrappedForward || wrappedBackward;
}

auto effectiveAlignment(HorizontalAlignment requested, bool isFiltered, int totalItems,
                        int itemsPerRow) -> HorizontalAlignment {
  if (isFiltered && totalItems > 0 && itemsPerRow > 0) {
    if (totalItems < (itemsPerRow - 2)) {
      return HorizontalAlignment::Center;
    }
  }
  return requested;
}

} // namespace ScrollHelpers
