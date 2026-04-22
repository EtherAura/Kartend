#ifndef NAVIGATIONHELPERS_H
#define NAVIGATIONHELPERS_H

#include "collectionutils.h"
#include <QList>
#include <QString>
#include <functional>

// Pure helpers extracted from NavigationManager so they can be unit-tested
// without instantiating the full UI/manager graph.
//
// All functions are stateless and free of Qt object dependencies. They take
// their inputs explicitly so tests can construct minimal fixtures.
//
// See bd Kartend-tty for the broader extraction effort.
namespace NavigationHelpers {

// Walks the parentCollectionIndex chain and returns 1-based depth.
//
// - Top-level collection (parent == -1) -> depth 1.
// - Returns 0 for an out-of-range or negative collectionIndex.
// - Cycles in the parent chain are bounded by the size of the collection
//   list (worst case: every collection is one link in a chain).
[[nodiscard]] auto computeCollectionDepth(
    int collectionIndex, const QList<CollectionConfig> &collections) -> int;

// Validates that an index refers to a real entry in the collections list.
//
// Returns false for negative indices, out-of-range indices, or when the list
// is empty. This is a thin pure wrapper around the existing
// CollectionUtils::isValidIndex(int, list) overload — it exists here so the
// helper namespace can be a single discoverable surface.
[[nodiscard]] auto isValidCollectionIndex(
    int collectionIndex, const QList<CollectionConfig> &collections) -> bool;

// Looks up the index of the remembered selection for a collection.
//
// Pure version of NavigationManager::lookupRememberedSelectionIndex — instead
// of holding a SessionManager pointer, it accepts a callable that maps a
// session key to a stored index (or -1 if absent). Tests pass a lambda backed
// by a QHash; production code passes a lambda calling
// SessionManager::getLastSelectedIndex.
//
// Behavior:
// - When a subfolder is active for the collection, looks up under the
//   subfolder-aware key from CollectionUtils::selectionSessionKeyFor.
// - Otherwise tries the hierarchical name, then falls back to the bare
//   collection name (matches legacy session keys).
// - Clamps the result into [0, totalItems-1]; returns -1 if no remembered
//   index exists or totalItems <= 0 or the collectionIndex is invalid.
using SessionLookup = std::function<int(const QString &)>;
[[nodiscard]] auto lookupRememberedSelectionIndex(
    int collectionIndex, const QList<CollectionConfig> &collections,
    int totalItems, const SessionLookup &lookup) -> int;

// Calculates the selection index to restore after items load.
//
// Pure version of NavigationManager::calculateSelectionIndex.
//
// - Returns -1 when search is active, when remembering is disabled, or when
//   totalItems <= 0 (signals "no restore, leave selection as is").
// - Returns 0 when remember is enabled but no remembered index is found.
// - Otherwise returns the clamped remembered index from `lookup`.
[[nodiscard]] auto calculateSelectionIndex(
    int collectionIndex, const QList<CollectionConfig> &collections,
    int totalItems, bool searchActive, bool rememberSelectionEnabled,
    const SessionLookup &lookup) -> int;

} // namespace NavigationHelpers

#endif // NAVIGATIONHELPERS_H
