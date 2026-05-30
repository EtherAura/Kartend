/**
 * @file test_collectionutils_ancestry.cpp
 * @brief Unit tests for CollectionUtils tree-walk helpers and the
 *        CollectionHierarchyCache.
 *
 * Extracted from test_collectionutils.cpp as a cohesion split. Groups the
 * ancestor / descendant / hierarchy-cache / remap-on-removal slots that all
 * share the same tree-of-collections fixture pattern.
 */

#include "collection/collectionconfig.h"
#include "collection/collectionhierarchycache.h"
#include "collection/hierarchyhelpers.h"
#include <QTest>

class TestCollectionUtilsAncestry : public QObject {
  Q_OBJECT

private slots:
  // ancestorIndexChain
  void ancestorIndexChain_topLevelReturnsEmpty();
  void ancestorIndexChain_oneDeep();
  void ancestorIndexChain_multiDeepRootFirst();
  void ancestorIndexChain_stopsAtNonSubcollection();
  void ancestorIndexChain_invalidParentReturnsEmpty();
  void ancestorIndexChain_cycleIsBounded();

  // alias-parent links via CollectionHierarchyCache
  void hierarchyCache_directChildren_includesLinkedParent();
  void hierarchyCache_linkedDirectChildren_returnsLinkedOnly();
  void hierarchyCache_directChildren_primaryFirstThenLinked();
  void hierarchyCache_directChildren_skipsSelfLink();
  void hierarchyCache_directChildren_skipsUnknownLinkName();
  void hierarchyCache_directChildren_skipsLinkEqualToPrimary();
  void hierarchyCache_allDescendants_handlesMutualLinkCycle();
  void hierarchyCache_allDescendants_excludesSelf();

  // collectDescendantIndices + applyCollectionRemoval (collection delete path)
  void collectDescendantIndices_returnsNestedSubtree();
  void collectDescendantIndices_cycleIsBounded();
  void applyCollectionRemoval_remapsParentsAfterRemoval();
  void applyCollectionRemoval_orphansSurvivorWhoseParentWasRemoved();
  void applyCollectionRemoval_healsStaleParentIndex();
  void applyCollectionRemoval_shiftsSurvivingParentIndex();
};

namespace {
// Minimal CollectionConfig builder used across every fixture in this file.
CollectionConfig makeCollection(const QString &name, bool isSub, int parentIdx) {
  CollectionConfig c;
  c.name = name;
  c.isSubcollection = isSub;
  c.parentCollectionIndex = parentIdx;
  return c;
}

// Pull the names / parent indices out of a remap result so it can be
// asserted against plain QList literals (the whole-list QCOMPARE idiom).
QStringList namesOf(const QList<CollectionConfig> &cs) {
  QStringList names;
  for (const CollectionConfig &c : cs) {
    names << c.name;
  }
  return names;
}
QList<int> parentsOf(const QList<CollectionConfig> &cs) {
  QList<int> parents;
  for (const CollectionConfig &c : cs) {
    parents << c.parentCollectionIndex;
  }
  return parents;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ancestorIndexChain (full-path breadcrumb)
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAncestry::ancestorIndexChain_topLevelReturnsEmpty() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[0], cs), QList<int>{});
}

void TestCollectionUtilsAncestry::ancestorIndexChain_oneDeep() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Video", /*isSub=*/false, -1);
  cs << makeCollection("Movies", /*isSub=*/true, 0);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[1], cs), (QList<int>{0}));
}

void TestCollectionUtilsAncestry::ancestorIndexChain_multiDeepRootFirst() {
  // Root → Video → Movies → SciFi. Expect ancestors for SciFi == [0,1,2].
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  cs << makeCollection("Video", /*isSub=*/true, 0);
  cs << makeCollection("Movies", /*isSub=*/true, 1);
  cs << makeCollection("SciFi", /*isSub=*/true, 2);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[3], cs), (QList<int>{0, 1, 2}));
}

