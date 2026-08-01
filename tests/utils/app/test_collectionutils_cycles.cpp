/**
 * @file test_collectionutils_cycles.cpp
 * @brief Unit tests for CollectionUtils::wouldCreateCircularReference.
 *
 * Extracted from test_collectionutils.cpp as a cohesion split — the original
 * single test class mixed pure-conversion helpers, tree walks, and these
 * cycle-detection cases together at low graphify-community cohesion. Keeping
 * the cycle-detection slots in their own translation unit lets each concern
 * grow independently and surfaces clearly in CTest's per-binary output.
 */

#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include <QTest>

class TestCollectionUtilsCycles : public QObject {
  Q_OBJECT

private slots:
  void circularRef_outOfRangeIndices_treatedAsCircular();
  void circularRef_selfParenting_isCircular();
  void circularRef_directCycle_detected();
  void circularRef_multiHopCycle_detected();
  void circularRef_unrelatedSubtree_isAllowed();
  void circularRef_topLevelReparent_isAllowed();
  void circularRef_existingDataCycle_isBounded();

  // Kartend-14mll: the two walkers that lacked the sibling guards — a
  // pre-existing 2-cycle in corrupt input must terminate, not hang (and, for
  // hierarchicalNameFor, not grow the parts list without bound).
  void hierarchicalNameFor_cyclicParents_terminates();
  void resolveInheritedField_cyclicParents_terminates();
};

namespace {
// Build a small hierarchy:
//   0: Root (top-level)
//     1: ChildA (parent=0)
//       2: GrandchildA (parent=1)
//     3: ChildB (parent=0)
//   4: OtherRoot (top-level)
QList<CollectionConfig> sampleHierarchy() {
  QList<CollectionConfig> cs;
  CollectionConfig root;
  root.name = QStringLiteral("Root");
  root.parentCollectionIndex = -1;
  cs.append(root);

  CollectionConfig childA;
  childA.name = QStringLiteral("ChildA");
  childA.parentCollectionIndex = 0;
  childA.isSubcollection = true;
  cs.append(childA);

  CollectionConfig grandchildA;
  grandchildA.name = QStringLiteral("GrandchildA");
  grandchildA.parentCollectionIndex = 1;
  grandchildA.isSubcollection = true;
  cs.append(grandchildA);

  CollectionConfig childB;
  childB.name = QStringLiteral("ChildB");
  childB.parentCollectionIndex = 0;
  childB.isSubcollection = true;
  cs.append(childB);

  CollectionConfig otherRoot;
  otherRoot.name = QStringLiteral("OtherRoot");
  otherRoot.parentCollectionIndex = -1;
  cs.append(otherRoot);
  return cs;
}
} // namespace

void TestCollectionUtilsCycles::circularRef_outOfRangeIndices_treatedAsCircular() {
  const auto cs = sampleHierarchy();
  QVERIFY(CollectionUtils::wouldCreateCircularReference(-1, 0, cs));
  QVERIFY(CollectionUtils::wouldCreateCircularReference(0, -1, cs));
  QVERIFY(CollectionUtils::wouldCreateCircularReference(99, 0, cs));
  QVERIFY(CollectionUtils::wouldCreateCircularReference(0, 99, cs));
}

void TestCollectionUtilsCycles::circularRef_selfParenting_isCircular() {
  const auto cs = sampleHierarchy();
  QVERIFY(CollectionUtils::wouldCreateCircularReference(2, 2, cs));
}

void TestCollectionUtilsCycles::circularRef_directCycle_detected() {
  // Reparent ChildA (1) under its own child GrandchildA (2) — direct cycle.
  const auto cs = sampleHierarchy();
  QVERIFY(CollectionUtils::wouldCreateCircularReference(1, 2, cs));
}

void TestCollectionUtilsCycles::circularRef_multiHopCycle_detected() {
  // Reparent Root (0) under GrandchildA (2) — would make Root descend from
  // itself via 0 → 2 → 1 → 0.
  const auto cs = sampleHierarchy();
  QVERIFY(CollectionUtils::wouldCreateCircularReference(0, 2, cs));
}

