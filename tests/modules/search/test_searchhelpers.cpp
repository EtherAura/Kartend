// Tests for SearchHelpers — pure helpers extracted from SearchManager.
//
// These tests exercise the search-mode cycle decisions and the AllCollections
// "allow" rule without spinning up a SearchManager (which would require the
// full UI graph + filesystem-backed collection scans).

#include <QTest>
#include <QVector>

#include "collection/collectionconfig.h"
#include "searchhelpers.h"
#include "searchutils.h"

namespace {

auto makeCollection(const QString &name, int parent = -1) -> CollectionConfig {
  CollectionConfig c;
  c.name = name;
  c.parentCollectionIndex = parent;
  return c;
}

auto makeCtx(bool hasSubs, bool realDirectItems, bool allowAll) -> SearchContext {
  SearchContext ctx;
  ctx.hasSubs = hasSubs;
  ctx.realDirectItems = realDirectItems;
  ctx.allowAll = allowAll;
  return ctx;
}

} // namespace

class TestSearchHelpers : public QObject {
  Q_OBJECT
private slots:
  // buildSearchModeCycle — root collection
  void rootNoSubsNoAllow();
  void rootHasSubsNoAllow();
  void rootNoSubsWithAllow();
  void rootHasSubsWithAllow();

  // buildSearchModeCycle — non-root, has subs
  void nonRootHasSubsNoDirectNoAllow();
  void nonRootHasSubsWithDirectNoAllow();
  void nonRootHasSubsWithDirectAndAllow();

  // buildSearchModeCycle — non-root, no subs (leaf)
  void nonRootLeafNoAllow();
  void nonRootLeafWithAllow();

  // allowAllFor
  void allowAllForRootWithoutOtherRoots();
  void allowAllForRootWithSiblingRootHavingItems();
  void allowAllForRootIgnoresSiblingRootWithoutItems();
  void allowAllForRootIgnoresChildCollections();
  void allowAllForNonRootWithSubsTrue();
  void allowAllForNonRootLeafTrue();
  void allowAllForRootWithoutLookupReturnsFalse();

  // computeAdaptiveDebounceMs
  void adaptiveDebounceFirstKeystrokeUsesDefault();
  void adaptiveDebounceVeryFastTypingClampsToMin();
  void adaptiveDebounceVerySlowTypingClampsToMax();
  void adaptiveDebounceMidIntervalLandsBetween();
  void adaptiveDebounceNegativeGapTreatedAsFirstKeystroke();

  // shouldSavePreSearchState
  void preSearchSnapshotOnlyForCurrentCollection();

  // classifySearchClearAction
  void clearFromRootViewRestoresRootView();
  void clearWithoutHostOrRootViewDoesNothing();
  void clearWithSnapshotCurrentCollectionRebuilds();
  void clearWithSnapshotInMemoryModeClearsFilter();
  void clearWithoutSnapshotReloads();

  // classifySearchDispatch
  void dispatchRootViewRoutesToAllCollections();
  void dispatchNoHostOutsideRootViewIsNone();
  void dispatchOutOfRangeHostIsNone();
  void dispatchFollowsModeForValidHost();

  // shouldRefocusSearchBar
  void refocusReclaimsFromTransientGrabbers();
  void refocusLeavesDeliberateTextTargetAlone();
  void refocusSkipsHiddenOrAlreadyFocusedBar();
};

// --------------------------- buildSearchModeCycle (root) --------------------

void TestSearchHelpers::rootNoSubsNoAllow() {
  const auto cycle =
      SearchHelpers::buildSearchModeCycle(makeCtx(false, false, false), /*isRoot=*/true);
  QCOMPARE(cycle.size(), 1);
  QCOMPARE(cycle[0], SearchMode::CurrentCollection);
}

void TestSearchHelpers::rootHasSubsNoAllow() {
  const auto cycle =
      SearchHelpers::buildSearchModeCycle(makeCtx(true, false, false), /*isRoot=*/true);
  QCOMPARE(cycle.size(), 1);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
}

void TestSearchHelpers::rootNoSubsWithAllow() {
  const auto cycle =
      SearchHelpers::buildSearchModeCycle(makeCtx(false, false, true), /*isRoot=*/true);
  QCOMPARE(cycle.size(), 2);
  QCOMPARE(cycle[0], SearchMode::CurrentCollection);
  QCOMPARE(cycle[1], SearchMode::AllCollections);
}

void TestSearchHelpers::rootHasSubsWithAllow() {
  const auto cycle =
      SearchHelpers::buildSearchModeCycle(makeCtx(true, false, true), /*isRoot=*/true);
  QCOMPARE(cycle.size(), 2);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
  QCOMPARE(cycle[1], SearchMode::AllCollections);
}