void TestCollectionUtilsAncestry::ancestorIndexChain_stopsAtNonSubcollection() {
  // A non-subcollection in the middle terminates the walk (inclusive): it's
  // the root-most ancestor we render in the breadcrumb.
  QList<CollectionConfig> cs;
  cs << makeCollection("Detached", /*isSub=*/false, -1);
  cs << makeCollection("TopLikeRoot", /*isSub=*/false, 0); // not a subcoll
  cs << makeCollection("Child", /*isSub=*/true, 1);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[2], cs), (QList<int>{1}));
}

void TestCollectionUtilsAncestry::ancestorIndexChain_invalidParentReturnsEmpty() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Orphan", /*isSub=*/true, -1);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[0], cs), QList<int>{});
}

void TestCollectionUtilsAncestry::ancestorIndexChain_cycleIsBounded() {
  // Degenerate cycle: A → B → A. The walk must terminate by bounded-depth
  // guard rather than looping forever.
  QList<CollectionConfig> cs;
  cs << makeCollection("A", /*isSub=*/true, 1);
  cs << makeCollection("B", /*isSub=*/true, 0);
  const QList<int> chain = CollectionUtils::ancestorIndexChain(cs[0], cs);
  QVERIFY(chain.size() <= cs.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// CollectionHierarchyCache + alias parents
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAncestry::hierarchyCache_directChildren_includesLinkedParent() {
  // Root, primary child Video with subcollection Movies, plus a sibling
  // category Reference that lists Movies as an additional parent. The merged
  // child list under Reference should expose Movies even though Reference is
  // not Movies's primary parent.
  QList<CollectionConfig> cs;
  cs << makeCollection("Video", /*isSub=*/false, -1);     // 0
  cs << makeCollection("Reference", /*isSub=*/false, -1); // 1
  CollectionConfig movies = makeCollection("Movies", /*isSub=*/true, 0);
  movies.additionalParentNames << QStringLiteral("Reference");
  cs << movies; // 2

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  QCOMPARE(cache.directChildren(0), QList<int>{2});
  QCOMPARE(cache.directChildren(1), QList<int>{2});
}

void TestCollectionUtilsAncestry::hierarchyCache_linkedDirectChildren_returnsLinkedOnly() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Video", /*isSub=*/false, -1);     // 0
  cs << makeCollection("Reference", /*isSub=*/false, -1); // 1
  CollectionConfig movies = makeCollection("Movies", /*isSub=*/true, 0);
  movies.additionalParentNames << QStringLiteral("Reference");
  cs << movies; // 2

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  // Video is the primary parent — no linked entry there.
  QVERIFY(cache.linkedDirectChildren(0).isEmpty());
  // Reference holds the alias.
  QCOMPARE(cache.linkedDirectChildren(1), QList<int>{2});
}

void TestCollectionUtilsAncestry::hierarchyCache_directChildren_primaryFirstThenLinked() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Reference", /*isSub=*/false, -1); // 0
  cs << makeCollection("Articles", /*isSub=*/true, 0);    // 1 — primary child of Reference
  cs << makeCollection("Video", /*isSub=*/false, -1);     // 2
  CollectionConfig movies = makeCollection("Movies", /*isSub=*/true, 2);
  movies.additionalParentNames << QStringLiteral("Reference");
  cs << movies; // 3 — linked under Reference

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  const QList<int> children = cache.directChildren(0);
  QCOMPARE(children.size(), 2);
  QCOMPARE(children[0], 1); // primary first
  QCOMPARE(children[1], 3); // linked second
}

void TestCollectionUtilsAncestry::hierarchyCache_directChildren_skipsSelfLink() {
  QList<CollectionConfig> cs;
  CollectionConfig solo = makeCollection("Solo", /*isSub=*/false, -1);
  solo.additionalParentNames << QStringLiteral("Solo"); // self-reference
  cs << solo;

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  QVERIFY(cache.directChildren(0).isEmpty());
  QVERIFY(cache.linkedDirectChildren(0).isEmpty());
}

