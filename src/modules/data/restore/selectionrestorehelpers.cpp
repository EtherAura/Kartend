#include "selectionrestorehelpers.h"

namespace SelectionRestoreHelpers {

bool searchTextIsActive(const QString &rawText) {
  return !rawText.trimmed().isEmpty();
}

bool shouldRestoreSelection(bool rememberSelection, bool searchActive, bool hasScrollManager,
                            bool hasInteractionManager) {
  return rememberSelection && !searchActive && hasScrollManager && hasInteractionManager;
}

int clampRestoreIndex(int selIdx, int total) {
  if (total <= 0) {
    return -1;
  }
  if (selIdx < 0) {
    return -1;
  }
  if (selIdx >= total) {
    return total - 1;
  }
  return selIdx;
}

bool restoreStillValid(int currentCollectionIndex, int scheduledCollectionIndex, int currentToken,
                       int expectedToken) {
  return currentCollectionIndex == scheduledCollectionIndex && currentToken == expectedToken;
}

} // namespace SelectionRestoreHelpers