// --------------------------- buildSearchModeCycle (non-root, has subs) ------

void TestSearchHelpers::nonRootHasSubsNoDirectNoAllow() {
  const auto cycle =
      SearchHelpers::buildSearchModeCycle(makeCtx(true, false, false), /*isRoot=*/false);
  QCOMPARE(cycle.size(), 1);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
}

void TestSearchHelpers::nonRootHasSubsWithDirectNoAllow() {
  const auto cycle =
      SearchHelpers::buildSearchModeCycle(makeCtx(true, true, false), /*isRoot=*/false);
  QCOMPARE(cycle.size(), 2);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
  QCOMPARE(cycle[1], SearchMode::CurrentCollection);
}

void TestSearchHelpers::nonRootHasSubsWithDirectAndAllow() {
  const auto cycle =
      SearchHelpers::buildSearchModeCycle(makeCtx(true, true, true), /*isRoot=*/false);
  QCOMPARE(cycle.size(), 3);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
  QCOMPARE(cycle[1], SearchMode::CurrentCollection);
  QCOMPARE(cycle[2], SearchMode::AllCollections);
}

// --------------------------- buildSearchModeCycle (non-root, leaf) ----------

void TestSearchHelpers::nonRootLeafNoAllow() {
  const auto cycle =
      SearchHelpers::buildSearchModeCycle(makeCtx(false, false, false), /*isRoot=*/false);
  QCOMPARE(cycle.size(), 1);
  QCOMPARE(cycle[0], SearchMode::CurrentCollection);
}

void TestSearchHelpers::nonRootLeafWithAllow() {
  const auto cycle =
      SearchHelpers::buildSearchModeCycle(makeCtx(false, false, true), /*isRoot=*/false);
  QCOMPARE(cycle.size(), 2);
  QCOMPARE(cycle[0], SearchMode::CurrentCollection);
  QCOMPARE(cycle[1], SearchMode::AllCollections);
}

// --------------------------- allowAllFor ------------------------------------

void TestSearchHelpers::allowAllForRootWithoutOtherRoots() {
  // Single root collection -> no sibling roots with items -> false.
  QList<CollectionConfig> cs = {makeCollection("Solo")};
  auto lookup = [](int) { return true; };
  QVERIFY(!SearchHelpers::allowAllFor(cs[0], 0, /*hasSubs=*/false, cs, lookup));
}

void TestSearchHelpers::allowAllForRootWithSiblingRootHavingItems() {
  // Two root collections, sibling root has items -> true.
  QList<CollectionConfig> cs = {makeCollection("Movies"), makeCollection("Music")};
  auto lookup = [](int i) { return i == 1; }; // Music has items
  QVERIFY(SearchHelpers::allowAllFor(cs[0], 0, /*hasSubs=*/false, cs, lookup));
}

void TestSearchHelpers::allowAllForRootIgnoresSiblingRootWithoutItems() {
  // Two root collections, sibling root is empty -> false.
  QList<CollectionConfig> cs = {makeCollection("Movies"), makeCollection("Empty")};
  auto lookup = [](int) { return false; };
  QVERIFY(!SearchHelpers::allowAllFor(cs[0], 0, /*hasSubs=*/false, cs, lookup));
}

void TestSearchHelpers::allowAllForRootIgnoresChildCollections() {
  // Root + child of the root. The child has items but isn't a root, so
  // it must NOT count as a sibling root. -> false.
  QList<CollectionConfig> cs = {makeCollection("Movies"), makeCollection("Movies > Action", 0)};
  auto lookup = [](int) { return true; };
  QVERIFY(!SearchHelpers::allowAllFor(cs[0], 0, /*hasSubs=*/true, cs, lookup));
}

void TestSearchHelpers::allowAllForNonRootWithSubsTrue() {
  // Non-root collection with subs -> always true.
  QList<CollectionConfig> cs = {makeCollection("Top"), makeCollection("Top > Mid", 0)};
  auto lookup = [](int) { return false; };
  QVERIFY(SearchHelpers::allowAllFor(cs[1], 1, /*hasSubs=*/true, cs, lookup));
}

void TestSearchHelpers::allowAllForNonRootLeafTrue() {
  // Non-root leaf -> always true.
  QList<CollectionConfig> cs = {makeCollection("Top"), makeCollection("Top > Leaf", 0)};
  auto lookup = [](int) { return false; };
  QVERIFY(SearchHelpers::allowAllFor(cs[1], 1, /*hasSubs=*/false, cs, lookup));
}