void TestCollectionUtilsAncestry::hierarchyCache_directChildren_skipsUnknownLinkName() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Video", /*isSub=*/false, -1);
  CollectionConfig movies = makeCollection("Movies", /*isSub=*/true, 0);
  movies.additionalParentNames << QStringLiteral("DoesNotExist");
  cs << movies;

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  // Only the primary parent should claim Movies.
  QCOMPARE(cache.directChildren(0), QList<int>{1});
}

void TestCollectionUtilsAncestry::hierarchyCache_directChildren_skipsLinkEqualToPrimary() {
  // If the user lists their own primary parent in additionalParentNames,
  // the cache should NOT push the child a second time.
  QList<CollectionConfig> cs;
  cs << makeCollection("Video", /*isSub=*/false, -1);
  CollectionConfig movies = makeCollection("Movies", /*isSub=*/true, 0);
  movies.additionalParentNames << QStringLiteral("Video"); // same as primary
  cs << movies;

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  QCOMPARE(cache.directChildren(0), QList<int>{1});
  QVERIFY(cache.linkedDirectChildren(0).isEmpty());
}

void TestCollectionUtilsAncestry::hierarchyCache_allDescendants_handlesMutualLinkCycle() {
  // A links to B, B links to A. Without dedup the descendant walk would
  // never terminate.
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.additionalParentNames << QStringLiteral("B");
  CollectionConfig b = makeCollection("B", /*isSub=*/false, -1);
  b.additionalParentNames << QStringLiteral("A");
  cs << a << b;

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  const QList<int> aDescendants = cache.allDescendants(0);
  // A is reachable through B, but the starting node is excluded — so
  // descendants(A) is exactly {B} and descendants(B) is exactly {A}.
  QCOMPARE(aDescendants.size(), 1);
  QCOMPARE(aDescendants[0], 1);

  const QList<int> bDescendants = cache.allDescendants(1);
  QCOMPARE(bDescendants.size(), 1);
  QCOMPARE(bDescendants[0], 0);
}

void TestCollectionUtilsAncestry::hierarchyCache_allDescendants_excludesSelf() {
  // A self-link must not make a collection its own descendant.
  QList<CollectionConfig> cs;
  CollectionConfig solo = makeCollection("Solo", /*isSub=*/false, -1);
  solo.additionalParentNames << QStringLiteral("Solo");
  cs << solo;

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  QVERIFY(cache.allDescendants(0).isEmpty());
}

// ─────────────────────────────────────────────────────────────────────────────
// collectDescendantIndices + applyCollectionRemoval (collection delete path)
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAncestry::collectDescendantIndices_returnsNestedSubtree() {
  // Root(0) → Video(1) → Movies(2); Root(0) → Audio(3). Descendants of the
  // root are every nested collection; descendants of a leaf are empty.
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  cs << makeCollection("Video", /*isSub=*/true, 0);
  cs << makeCollection("Movies", /*isSub=*/true, 1);
  cs << makeCollection("Audio", /*isSub=*/true, 0);
  QCOMPARE(CollectionUtils::collectDescendantIndices(0, cs), (QList<int>{1, 2, 3}));
  QCOMPARE(CollectionUtils::collectDescendantIndices(1, cs), (QList<int>{2}));
  QCOMPARE(CollectionUtils::collectDescendantIndices(2, cs), QList<int>{});
}

void TestCollectionUtilsAncestry::collectDescendantIndices_cycleIsBounded() {
  // Corrupt input: A(0) and B(1) each name the other as parent. The walk
  // must terminate via the visited-set guard instead of recursing until the
  // stack overflows (the test would crash, not merely fail, otherwise).
  QList<CollectionConfig> cs;
  cs << makeCollection("A", /*isSub=*/true, 1);
  cs << makeCollection("B", /*isSub=*/true, 0);
  const QList<int> descendants = CollectionUtils::collectDescendantIndices(0, cs);
  QVERIFY(descendants.size() <= cs.size());
}

