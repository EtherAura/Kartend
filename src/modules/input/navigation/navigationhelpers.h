#ifndef NAVIGATIONHELPERS_H
#define NAVIGATIONHELPERS_H

#include "collectionutils.h"
#include <functional>
#include <QList>
#include <QString>

// Pure helpers extracted from NavigationManager so they can be unit-tested
// without instantiating the full UI/manager graph.
//
// All functions are stateless and free of Qt object dependencies. They take
// their inputs explicitly so tests can construct minimal fixtures.
//
// See bd for the broader extraction effort.
namespace NavigationHelpers {

// Walks the parentCollectionIndex chain and returns 1-based depth.
//
// - Top-level collection (parent == -1) -> depth 1.
// - Returns 0 for an out-of-range or negative collectionIndex.
// - Cycles in the parent chain are bounded by the size of the collection
//   list (worst case: every collection is one link in a chain).
[[nodiscard]] auto computeCollectionDepth(int collectionIndex,
                                          const QList<CollectionConfig> &collections) -> int;

// Validates that an index refers to a real entry in the collections list.
//
// Returns false for negative indices, out-of-range indices, or when the list
// is empty. This is a thin pure wrapper around the existing
// CollectionUtils::isValidIndex(int, list) overload — it exists here so the
// helper namespace can be a single discoverable surface.
[[nodiscard]] auto isValidCollectionIndex(int collectionIndex,
                                          const QList<CollectionConfig> &collections) -> bool;

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
[[nodiscard]] auto lookupRememberedSelectionIndex(int collectionIndex,
                                                  const QList<CollectionConfig> &collections,
                                                  int totalItems, const SessionLookup &lookup)
    -> int;

// Calculates the selection index to restore after items load.
//
// Pure version of NavigationManager::calculateSelectionIndex.
//
// - Returns -1 when search is active, when remembering is disabled, or when
//   totalItems <= 0 (signals "no restore, leave selection as is").
// - Returns 0 when remember is enabled but no remembered index is found.
// - Otherwise returns the clamped remembered index from `lookup`.
[[nodiscard]] auto calculateSelectionIndex(int collectionIndex,
                                           const QList<CollectionConfig> &collections,
                                           int totalItems, bool searchActive,
                                           bool rememberSelectionEnabled,
                                           const SessionLookup &lookup) -> int;

// Finds the visual index of `previousIndex` within the subcollections of
// `targetCollectionIndex`.
//
// Pure version of NavigationManager::findSubcollectionVisualIndex. Returns -1
// when targetCollectionIndex is out of range or when previousIndex is not a
// direct child of the target collection (e.g. arriving from a non-subcollection
// origin).
[[nodiscard]] auto findSubcollectionVisualIndex(int targetCollectionIndex, int previousIndex,
                                                const QList<CollectionConfig> &collections) -> int;

// Chooses a sensible collection to land on when the navigation stack is empty
// but the user requested "go back".
//
// Pure version of the fallback branch in NavigationManager::handleNavigationFallback.
//
// Behavior:
// - If `currentIndex` is in range, that index is kept (caller stays put).
// - Otherwise prefers the first top-level collection (parentCollectionIndex == -1).
// - Otherwise falls back to index 0 if the list is non-empty.
// - Returns -1 only when `collections` is empty.
[[nodiscard]] auto chooseFallbackCollectionIndex(int currentIndex,
                                                 const QList<CollectionConfig> &collections) -> int;

// Decoded breadcrumb link. The original is one of:
//   "collection:<int>"  — navigate to ancestor collection
//   "subfolder:<path>"  — navigate to a specific virtual subfolder level
//   "root:"             — return to the root of the current collection
// Anything else parses as Kind::Invalid.
struct BreadcrumbLink {
  enum class Kind { Invalid, Collection, Subfolder, Root };
  Kind kind = Kind::Invalid;
  int collectionIndex = -1; // valid only for Kind::Collection
  QString subfolderPath;    // valid only for Kind::Subfolder
};

// Parses a breadcrumb link emitted by the title/subfolder QLabel.
//
// Pure: does not bounds-check `collectionIndex` against any list — that is the
// caller's responsibility. Returns Kind::Invalid on malformed prefixes,
// non-numeric "collection:" payloads, or unknown link schemes.
[[nodiscard]] auto parseBreadcrumbLink(const QString &link) -> BreadcrumbLink;

// Computes the parent path when going up one level from a virtual subfolder.
//
// Empty input -> empty output. Single-segment input ("Action") -> empty.
// Multi-segment input ("Action/2024/Q1") -> "Action/2024".
//
// Trailing slashes are treated as empty trailing segments and trimmed before
// the parent split, so "Action/" behaves like "Action".
[[nodiscard]] auto parentSubfolderPath(const QString &currentSubfolder) -> QString;

} // namespace NavigationHelpers

#endif // NAVIGATIONHELPERS_H
