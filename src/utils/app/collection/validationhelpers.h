#ifndef KARTEND_COLLECTION_VALIDATIONHELPERS_H
#define KARTEND_COLLECTION_VALIDATIONHELPERS_H

// CollectionUtils index-validation and effective grid-sizing helpers. Split
// out of the former collection/helpers.h grab-bag. These inline bodies touch
// CollectionConfig members directly, so the complete struct is required.

#include "collectionconfig.h"

#include <QList>

#include <uiconstants/grid.h>

namespace CollectionUtils {

// ─── Index validation helpers ────────────────────────────────────────────────

/// Validates index against collection pointer (null-safe)
[[nodiscard]] inline bool isValidIndex(int index, const QList<CollectionConfig> *collections) {
  return collections && index >= 0 && index < collections->size();
}

/// Validates index pointer against collection pointer (null-safe for both)
[[nodiscard]] inline bool isValidIndex(const int *indexPtr,
                                       const QList<CollectionConfig> *collections) {
  return indexPtr && isValidIndex(*indexPtr, collections);
}

/// True when the index points at a real collection OR the synthetic
/// root / home view (index -1). The home view renders the root
/// collections as tiles but has no backing collection; input handlers
/// gate on this (instead of isValidIndex) so it still accepts keyboard
/// + mouse interaction. Callers that then read a per-collection
/// property must still guard that access with isValidIndex — only the
/// outer "is this an interactive view" gate is widened.
[[nodiscard]] inline bool isInteractiveViewIndex(const int *indexPtr,
                                                 const QList<CollectionConfig> *collections) {
  return isValidIndex(indexPtr, collections) || (indexPtr && *indexPtr == -1);
}

/// Validates index against collection reference (no null check needed)
[[nodiscard]] inline bool isValidIndex(int index, const QList<CollectionConfig> &collections) {
  return index >= 0 && index < collections.size();
}

// ─── Collection property accessors with validation ───────────────────────────

/// Get grid width with fallback to default - reduces duplication across
/// managers
[[nodiscard]] inline int getGridWidth(const int *indexPtr,
                                      const QList<CollectionConfig> *collections) {
  if (!isValidIndex(indexPtr, collections)) {
    return UIConstants::Grid::DEFAULT_WIDTH;
  }
  return (*collections)[*indexPtr].gridLayout.gridWidth;
}

// ─── Effective grid sizing helpers ───────────────────────────────────────────
//
// "Sidebar shrinking active" is the predicate captured by the caller: sidebar
// is currently hidden AND the collection's sidebarMode is Expand (i.e. the
// sidebar would push the grid when shown). Overlay mode never shrinks, so we
// always use the primary value there. The alt fields default to 0, which means
// "inherit the primary" — a fresh upgrade keeps existing layout behavior.

[[nodiscard]] inline int effectiveGridWidth(const CollectionConfig &config,
                                            bool sidebarShrinkingActive) {
  if (sidebarShrinkingActive && config.gridLayout.gridWidthSidebarHidden > 0) {
    return config.gridLayout.gridWidthSidebarHidden;
  }
  return config.gridLayout.gridWidth;
}

[[nodiscard]] inline int effectiveHorizontalGridHeight(const CollectionConfig &config,
                                                       bool sidebarShrinkingActive) {
  if (sidebarShrinkingActive && config.gridLayout.horizontalGridHeightSidebarHidden > 0) {
    return config.gridLayout.horizontalGridHeightSidebarHidden;
  }
  return config.gridLayout.horizontalGridHeight;
}

} // namespace CollectionUtils

#endif // KARTEND_COLLECTION_VALIDATIONHELPERS_H
