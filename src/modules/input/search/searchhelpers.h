#ifndef SEARCHHELPERS_H
#define SEARCHHELPERS_H

#include "collection/collectionconfig.h"
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

// Computes the adaptive debounce delay (ms) for the search input given the
// gap (ms) since the previous keystroke and the configured min/max/default
// debounces. Reflects the SearchManager logic:
//
// - timeSinceLastKeystrokeMs <= 0  -> default debounce (first keystroke)
// - otherwise: clamp gap to [50, 500], map linearly into [min, max].
//   Fast typing (50ms) -> min. Slow typing (>=500ms) -> max.
//
// The formula matches SearchManager::updateAdaptiveDebounce so changes to
// either side stay in lockstep and tests catch a drift.
[[nodiscard]] auto computeAdaptiveDebounceMs(qint64 timeSinceLastKeystrokeMs, int minDebounceMs,
                                             int maxDebounceMs, int defaultDebounceMs) -> int;

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

// Whether entering search should snapshot the pre-search scroll/widget state
// for instant restore on clear.
//
// Only CurrentCollection uses the pre-search restore path: it reloads via
// setupVirtualScrolling on the same collection context. CurrentAndSub-
// collections and AllCollections are DB-backed and can change the visible
// data backing (and the subcollection/virtual-folder tile composition), so
// they take the full reload path on clear instead.
[[nodiscard]] auto shouldSavePreSearchState(SearchMode mode) -> bool;

// What SearchManager::onSearchTextChanged should do when the search text
// transitions to empty. Pure decision over the saved pre-search state:
//
// - RestoreRootView: search started from the synthetic Home view (no host
//   collection) — rebuild the root tile view.
// - RebuildAndRestorePreSearch: a CurrentCollection search saved scroll state
//   — rebuild the pre-search view, then restore the cached widgets/scroll.
// - ClearFilterAndRestore: an in-memory-filter mode saved state — clearFilter
//   restores directly.
// - ReloadCollection: no snapshot to restore — full reload via
//   filterItems(QString()).
// - None: no host collection and search didn't start from the root view —
//   nothing view-shaped to restore.
enum class SearchClearAction {
  None,
  RestoreRootView,
  RebuildAndRestorePreSearch,
  ClearFilterAndRestore,
  ReloadCollection,
};

[[nodiscard]] auto classifySearchClearAction(int collIndex, bool preSearchInRootView,
                                             bool hasPreSearchState, SearchMode preSearchMode)
    -> SearchClearAction;

// Which query pipeline a debounced non-empty search dispatches to.
//
// - RootAllCollections: no host collection but the user is in the root/Home
//   view — the cross-collection pipeline is the only one that makes sense.
// - None: no valid host collection and not in the root view — nothing to
//   query against.
// - Otherwise the pipeline follows the active SearchMode 1:1 (all three are
//   DB-backed; see the dispatch comments in performDebouncedSearch).
enum class SearchDispatch {
  None,
  RootAllCollections,
  CurrentCollection,
  CurrentAndSubcollections,
  AllCollections,
};

[[nodiscard]] auto classifySearchDispatch(int collIndex, int collectionsSize, bool inRootView,
                                          SearchMode mode) -> SearchDispatch;

// The refocus-unless-deliberate heuristic (Kartend-8oau): after a results
// update, focus is reclaimed for the search bar only when
// - the search bar is visible,
// - it doesn't already have focus, and
// - focus is NOT in another text input (a QLineEdit other than the search
//   bar means the user deliberately moved focus; transient grabbers like
//   result tiles are not text inputs and are reclaimed from).
[[nodiscard]] auto shouldRefocusSearchBar(bool searchBarVisible, bool focusIsSearchBar,
                                          bool focusIsOtherTextInput) -> bool;

} // namespace SearchHelpers

#endif // SEARCHHELPERS_H
