#include "selectionhelpers.h"

#include <cstdlib>

namespace SelectionHelpers {

auto shouldTreatAsNewRow(int targetIndex, int lastSelectedRow, int gridWidth)
    -> bool {
  if (gridWidth <= 0) {
    return false;
  }
  const int targetRow = targetIndex / gridWidth;
  return (lastSelectedRow < 0) || (targetRow != lastSelectedRow);
}

auto shouldAnimateHorizontalHop(int fromIndex, int toIndex, int gridWidth)
    -> bool {
  if (fromIndex < 0 || gridWidth <= 0) {
    return false;
  }
  return (fromIndex / gridWidth) == (toIndex / gridWidth)
         && std::abs(toIndex - fromIndex) > 1;
}

auto isNewRow(int currentSelection, int newSelection, int gridWidth) -> bool {
  if (gridWidth <= 0) {
    return false;
  }
  const int currentRow =
      (currentSelection >= 0) ? currentSelection / gridWidth : -1;
  const int targetRow = newSelection / gridWidth;
  return currentRow != targetRow;
}

} // namespace SelectionHelpers
