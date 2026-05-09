#include "viewporthelpers.h"

namespace ViewportHelpers {

auto smallMovementThreshold(int lastSelectedRow, int currentRow) -> int {
  // Same-row hops are unlikely to need recentering visually, so allow a
  // bigger no-op band.
  constexpr int kSmallThresholdSameRow = 8;
  constexpr int kSmallThresholdOtherRow = 2;
  return (lastSelectedRow >= 0 && lastSelectedRow == currentRow) ? kSmallThresholdSameRow
                                                                 : kSmallThresholdOtherRow;
}

auto computeForceImmediate(bool immediate, bool forceImmediateCenter, bool isWrappingNavigation,
                            bool restoringSelection, bool instantPositioning,
                            bool wrapSequenceActive) -> bool {
  return immediate || forceImmediateCenter || isWrappingNavigation || restoringSelection ||
         instantPositioning || wrapSequenceActive;
}

} // namespace ViewportHelpers