void TestSearchHelpers::allowAllForRootWithoutLookupReturnsFalse() {
  // Defensive: a null/empty std::function in the root branch should not crash.
  QList<CollectionConfig> cs = {makeCollection("Movies"), makeCollection("Music")};
  SearchHelpers::HasDirectItemsLookup empty;
  QVERIFY(!SearchHelpers::allowAllFor(cs[0], 0, /*hasSubs=*/false, cs, empty));
}

// --------------------------- computeAdaptiveDebounceMs ----------------------

void TestSearchHelpers::adaptiveDebounceFirstKeystrokeUsesDefault() {
  // gap=0 means "no prior keystroke" -> default debounce, regardless of
  // min/max.
  QCOMPARE(SearchHelpers::computeAdaptiveDebounceMs(/*gap=*/0, /*min=*/80,
                                                    /*max=*/250, /*default=*/150),
           150);
}

void TestSearchHelpers::adaptiveDebounceVeryFastTypingClampsToMin() {
  // gap=10ms is below the 50ms clamp floor -> treated as 50ms gap, which maps
  // to min debounce.
  QCOMPARE(SearchHelpers::computeAdaptiveDebounceMs(/*gap=*/10, /*min=*/80,
                                                    /*max=*/250, /*default=*/150),
           80);
  QCOMPARE(SearchHelpers::computeAdaptiveDebounceMs(/*gap=*/50, /*min=*/80,
                                                    /*max=*/250, /*default=*/150),
           80);
}

void TestSearchHelpers::adaptiveDebounceVerySlowTypingClampsToMax() {
  // gap=2000ms exceeds the 500ms clamp ceiling -> treated as 500ms gap, max
  // debounce.
  QCOMPARE(SearchHelpers::computeAdaptiveDebounceMs(/*gap=*/2000, /*min=*/80,
                                                    /*max=*/250, /*default=*/150),
           250);
  QCOMPARE(SearchHelpers::computeAdaptiveDebounceMs(/*gap=*/500, /*min=*/80,
                                                    /*max=*/250, /*default=*/150),
           250);
}

void TestSearchHelpers::adaptiveDebounceMidIntervalLandsBetween() {
  // gap=275 is the midpoint between 50 and 500. The mapping is linear, so the
  // result should land at the midpoint of [80, 250] = 165.
  QCOMPARE(SearchHelpers::computeAdaptiveDebounceMs(/*gap=*/275, /*min=*/80,
                                                    /*max=*/250, /*default=*/150),
           165);
}

void TestSearchHelpers::adaptiveDebounceNegativeGapTreatedAsFirstKeystroke() {
  // Defensive: clock skew or test injection of negative gap shouldn't break
  // the math. Falls through to "first keystroke" branch.
  QCOMPARE(SearchHelpers::computeAdaptiveDebounceMs(/*gap=*/-50, /*min=*/80,
                                                    /*max=*/250, /*default=*/150),
           150);
}

// --------------------------- shouldSavePreSearchState -----------------------

void TestSearchHelpers::preSearchSnapshotOnlyForCurrentCollection() {
  // Only CurrentCollection reloads onto the same collection context on
  // clear; the DB-backed modes must take the full reload path.
  QVERIFY(SearchHelpers::shouldSavePreSearchState(SearchMode::CurrentCollection));
  QVERIFY(!SearchHelpers::shouldSavePreSearchState(SearchMode::CurrentAndSubcollections));
  QVERIFY(!SearchHelpers::shouldSavePreSearchState(SearchMode::AllCollections));
}

// --------------------------- classifySearchClearAction ----------------------

void TestSearchHelpers::clearFromRootViewRestoresRootView() {
  // Search started from the synthetic Home view: no host collection to
  // restore, rebuild the root tile view instead.
  QCOMPARE(SearchHelpers::classifySearchClearAction(/*collIndex=*/-1,
                                                    /*preSearchInRootView=*/true,
                                                    /*hasPreSearchState=*/false,
                                                    SearchMode::AllCollections),
           SearchHelpers::SearchClearAction::RestoreRootView);
}

void TestSearchHelpers::clearWithoutHostOrRootViewDoesNothing() {
  QCOMPARE(SearchHelpers::classifySearchClearAction(-1, false, false, SearchMode::AllCollections),
           SearchHelpers::SearchClearAction::None);
}

void TestSearchHelpers::clearWithSnapshotCurrentCollectionRebuilds() {
  // CurrentCollection searches are DB-backed, so the pre-search view must be
  // rebuilt before the cached widgets/scroll position are restored.
  QCOMPARE(
      SearchHelpers::classifySearchClearAction(0, false, true, SearchMode::CurrentCollection),
      SearchHelpers::SearchClearAction::RebuildAndRestorePreSearch);
}

