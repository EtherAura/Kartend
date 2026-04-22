#include "navigationhelpers.h"

#include <algorithm>

namespace NavigationHelpers {

auto computeCollectionDepth(int collectionIndex,
                            const QList<CollectionConfig> &collections)
    -> int {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return 0;
  }
  // Bounded iteration: in the worst case (linear chain) we visit every
  // collection once. Anything beyond that is a cycle and we bail out.
  const int maxIters = collections.size();
  int depth = 0;
  int idx = collectionIndex;
  for (int i = 0; i < maxIters && idx >= 0 && idx < collections.size(); ++i) {
    ++depth;
    int parent = collections[idx].parentCollectionIndex;
    if (parent < 0) {
      return depth;
    }
    idx = parent;
  }
  return depth;
}

auto isValidCollectionIndex(int collectionIndex,
                            const QList<CollectionConfig> &collections)
    -> bool {
  return collectionIndex >= 0 && collectionIndex < collections.size();
}

auto lookupRememberedSelectionIndex(
    int collectionIndex, const QList<CollectionConfig> &collections,
    int totalItems, const SessionLookup &lookup) -> int {
  if (!isValidCollectionIndex(collectionIndex, collections) || totalItems <= 0
      || !lookup) {
    return -1;
  }

  const CollectionConfig &cfg = collections[collectionIndex];
  const bool subfolderActive = !cfg.currentSubfolder.trimmed().isEmpty();

  int selIdx = -1;
  if (subfolderActive) {
    selIdx =
        lookup(CollectionUtils::selectionSessionKeyFor(cfg, collections));
  } else {
    selIdx = lookup(CollectionUtils::hierarchicalNameFor(cfg, collections));
    if (selIdx < 0) {
      selIdx = lookup(cfg.name);
    }
  }

  if (selIdx < 0) {
    return -1;
  }
  if (selIdx >= totalItems) {
    selIdx = totalItems - 1;
  }
  return std::max(selIdx, 0);
}

auto calculateSelectionIndex(int collectionIndex,
                             const QList<CollectionConfig> &collections,
                             int totalItems, bool searchActive,
                             bool rememberSelectionEnabled,
                             const SessionLookup &lookup) -> int {
  if (searchActive || !rememberSelectionEnabled || totalItems <= 0) {
    return -1;
  }
  const int selIdx = lookupRememberedSelectionIndex(
      collectionIndex, collections, totalItems, lookup);
  if (selIdx < 0) {
    return 0;
  }
  return selIdx;
}

} // namespace NavigationHelpers
