#ifndef SEARCHHELPERS_H
#define SEARCHHELPERS_H

#include "collectionutils.h"
#include "searchutils.h"
#include <functional>
#include <QList>
#include <QVector>

// Pure helpers extracted from SearchManager so the search-mode cycling /
// "allow all" decisions can be unit-tested without instantiating SearchManager
// (which depends on the full UI graph + filesystem-backed collection scans).
//
// See bd for the broader extraction effort.
namespace SearchHelpers {

// Builds the ordered cycle of search modes available for the current
// collection. Pressing the search-mode toggle walks this cycle in order.
//
// The cycle is shaped by:
// - isRoot: whether the active collection is a top-level (parentCollectionIndex == -1)
// - ctx.hasSubs: whether the active collection has any subcollections
// - ctx.realDirectItems: whether the active collection has its own direct items
// - ctx.allowAll: whether the AllCollections mode should appear in the cycle
//
// Cycle shapes (in order):
// - root + hasSubs:                    [Current+Sub]                 (+All if allowAll)
// - root + !hasSubs:                   [Current]                     (+All if allowAll)
// - non-root + hasSubs + direct:       [Current+Sub, Current]        (+All if allowAll)
// - non-root + hasSubs + no-direct:    [Current+Sub]                 (+All if allowAll)
// - non-root + !hasSubs:               [Current]                     (+All if allowAll)
[[nodiscard]] auto buildSearchModeCycle(const SearchContext &ctx, bool isRoot)
    -> QVector<SearchMode>;

// Pure version of SearchManager::allowAllFor.
//
// Decides whether the AllCollections mode should be available given the
// current collection's role. Because the production code consults
// hasDirectItemsForIndex (which does filesystem I/O) for the root case,
// callers pass a callable that maps a collection index to its
// "has-direct-items" answer; tests inject a precomputed lookup, production
// passes a lambda calling SearchManager::hasDirectItemsForIndex.
//
// Returns true when:
// - the collection is a leaf (!hasSubs), OR
// - the collection has subs (hasSubs), OR
// - the collection is root AND there exists at least one OTHER root
//   collection with direct items (allows discovery of sibling roots).
using HasDirectItemsLookup = std::function<bool(int)>;
[[nodiscard]] auto allowAllFor(const CollectionConfig &cfg, int collIndex, bool hasSubs,
                               const QList<CollectionConfig> &collections,
                               const HasDirectItemsLookup &hasDirectItemsLookup) -> bool;

} // namespace SearchHelpers

#endif // SEARCHHELPERS_H