void TestSearchHelpers::clearWithSnapshotInMemoryModeClearsFilter() {
  // In-memory filter modes restore directly via clearFilter.
  QCOMPARE(SearchHelpers::classifySearchClearAction(0, false, true,
                                                    SearchMode::CurrentAndSubcollections),
           SearchHelpers::SearchClearAction::ClearFilterAndRestore);
  QCOMPARE(SearchHelpers::classifySearchClearAction(0, false, true, SearchMode::AllCollections),
           SearchHelpers::SearchClearAction::ClearFilterAndRestore);
}

void TestSearchHelpers::clearWithoutSnapshotReloads() {
  QCOMPARE(
      SearchHelpers::classifySearchClearAction(2, false, false, SearchMode::CurrentCollection),
      SearchHelpers::SearchClearAction::ReloadCollection);
  // The root-view flag is irrelevant once a host collection exists.
  QCOMPARE(SearchHelpers::classifySearchClearAction(2, true, false, SearchMode::AllCollections),
           SearchHelpers::SearchClearAction::ReloadCollection);
}

// --------------------------- classifySearchDispatch -------------------------

void TestSearchHelpers::dispatchRootViewRoutesToAllCollections() {
  // No host collection but the user is in the root/Home view: the
  // cross-collection pipeline is the only sensible route, regardless of the
  // nominally active mode.
  QCOMPARE(SearchHelpers::classifySearchDispatch(/*collIndex=*/-1, /*size=*/3,
                                                 /*inRootView=*/true,
                                                 SearchMode::CurrentCollection),
           SearchHelpers::SearchDispatch::RootAllCollections);
}

void TestSearchHelpers::dispatchNoHostOutsideRootViewIsNone() {
  QCOMPARE(SearchHelpers::classifySearchDispatch(-1, 3, false, SearchMode::CurrentCollection),
           SearchHelpers::SearchDispatch::None);
}

void TestSearchHelpers::dispatchOutOfRangeHostIsNone() {
  QCOMPARE(SearchHelpers::classifySearchDispatch(3, 3, false, SearchMode::CurrentCollection),
           SearchHelpers::SearchDispatch::None);
  QCOMPARE(SearchHelpers::classifySearchDispatch(0, 0, false, SearchMode::AllCollections),
           SearchHelpers::SearchDispatch::None);
}

void TestSearchHelpers::dispatchFollowsModeForValidHost() {
  QCOMPARE(SearchHelpers::classifySearchDispatch(1, 3, false, SearchMode::CurrentCollection),
           SearchHelpers::SearchDispatch::CurrentCollection);
  QCOMPARE(
      SearchHelpers::classifySearchDispatch(1, 3, false, SearchMode::CurrentAndSubcollections),
      SearchHelpers::SearchDispatch::CurrentAndSubcollections);
  QCOMPARE(SearchHelpers::classifySearchDispatch(1, 3, false, SearchMode::AllCollections),
           SearchHelpers::SearchDispatch::AllCollections);
  // A valid host keeps mode routing even while the root-view flag is set
  // (the flag only matters when no host collection exists).
  QCOMPARE(SearchHelpers::classifySearchDispatch(1, 3, true, SearchMode::CurrentCollection),
           SearchHelpers::SearchDispatch::CurrentCollection);
}

// --------------------------- shouldRefocusSearchBar -------------------------

void TestSearchHelpers::refocusReclaimsFromTransientGrabbers() {
  // Focus wandered to a non-text widget (result tile) during a results
  // update: reclaim it (Kartend-8oau).
  QVERIFY(SearchHelpers::shouldRefocusSearchBar(/*visible=*/true, /*focusIsSearchBar=*/false,
                                                /*focusIsOtherTextInput=*/false));
}

void TestSearchHelpers::refocusLeavesDeliberateTextTargetAlone() {
  // The user clicked into another line edit: stealing focus back would fight
  // deliberate input.
  QVERIFY(!SearchHelpers::shouldRefocusSearchBar(true, false, true));
}

void TestSearchHelpers::refocusSkipsHiddenOrAlreadyFocusedBar() {
  QVERIFY(!SearchHelpers::shouldRefocusSearchBar(/*visible=*/false, false, false));
  QVERIFY(!SearchHelpers::shouldRefocusSearchBar(true, /*focusIsSearchBar=*/true, false));
}

QTEST_APPLESS_MAIN(TestSearchHelpers)
#include "test_searchhelpers.moc"