void TestCollectionUtilsAncestry::applyCollectionRemoval_remapsParentsAfterRemoval() {
  // A(0) → B(1); C(2) → D(3). Removing A and B (mapped to -1) drops that
  // subtree; C and D survive and D's parent link follows C to its new index.
  QList<CollectionConfig> cs;
  cs << makeCollection("A", /*isSub=*/false, -1);
  cs << makeCollection("B", /*isSub=*/true, 0);
  cs << makeCollection("C", /*isSub=*/false, -1);
  cs << makeCollection("D", /*isSub=*/true, 2);
  const QList<CollectionConfig> result =
      CollectionUtils::applyCollectionRemoval(cs, QList<int>{-1, -1, 0, 1});
  QCOMPARE(namesOf(result), (QStringList{"C", "D"}));
  QCOMPARE(parentsOf(result), (QList<int>{-1, 0}));
  QVERIFY(result[1].isSubcollection);
}

void TestCollectionUtilsAncestry::applyCollectionRemoval_orphansSurvivorWhoseParentWasRemoved() {
  // A(0) → B(1) → C(2). Removing only B leaves C with a dead parent, so C is
  // orphaned to the root rather than left pointing at a removed row.
  QList<CollectionConfig> cs;
  cs << makeCollection("A", /*isSub=*/false, -1);
  cs << makeCollection("B", /*isSub=*/true, 0);
  cs << makeCollection("C", /*isSub=*/true, 1);
  const QList<CollectionConfig> result =
      CollectionUtils::applyCollectionRemoval(cs, QList<int>{0, -1, 1});
  QCOMPARE(namesOf(result), (QStringList{"A", "C"}));
  QCOMPARE(parentsOf(result), (QList<int>{-1, -1}));
  QVERIFY(!result[1].isSubcollection);
}

void TestCollectionUtilsAncestry::applyCollectionRemoval_healsStaleParentIndex() {
  // B carries a stale, out-of-range parent index. Even with nothing removed
  // the remap heals it to the root so it can't render as a mis-nested ghost.
  QList<CollectionConfig> cs;
  cs << makeCollection("A", /*isSub=*/false, -1);
  cs << makeCollection("B", /*isSub=*/true, 99);
  const QList<CollectionConfig> result =
      CollectionUtils::applyCollectionRemoval(cs, QList<int>{0, 1});
  QCOMPARE(parentsOf(result), (QList<int>{-1, -1}));
  QVERIFY(!result[1].isSubcollection);
}

void TestCollectionUtilsAncestry::applyCollectionRemoval_shiftsSurvivingParentIndex() {
  // A(0), B(1), C(2) → D(3) → E(4). Removing A shifts every later index down
  // by one; the surviving D→C and E→D parent links shift with them.
  QList<CollectionConfig> cs;
  cs << makeCollection("A", /*isSub=*/false, -1);
  cs << makeCollection("B", /*isSub=*/false, -1);
  cs << makeCollection("C", /*isSub=*/false, -1);
  cs << makeCollection("D", /*isSub=*/true, 2);
  cs << makeCollection("E", /*isSub=*/true, 3);
  const QList<CollectionConfig> result =
      CollectionUtils::applyCollectionRemoval(cs, QList<int>{-1, 0, 1, 2, 3});
  QCOMPARE(namesOf(result), (QStringList{"B", "C", "D", "E"}));
  QCOMPARE(parentsOf(result), (QList<int>{-1, -1, 1, 2}));
}

QTEST_APPLESS_MAIN(TestCollectionUtilsAncestry)
#include "test_collectionutils_ancestry.moc"
