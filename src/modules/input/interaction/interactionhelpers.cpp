#include "interactionhelpers.h"

#include <algorithm>

namespace InteractionHelpers {

auto stepSizeForViewType(ViewType viewType, int gridWidth) -> int {
  if (viewType == ViewType::List || viewType == ViewType::CoverFlow) {
    return 1;
  }
  return gridWidth > 0 ? gridWidth : 1;
}

auto computeVerticalHoldStep(int currentIndex, int direction, int stepSize, int totalItems,
                             bool wrap) -> HoldScrollStep {
  HoldScrollStep result;
  if (totalItems <= 0 || stepSize <= 0) {
    return result;
  }

  const int safeCurrent = std::max(0, currentIndex);
  int next = safeCurrent + (direction * stepSize);

  if (wrap) {
    if (next < 0) {
      // Modulo for negatives: ((next % N) + N) % N maps any negative back into
      // [0, totalItems). This mirrors the production wrap rule used by
      // InteractionManager::onMouseHoldScrollStep before extraction.
      next = ((next % totalItems) + totalItems) % totalItems;
      result.didWrap = true;
    } else if (next >= totalItems) {
      next = next % totalItems;
      result.didWrap = true;
    }
  } else {
    next = std::max(next, 0);
    if (next >= totalItems) {
      next = totalItems - 1;
    }
  }

  result.nextIndex = next;
  return result;
}

auto resolveOwnerIndex(int dbIndex, int fallbackIndex, int collectionsSize) -> int {
  if (dbIndex >= 0 && dbIndex < collectionsSize) {
    return dbIndex;
  }
  if (fallbackIndex >= 0 && fallbackIndex < collectionsSize) {
    return fallbackIndex;
  }
  return -1;
}

} // namespace InteractionHelpers
