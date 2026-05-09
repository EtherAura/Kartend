// Tests for SearchHelpers — pure helpers extracted from SearchManager.
//
// These tests exercise the search-mode cycle decisions and the AllCollections
// "allow" rule without spinning up a SearchManager (which would require the
// full UI graph + filesystem-backed collection scans).

#include <QTest>
#include <QVector>

#include "collectionutils.h"
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
};

// --------------------------- buildSearchModeCycle (root) --------------------

void TestSearchHelpers::rootNoSubsNoAllow() {
  const auto cycle = SearchHelpers::buildSearchModeCycle(
      makeCtx(false, false, false), /*isRoot=*/true);
  QCOMPARE(cycle.size(), 1);
  QCOMPARE(cycle[0], SearchMode::CurrentCollection);
}

void TestSearchHelpers::rootHasSubsNoAllow() {
  const auto cycle = SearchHelpers::buildSearchModeCycle(
      makeCtx(true, false, false), /*isRoot=*/true);
  QCOMPARE(cycle.size(), 1);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
}

void TestSearchHelpers::rootNoSubsWithAllow() {
  const auto cycle = SearchHelpers::buildSearchModeCycle(
      makeCtx(false, false, true), /*isRoot=*/true);
  QCOMPARE(cycle.size(), 2);
  QCOMPARE(cycle[0], SearchMode::CurrentCollection);
  QCOMPARE(cycle[1], SearchMode::AllCollections);
}

void TestSearchHelpers::rootHasSubsWithAllow() {
  const auto cycle = SearchHelpers::buildSearchModeCycle(
      makeCtx(true, false, true), /*isRoot=*/true);
  QCOMPARE(cycle.size(), 2);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
  QCOMPARE(cycle[1], SearchMode::AllCollections);
}

// --------------------------- buildSearchModeCycle (non-root, has subs) ------

void TestSearchHelpers::nonRootHasSubsNoDirectNoAllow() {
  const auto cycle = SearchHelpers::buildSearchModeCycle(
      makeCtx(true, false, false), /*isRoot=*/false);
  QCOMPARE(cycle.size(), 1);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
}

void TestSearchHelpers::nonRootHasSubsWithDirectNoAllow() {
  const auto cycle = SearchHelpers::buildSearchModeCycle(
      makeCtx(true, true, false), /*isRoot=*/false);
  QCOMPARE(cycle.size(), 2);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
  QCOMPARE(cycle[1], SearchMode::CurrentCollection);
}

void TestSearchHelpers::nonRootHasSubsWithDirectAndAllow() {
  const auto cycle = SearchHelpers::buildSearchModeCycle(
      makeCtx(true, true, true), /*isRoot=*/false);
  QCOMPARE(cycle.size(), 3);
  QCOMPARE(cycle[0], SearchMode::CurrentAndSubcollections);
  QCOMPARE(cycle[1], SearchMode::CurrentCollection);
  QCOMPARE(cycle[2], SearchMode::AllCollections);
}

// --------------------------- buildSearchModeCycle (non-root, leaf) ----------

void TestSearchHelpers::nonRootLeafNoAllow() {
  const auto cycle = SearchHelpers::buildSearchModeCycle(
      makeCtx(false, false, false), /*isRoot=*/false);
  QCOMPARE(cycle.size(), 1);
  QCOMPARE(cycle[0], SearchMode::CurrentCollection);
}

void TestSearchHelpers::nonRootLeafWithAllow() {
  const auto cycle = SearchHelpers::buildSearchModeCycle(
      makeCtx(false, false, true), /*isRoot=*/false);
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
  QList<CollectionConfig> cs = {makeCollection("Movies"),
                                makeCollection("Music")};
  auto lookup = [](int i) { return i == 1; }; // Music has items
  QVERIFY(SearchHelpers::allowAllFor(cs[0], 0, /*hasSubs=*/false, cs, lookup));
}

void TestSearchHelpers::allowAllForRootIgnoresSiblingRootWithoutItems() {
  // Two root collections, sibling root is empty -> false.
  QList<CollectionConfig> cs = {makeCollection("Movies"),
                                makeCollection("Empty")};
  auto lookup = [](int) { return false; };
  QVERIFY(!SearchHelpers::allowAllFor(cs[0], 0, /*hasSubs=*/false, cs, lookup));
}

void TestSearchHelpers::allowAllForRootIgnoresChildCollections() {
  // Root + child of the root. The child has items but isn't a root, so
  // it must NOT count as a sibling root. -> false.
  QList<CollectionConfig> cs = {makeCollection("Movies"),
                                makeCollection("Movies > Action", 0)};
  auto lookup = [](int) { return true; };
  QVERIFY(!SearchHelpers::allowAllFor(cs[0], 0, /*hasSubs=*/true, cs, lookup));
}

void TestSearchHelpers::allowAllForNonRootWithSubsTrue() {
  // Non-root collection with subs -> always true.
  QList<CollectionConfig> cs = {makeCollection("Top"),
                                makeCollection("Top > Mid", 0)};
  auto lookup = [](int) { return false; };
  QVERIFY(SearchHelpers::allowAllFor(cs[1], 1, /*hasSubs=*/true, cs, lookup));
}

void TestSearchHelpers::allowAllForNonRootLeafTrue() {
  // Non-root leaf -> always true.
  QList<CollectionConfig> cs = {makeCollection("Top"),
                                makeCollection("Top > Leaf", 0)};
  auto lookup = [](int) { return false; };
  QVERIFY(SearchHelpers::allowAllFor(cs[1], 1, /*hasSubs=*/false, cs, lookup));
}

void TestSearchHelpers::allowAllForRootWithoutLookupReturnsFalse() {
  // Defensive: a null/empty std::function in the root branch should not crash.
  QList<CollectionConfig> cs = {makeCollection("Movies"),
                                makeCollection("Music")};
  SearchHelpers::HasDirectItemsLookup empty;
  QVERIFY(!SearchHelpers::allowAllFor(cs[0], 0, /*hasSubs=*/false, cs, empty));
}

QTEST_APPLESS_MAIN(TestSearchHelpers)
#include "test_searchhelpers.moc"