void TestCollectionUtilsCycles::circularRef_unrelatedSubtree_isAllowed() {
  // Move ChildB (3) under OtherRoot (4) — different subtree, no cycle.
  const auto cs = sampleHierarchy();
  QVERIFY(!CollectionUtils::wouldCreateCircularReference(3, 4, cs));
}

void TestCollectionUtilsCycles::circularRef_topLevelReparent_isAllowed() {
  // Move ChildA (1) under OtherRoot (4) — top-level swap, no cycle.
  const auto cs = sampleHierarchy();
  QVERIFY(!CollectionUtils::wouldCreateCircularReference(1, 4, cs));
}

void TestCollectionUtilsCycles::circularRef_existingDataCycle_isBounded() {
  // Build corrupt input where 1 → 2 → 1 (existing cycle); the function must
  // terminate AND report circular rather than infinite-looping.
  QList<CollectionConfig> cs;
  CollectionConfig a;
  a.name = QStringLiteral("A");
  a.parentCollectionIndex = -1;
  cs.append(a);
  CollectionConfig b;
  b.name = QStringLiteral("B");
  b.parentCollectionIndex = 2; // points at C
  cs.append(b);
  CollectionConfig c;
  c.name = QStringLiteral("C");
  c.parentCollectionIndex = 1; // points back at B → cycle
  cs.append(c);

  // Asking whether reparenting 0 under 1 would be circular hits the existing
  // 1↔2 cycle while walking up. Must detect and return true within bounded
  // time (test would hang otherwise).
  QVERIFY(CollectionUtils::wouldCreateCircularReference(0, 1, cs));
}

namespace {
// Corrupt input with a 2-cycle among subcollections: 1 → 2 → 1. Both carry
// isSubcollection so the walkers keep following parent links; index 0 is a
// healthy top-level row for the control assertions.
QList<CollectionConfig> cyclicHierarchy() {
  QList<CollectionConfig> cs;
  CollectionConfig root;
  root.name = QStringLiteral("Root");
  root.parentCollectionIndex = -1;
  cs.append(root);
  CollectionConfig b;
  b.name = QStringLiteral("B");
  b.parentCollectionIndex = 2; // points at C
  b.isSubcollection = true;
  cs.append(b);
  CollectionConfig c;
  c.name = QStringLiteral("C");
  c.parentCollectionIndex = 1; // points back at B → cycle
  c.isSubcollection = true;
  cs.append(c);
  return cs;
}
} // namespace

void TestCollectionUtilsCycles::hierarchicalNameFor_cyclicParents_terminates() {
  const auto cs = cyclicHierarchy();
  // Termination is the contract; the joined name must stay bounded by the
  // walk limit (own name + at most collections.size() ancestors) instead of
  // growing until allocation failure.
  const QString name = CollectionUtils::hierarchicalNameFor(cs[1], cs);
  QVERIFY(!name.isEmpty());
  QVERIFY(name.count(QLatin1Char('/')) <= cs.size());
  QVERIFY(name.endsWith(QStringLiteral("B")));

  // Healthy input is unaffected by the guard.
  QCOMPARE(CollectionUtils::hierarchicalNameFor(cs[0], cs), QStringLiteral("Root"));
}

void TestCollectionUtilsCycles::resolveInheritedField_cyclicParents_terminates() {
  auto cs = cyclicHierarchy();
  // All fields empty along the cycle: must terminate and report nothing.
  QCOMPARE(CollectionUtils::resolveArtworkDirectory(1, cs), QString());

  // A value somewhere on the cycle is still found before the bound trips.
  cs[2].artworkDirectory = QStringLiteral("/art");
  QCOMPARE(CollectionUtils::resolveArtworkDirectory(1, cs), QStringLiteral("/art"));
}

QTEST_APPLESS_MAIN(TestCollectionUtilsCycles)
#include "test_collectionutils_cycles.moc"
