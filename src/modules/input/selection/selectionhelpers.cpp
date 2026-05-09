#include "selectionhelpers.h"

#include <cstdlib>

namespace SelectionHelpers {

auto shouldTreatAsNewRow(int targetIndex, int lastSelectedRow, int gridWidth) -> bool {
  if (gridWidth <= 0) {
    return false;
  }
  const int targetRow = targetIndex / gridWidth;
  return (lastSelectedRow < 0) || (targetRow != lastSelectedRow);
}

auto shouldAnimateHorizontalHop(int fromIndex, int toIndex, int gridWidth) -> bool {
  if (fromIndex < 0 || gridWidth <= 0) {
    return false;
  }
  return (fromIndex / gridWidth) == (toIndex / gridWidth) && std::abs(toIndex - fromIndex) > 1;
}

auto isNewRow(int currentSelection, int newSelection, int gridWidth) -> bool {
  if (gridWidth <= 0) {
    return false;
  }
  const int currentRow = (currentSelection >= 0) ? currentSelection / gridWidth : -1;
  const int targetRow = newSelection / gridWidth;
  return currentRow != targetRow;
}

auto hopDirection(int fromIndex, int toIndex) -> int {
  if (toIndex > fromIndex) {
    return 1;
  }
  if (toIndex < fromIndex) {
    return -1;
  }
  return 0;
}

auto hopStepCount(int fromIndex, int toIndex) -> int {
  return std::abs(toIndex - fromIndex);
}

auto hopIntermediateIndex(int fromIndex, int toIndex, int stepIndex) -> int {
  const int total = hopStepCount(fromIndex, toIndex);
  if (total == 0 || stepIndex < 1 || stepIndex > total) {
    return -1;
  }
  return fromIndex + (stepIndex * hopDirection(fromIndex, toIndex));
}

auto isRowChangePendingValid(int pendingIndex, qint64 pendingMs, qint64 nowMs,
                             int doubleClickIntervalMs) -> bool {
  if (pendingIndex < 0 || doubleClickIntervalMs <= 0) {
    return false;
  }
  const qint64 delta = nowMs - pendingMs;
  return delta >= 0 && delta <= doubleClickIntervalMs;
}

} // namespace SelectionHelpers
