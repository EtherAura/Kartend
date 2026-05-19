#include "searchhelpers.h"

#include <algorithm>

namespace SearchHelpers {

auto computeAdaptiveDebounceMs(qint64 timeSinceLastKeystrokeMs, int minDebounceMs,
                               int maxDebounceMs, int defaultDebounceMs) -> int {
  if (timeSinceLastKeystrokeMs <= 0) {
    return defaultDebounceMs;
  }
  const int keystrokeInterval = static_cast<int>(
      std::clamp(timeSinceLastKeystrokeMs, static_cast<qint64>(50), static_cast<qint64>(500)));
  const int range = maxDebounceMs - minDebounceMs;
  const int debounce = minDebounceMs + ((keystrokeInterval - 50) * range) / 450;
  return std::clamp(debounce, minDebounceMs, maxDebounceMs);
}

auto buildSearchModeCycle(const SearchContext &ctx, bool isRoot) -> QVector<SearchMode> {
  QVector<SearchMode> cycle;
  cycle.reserve(3);

  if (isRoot) {
    cycle << (ctx.hasSubs ? SearchMode::CurrentAndSubcollections : SearchMode::CurrentCollection);
    if (ctx.allowAll) {
      cycle << SearchMode::AllCollections;
    }
    return cycle;
  }

  if (ctx.hasSubs) {
    cycle << SearchMode::CurrentAndSubcollections;
    if (ctx.realDirectItems) {
      cycle << SearchMode::CurrentCollection;
    }
    if (ctx.allowAll) {
      cycle << SearchMode::AllCollections;
    }
    return cycle;
  }

  cycle << SearchMode::CurrentCollection;
  if (ctx.allowAll) {
    cycle << SearchMode::AllCollections;
  }
  return cycle;
}

auto allowAllFor(const CollectionConfig &cfg, int collIndex, bool hasSubs,
                 const QList<CollectionConfig> &collections,
                 const HasDirectItemsLookup &hasDirectItemsLookup) -> bool {
  const bool isRoot = (cfg.parentCollectionIndex == -1);
  const bool isLeaf = !hasSubs;

  if (isRoot) {
    if (!hasDirectItemsLookup) {
      return false;
    }
    const int total = collections.size();
    for (int i = 0; i < total; ++i) {
      if (i == collIndex) {
        continue;
      }
      const CollectionConfig &candidate = collections[i];
      if (candidate.parentCollectionIndex == -1 && hasDirectItemsLookup(i)) {
        return true;
      }
    }
    return false;
  }

  if (hasSubs || isLeaf) {
    return true;
  }
  return false;
}

} // namespace SearchHelpers
