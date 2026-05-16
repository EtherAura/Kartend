/**
 * @file test_collectionutils.cpp
 * @brief Unit tests for CollectionUtils helpers and enum conversions
 */

#include "collectionutils.h"
#include <QTest>

class TestCollectionUtils : public QObject {
  Q_OBJECT

private slots:
  // alignmentToString / stringToAlignment
  void alignmentToString_left();
  void alignmentToString_center();
  void alignmentToString_right();
  void stringToAlignment_left();
  void stringToAlignment_center();
  void stringToAlignment_right();
  void stringToAlignment_caseInsensitive();
  void stringToAlignment_unknownDefaultsToCenter();
  void alignmentRoundTrip();

  // viewTypeToString / stringToViewType
  void viewTypeToString_grid();
  void viewTypeToString_list();
  void stringToViewType_grid();
  void stringToViewType_list();
  void stringToViewType_caseInsensitive();
  void stringToViewType_unknownDefaultsToGrid();
  void viewTypeRoundTrip();

  // isValidIndex overloads
  void isValidIndex_validIndexInRef();
  void isValidIndex_negativeIndexInRef();
  void isValidIndex_outOfBoundsInRef();
  void isValidIndex_emptyListInRef();
  void isValidIndex_validWithPointer();
  void isValidIndex_nullCollectionPointer();
  void isValidIndex_nullIndexPointer();
  void isValidIndex_validIndexPointer();

  // computeCollectionUuid
  void computeCollectionUuid_deterministic();
  void computeCollectionUuid_caseInsensitive();
  void computeCollectionUuid_trimmedWhitespace();
  void computeCollectionUuid_differentInputs();
  void computeCollectionUuid_emptyInputs();
  void computeCollectionUuid_isHex40();

  // ancestorIndexChain
  void ancestorIndexChain_topLevelReturnsEmpty();
  void ancestorIndexChain_oneDeep();
  void ancestorIndexChain_multiDeepRootFirst();
  void ancestorIndexChain_stopsAtNonSubcollection();
  void ancestorIndexChain_invalidParentReturnsEmpty();
  void ancestorIndexChain_cycleIsBounded();

  // placeholder artwork inheritance
  void resolvePlaceholderArtwork_usesOwnValue();
  void resolvePlaceholderArtwork_inheritsFromParent();
  void resolvePlaceholderArtwork_returnsEmptyWhenUnset();

  // alias-parent links via CollectionHierarchyCache
  void hierarchyCache_directChildren_includesLinkedParent();
  void hierarchyCache_linkedDirectChildren_returnsLinkedOnly();
  void hierarchyCache_directChildren_primaryFirstThenLinked();
  void hierarchyCache_directChildren_skipsSelfLink();
  void hierarchyCache_directChildren_skipsUnknownLinkName();
  void hierarchyCache_directChildren_skipsLinkEqualToPrimary();
  void hierarchyCache_allDescendants_handlesMutualLinkCycle();
  void hierarchyCache_allDescendants_excludesSelf();

  // collection categorization
  void effectiveCollectionType_returnsOwnTypeWhenSet();
  void effectiveCollectionType_inheritsFromParentWhenEmpty();
  void effectiveCollectionType_walksMultipleLevels();
  void effectiveCollectionType_emptyWhenNothingTagged();
  void effectiveCollectionType_invalidIndexReturnsEmpty();
  void effectiveCollectionType_trimsWhitespace();
  void collectAllCollectionTypes_returnsSortedUnion();
  void collectAllCollectionTypes_dedupesCaseInsensitive();
  void collectAllCollectionTypes_skipsEmpty();
  void standardCollectionTypes_areTheCuratedPresets();
  void collectionTypeChoices_blankThenPresetsThenInUse();
  void collectionTypeChoices_dropsInUseTypesThatDuplicatePresets();

  // wouldCreateCircularReference
  void circularRef_outOfRangeIndices_treatedAsCircular();
  void circularRef_selfParenting_isCircular();
  void circularRef_directCycle_detected();
  void circularRef_multiHopCycle_detected();
  void circularRef_unrelatedSubtree_isAllowed();
  void circularRef_topLevelReparent_isAllowed();
  void circularRef_existingDataCycle_isBounded();

  // CollectionContext::isValid + synthetic-root mode (Kartend-83iu)
  void context_isValid_defaultIsInvalid();
  void context_isValid_realCollectionIsValid();
  void context_isValid_rootViewWithNoIndexIsValid();

  // collectDescendantIndices + applyCollectionRemoval (collection delete path)
  void collectDescendantIndices_returnsNestedSubtree();
  void collectDescendantIndices_cycleIsBounded();
  void applyCollectionRemoval_remapsParentsAfterRemoval();
  void applyCollectionRemoval_orphansSurvivorWhoseParentWasRemoved();
  void applyCollectionRemoval_healsStaleParentIndex();
  void applyCollectionRemoval_shiftsSurvivingParentIndex();
};

// ─────────────────────────────────────────────────────────────────────────────
// alignment helpers
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::alignmentToString_left() {
  QCOMPARE(CollectionUtils::alignmentToString(HorizontalAlignment::Left), QStringLiteral("left"));
}

void TestCollectionUtils::alignmentToString_center() {
  QCOMPARE(CollectionUtils::alignmentToString(HorizontalAlignment::Center),
           QStringLiteral("center"));
}

void TestCollectionUtils::alignmentToString_right() {
  QCOMPARE(CollectionUtils::alignmentToString(HorizontalAlignment::Right), QStringLiteral("right"));
}

void TestCollectionUtils::stringToAlignment_left() {
  QCOMPARE(CollectionUtils::stringToAlignment("left"), HorizontalAlignment::Left);
}

void TestCollectionUtils::stringToAlignment_center() {
  QCOMPARE(CollectionUtils::stringToAlignment("center"), HorizontalAlignment::Center);
}

void TestCollectionUtils::stringToAlignment_right() {
  QCOMPARE(CollectionUtils::stringToAlignment("right"), HorizontalAlignment::Right);
}

void TestCollectionUtils::stringToAlignment_caseInsensitive() {
  QCOMPARE(CollectionUtils::stringToAlignment("LEFT"), HorizontalAlignment::Left);
  QCOMPARE(CollectionUtils::stringToAlignment("Right"), HorizontalAlignment::Right);
  QCOMPARE(CollectionUtils::stringToAlignment("CeNtEr"), HorizontalAlignment::Center);
}

void TestCollectionUtils::stringToAlignment_unknownDefaultsToCenter() {
  QCOMPARE(CollectionUtils::stringToAlignment(""), HorizontalAlignment::Center);
  QCOMPARE(CollectionUtils::stringToAlignment("nonsense"), HorizontalAlignment::Center);
}

void TestCollectionUtils::alignmentRoundTrip() {
  for (auto a :
       {HorizontalAlignment::Left, HorizontalAlignment::Center, HorizontalAlignment::Right}) {
    QCOMPARE(CollectionUtils::stringToAlignment(CollectionUtils::alignmentToString(a)), a);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// view type helpers
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::viewTypeToString_grid() {
  QCOMPARE(CollectionUtils::viewTypeToString(ViewType::Grid), QStringLiteral("grid"));
}

void TestCollectionUtils::viewTypeToString_list() {
  QCOMPARE(CollectionUtils::viewTypeToString(ViewType::List), QStringLiteral("list"));
}

void TestCollectionUtils::stringToViewType_grid() {
  QCOMPARE(CollectionUtils::stringToViewType("grid"), ViewType::Grid);
}

void TestCollectionUtils::stringToViewType_list() {
  QCOMPARE(CollectionUtils::stringToViewType("list"), ViewType::List);
}

void TestCollectionUtils::stringToViewType_caseInsensitive() {
  QCOMPARE(CollectionUtils::stringToViewType("LIST"), ViewType::List);
  QCOMPARE(CollectionUtils::stringToViewType("Grid"), ViewType::Grid);
}

void TestCollectionUtils::stringToViewType_unknownDefaultsToGrid() {
  QCOMPARE(CollectionUtils::stringToViewType(""), ViewType::Grid);
  QCOMPARE(CollectionUtils::stringToViewType("garbage"), ViewType::Grid);
}

void TestCollectionUtils::viewTypeRoundTrip() {
  for (auto v : {ViewType::Grid, ViewType::List}) {
    QCOMPARE(CollectionUtils::stringToViewType(CollectionUtils::viewTypeToString(v)), v);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// isValidIndex overloads
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::isValidIndex_validIndexInRef() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  list.append(CollectionConfig{});
  QVERIFY(CollectionUtils::isValidIndex(0, list));
  QVERIFY(CollectionUtils::isValidIndex(1, list));
}

void TestCollectionUtils::isValidIndex_negativeIndexInRef() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(!CollectionUtils::isValidIndex(-1, list));
  QVERIFY(!CollectionUtils::isValidIndex(-100, list));
}

void TestCollectionUtils::isValidIndex_outOfBoundsInRef() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(!CollectionUtils::isValidIndex(1, list));
  QVERIFY(!CollectionUtils::isValidIndex(1000, list));
}

void TestCollectionUtils::isValidIndex_emptyListInRef() {
  QList<CollectionConfig> list;
  QVERIFY(!CollectionUtils::isValidIndex(0, list));
  QVERIFY(!CollectionUtils::isValidIndex(-1, list));
}

void TestCollectionUtils::isValidIndex_validWithPointer() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(CollectionUtils::isValidIndex(0, &list));
  QVERIFY(!CollectionUtils::isValidIndex(1, &list));
}

void TestCollectionUtils::isValidIndex_nullCollectionPointer() {
  QVERIFY(!CollectionUtils::isValidIndex(0, static_cast<QList<CollectionConfig> *>(nullptr)));
}

void TestCollectionUtils::isValidIndex_nullIndexPointer() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(!CollectionUtils::isValidIndex(static_cast<int *>(nullptr), &list));
}

void TestCollectionUtils::isValidIndex_validIndexPointer() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  int idx = 0;
  QVERIFY(CollectionUtils::isValidIndex(&idx, &list));
  idx = 5;
  QVERIFY(!CollectionUtils::isValidIndex(&idx, &list));
}

// ─────────────────────────────────────────────────────────────────────────────
// computeCollectionUuid
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::computeCollectionUuid_deterministic() {
  QString a = CollectionUtils::computeCollectionUuid("Games", "/path/to/roms");
  QString b = CollectionUtils::computeCollectionUuid("Games", "/path/to/roms");
  QCOMPARE(a, b);
}

void TestCollectionUtils::computeCollectionUuid_caseInsensitive() {
  QString lower = CollectionUtils::computeCollectionUuid("games", "/x/y");
  QString upper = CollectionUtils::computeCollectionUuid("GAMES", "/X/Y");
  QString mixed = CollectionUtils::computeCollectionUuid("Games", "/X/y");
  QCOMPARE(lower, upper);
  QCOMPARE(lower, mixed);
}

void TestCollectionUtils::computeCollectionUuid_trimmedWhitespace() {
  QString clean = CollectionUtils::computeCollectionUuid("Games", "/x/y");
  QString padded = CollectionUtils::computeCollectionUuid("  Games  ", "/x/y  ");
  // Implementation trims the concatenated "name|mediaDir" string before
  // hashing, so trailing whitespace on the combined value is normalized.
  QString trailingPad = CollectionUtils::computeCollectionUuid("Games", "/x/y  ");
  QCOMPARE(clean, trailingPad);
  QVERIFY(!padded.isEmpty());
}

void TestCollectionUtils::computeCollectionUuid_differentInputs() {
  QString a = CollectionUtils::computeCollectionUuid("Games", "/path/a");
  QString b = CollectionUtils::computeCollectionUuid("Games", "/path/b");
  QString c = CollectionUtils::computeCollectionUuid("Movies", "/path/a");
  QVERIFY(a != b);
  QVERIFY(a != c);
  QVERIFY(b != c);
}

void TestCollectionUtils::computeCollectionUuid_emptyInputs() {
  QString empty = CollectionUtils::computeCollectionUuid("", "");
  QVERIFY(!empty.isEmpty());
  QCOMPARE(empty.size(), 40); // SHA1 hex
}

void TestCollectionUtils::computeCollectionUuid_isHex40() {
  QString uuid = CollectionUtils::computeCollectionUuid("Games", "/x/y");
  QCOMPARE(uuid.size(), 40);
  for (QChar c : uuid) {
    QVERIFY((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// ancestorIndexChain (full-path breadcrumb)
// ─────────────────────────────────────────────────────────────────────────────

namespace {
// Build a minimal CollectionConfig with just the fields ancestorIndexChain
// consults (name, isSubcollection, parentCollectionIndex).
CollectionConfig makeCollection(const QString &name, bool isSub, int parentIdx) {
  CollectionConfig c;
  c.name = name;
  c.isSubcollection = isSub;
  c.parentCollectionIndex = parentIdx;
  return c;
}
} // namespace

void TestCollectionUtils::ancestorIndexChain_topLevelReturnsEmpty() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[0], cs), QList<int>{});
}

void TestCollectionUtils::ancestorIndexChain_oneDeep() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Games", /*isSub=*/false, -1);
  cs << makeCollection("SNES", /*isSub=*/true, 0);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[1], cs), (QList<int>{0}));
}

void TestCollectionUtils::ancestorIndexChain_multiDeepRootFirst() {
  // Root → Games → Nintendo → SNES. Expect ancestors for SNES == [0,1,2].
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  cs << makeCollection("Games", /*isSub=*/true, 0);
  cs << makeCollection("Nintendo", /*isSub=*/true, 1);
  cs << makeCollection("SNES", /*isSub=*/true, 2);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[3], cs), (QList<int>{0, 1, 2}));
}

void TestCollectionUtils::ancestorIndexChain_stopsAtNonSubcollection() {
  // A non-subcollection in the middle terminates the walk (inclusive): it's
  // the root-most ancestor we render in the breadcrumb.
  QList<CollectionConfig> cs;
  cs << makeCollection("Detached", /*isSub=*/false, -1);
  cs << makeCollection("TopLikeRoot", /*isSub=*/false, 0); // not a subcoll
  cs << makeCollection("Child", /*isSub=*/true, 1);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[2], cs), (QList<int>{1}));
}

void TestCollectionUtils::ancestorIndexChain_invalidParentReturnsEmpty() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Orphan", /*isSub=*/true, -1);
  QCOMPARE(CollectionUtils::ancestorIndexChain(cs[0], cs), QList<int>{});
}

void TestCollectionUtils::ancestorIndexChain_cycleIsBounded() {
  // Degenerate cycle: A → B → A. The walk must terminate by bounded-depth
  // guard rather than looping forever.
  QList<CollectionConfig> cs;
  cs << makeCollection("A", /*isSub=*/true, 1);
  cs << makeCollection("B", /*isSub=*/true, 0);
  const QList<int> chain = CollectionUtils::ancestorIndexChain(cs[0], cs);
  QVERIFY(chain.size() <= cs.size());
}

void TestCollectionUtils::resolvePlaceholderArtwork_usesOwnValue() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.placeholderArtwork = QStringLiteral("/art/root.png");
  CollectionConfig child = makeCollection("Child", /*isSub=*/true, 0);
  child.placeholderArtwork = QStringLiteral("/art/child.png");
  cs << root << child;

  QCOMPARE(CollectionUtils::resolvePlaceholderArtwork(1, cs), QStringLiteral("/art/child.png"));
}

void TestCollectionUtils::resolvePlaceholderArtwork_inheritsFromParent() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.placeholderArtwork = QStringLiteral("/art/root.png");
  CollectionConfig child = makeCollection("Child", /*isSub=*/true, 0);
  cs << root << child;

  QCOMPARE(CollectionUtils::resolvePlaceholderArtwork(1, cs), QStringLiteral("/art/root.png"));
}

void TestCollectionUtils::resolvePlaceholderArtwork_returnsEmptyWhenUnset() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  cs << makeCollection("Child", /*isSub=*/true, 0);

  QVERIFY(CollectionUtils::resolvePlaceholderArtwork(1, cs).isEmpty());
}

// ─────────────────────────────────────────────────────────────────────────────
// CollectionHierarchyCache + alias parents
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::hierarchyCache_directChildren_includesLinkedParent() {
  // Root, primary child Console with subcollection Genesis, plus a sibling
  // category Sega that lists Genesis as an additional parent. The merged
  // child list under Sega should expose Genesis even though Sega is not
  // Genesis's primary parent.
  QList<CollectionConfig> cs;
  cs << makeCollection("Console", /*isSub=*/false, -1); // 0
  cs << makeCollection("Sega", /*isSub=*/false, -1);    // 1
  CollectionConfig genesis = makeCollection("Genesis", /*isSub=*/true, 0);
  genesis.additionalParentNames << QStringLiteral("Sega");
  cs << genesis; // 2

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  QCOMPARE(cache.directChildren(0), QList<int>{2});
  QCOMPARE(cache.directChildren(1), QList<int>{2});
}

void TestCollectionUtils::hierarchyCache_linkedDirectChildren_returnsLinkedOnly() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Console", /*isSub=*/false, -1); // 0
  cs << makeCollection("Sega", /*isSub=*/false, -1);    // 1
  CollectionConfig genesis = makeCollection("Genesis", /*isSub=*/true, 0);
  genesis.additionalParentNames << QStringLiteral("Sega");
  cs << genesis; // 2

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  // Console is the primary parent — no linked entry there.
  QVERIFY(cache.linkedDirectChildren(0).isEmpty());
  // Sega holds the alias.
  QCOMPARE(cache.linkedDirectChildren(1), QList<int>{2});
}

void TestCollectionUtils::hierarchyCache_directChildren_primaryFirstThenLinked() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Sega", /*isSub=*/false, -1);    // 0
  cs << makeCollection("SMS", /*isSub=*/true, 0);       // 1 — primary child of Sega
  cs << makeCollection("Console", /*isSub=*/false, -1); // 2
  CollectionConfig genesis = makeCollection("Genesis", /*isSub=*/true, 2);
  genesis.additionalParentNames << QStringLiteral("Sega");
  cs << genesis; // 3 — linked under Sega

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  const QList<int> children = cache.directChildren(0);
  QCOMPARE(children.size(), 2);
  QCOMPARE(children[0], 1); // primary first
  QCOMPARE(children[1], 3); // linked second
}

void TestCollectionUtils::hierarchyCache_directChildren_skipsSelfLink() {
  QList<CollectionConfig> cs;
  CollectionConfig solo = makeCollection("Solo", /*isSub=*/false, -1);
  solo.additionalParentNames << QStringLiteral("Solo"); // self-reference
  cs << solo;

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  QVERIFY(cache.directChildren(0).isEmpty());
  QVERIFY(cache.linkedDirectChildren(0).isEmpty());
}

void TestCollectionUtils::hierarchyCache_directChildren_skipsUnknownLinkName() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Console", /*isSub=*/false, -1);
  CollectionConfig genesis = makeCollection("Genesis", /*isSub=*/true, 0);
  genesis.additionalParentNames << QStringLiteral("DoesNotExist");
  cs << genesis;

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  // Only the primary parent should claim Genesis.
  QCOMPARE(cache.directChildren(0), QList<int>{1});
}

void TestCollectionUtils::hierarchyCache_directChildren_skipsLinkEqualToPrimary() {
  // If the user lists their own primary parent in additionalParentNames,
  // the cache should NOT push the child a second time.
  QList<CollectionConfig> cs;
  cs << makeCollection("Console", /*isSub=*/false, -1);
  CollectionConfig genesis = makeCollection("Genesis", /*isSub=*/true, 0);
  genesis.additionalParentNames << QStringLiteral("Console"); // same as primary
  cs << genesis;

  CollectionHierarchyCache cache;
  cache.rebuild(cs);

  QCOMPARE(cache.directChildren(0), QList<int>{1});
  QVERIFY(cache.linkedDirectChildren(0).isEmpty());
}

void TestCollectionUtils::hierarchyCache_allDescendants_handlesMutualLinkCycle() {
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

void TestCollectionUtils::hierarchyCache_allDescendants_excludesSelf() {
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
// Collection categorization
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::effectiveCollectionType_returnsOwnTypeWhenSet() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.type = QStringLiteral("Games");
  cs << root;
  QCOMPARE(CollectionUtils::effectiveCollectionType(0, cs), QStringLiteral("Games"));
}

void TestCollectionUtils::effectiveCollectionType_inheritsFromParentWhenEmpty() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.type = QStringLiteral("Games");
  CollectionConfig child = makeCollection("Child", /*isSub=*/true, 0);
  // Child has no type; should inherit "Games" from root.
  cs << root << child;
  QCOMPARE(CollectionUtils::effectiveCollectionType(1, cs), QStringLiteral("Games"));
}

void TestCollectionUtils::effectiveCollectionType_walksMultipleLevels() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.type = QStringLiteral("Music");
  CollectionConfig mid = makeCollection("Mid", /*isSub=*/true, 0);
  CollectionConfig leaf = makeCollection("Leaf", /*isSub=*/true, 1);
  cs << root << mid << leaf;
  QCOMPARE(CollectionUtils::effectiveCollectionType(2, cs), QStringLiteral("Music"));
}

void TestCollectionUtils::effectiveCollectionType_emptyWhenNothingTagged() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  cs << makeCollection("Child", /*isSub=*/true, 0);
  QVERIFY(CollectionUtils::effectiveCollectionType(1, cs).isEmpty());
}

void TestCollectionUtils::effectiveCollectionType_invalidIndexReturnsEmpty() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  QVERIFY(CollectionUtils::effectiveCollectionType(-1, cs).isEmpty());
  QVERIFY(CollectionUtils::effectiveCollectionType(99, cs).isEmpty());
}

void TestCollectionUtils::effectiveCollectionType_trimsWhitespace() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.type = QStringLiteral("  Movies  ");
  cs << root;
  // Whitespace gets trimmed so the toolbar dropdown doesn't show ghost
  // entries that look identical to a clean tag.
  QCOMPARE(CollectionUtils::effectiveCollectionType(0, cs), QStringLiteral("Movies"));
}

void TestCollectionUtils::collectAllCollectionTypes_returnsSortedUnion() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("Music");
  CollectionConfig b = makeCollection("B", /*isSub=*/false, -1);
  b.type = QStringLiteral("Games");
  CollectionConfig c = makeCollection("C", /*isSub=*/false, -1);
  c.type = QStringLiteral("Books");
  cs << a << b << c;
  const QStringList result = CollectionUtils::collectAllCollectionTypes(cs);
  QCOMPARE(result, (QStringList{QStringLiteral("Books"), QStringLiteral("Games"),
                                QStringLiteral("Music")}));
}

void TestCollectionUtils::collectAllCollectionTypes_dedupesCaseInsensitive() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("Games");
  CollectionConfig b = makeCollection("B", /*isSub=*/false, -1);
  b.type = QStringLiteral("games"); // duplicate by case
  CollectionConfig c = makeCollection("C", /*isSub=*/true, 0);
  c.type = QStringLiteral("Movies");
  cs << a << b << c;
  const QStringList result = CollectionUtils::collectAllCollectionTypes(cs);
  // First occurrence wins for casing, so "Games" survives, "games" dropped.
  QCOMPARE(result.size(), 2);
  QVERIFY(result.contains(QStringLiteral("Games")));
  QVERIFY(result.contains(QStringLiteral("Movies")));
  QVERIFY(!result.contains(QStringLiteral("games")));
}

void TestCollectionUtils::collectAllCollectionTypes_skipsEmpty() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("Games");
  CollectionConfig b = makeCollection("B", /*isSub=*/false, -1);
  // Empty type — should be skipped entirely.
  CollectionConfig c = makeCollection("C", /*isSub=*/false, -1);
  c.type = QStringLiteral("   "); // whitespace-only also skipped
  cs << a << b << c;
  const QStringList result = CollectionUtils::collectAllCollectionTypes(cs);
  QCOMPARE(result, QStringList{QStringLiteral("Games")});
}

void TestCollectionUtils::standardCollectionTypes_areTheCuratedPresets() {
  const QStringList presets = CollectionUtils::standardCollectionTypes();
  QCOMPARE(presets, (QStringList{QStringLiteral("Video"), QStringLiteral("Audio"),
                                 QStringLiteral("Images"), QStringLiteral("Documents"),
                                 QStringLiteral("Games")}));
}

void TestCollectionUtils::collectionTypeChoices_blankThenPresetsThenInUse() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("Podcasts"); // a custom type not in the presets
  cs << a;
  const QStringList choices = CollectionUtils::collectionTypeChoices(cs);
  // Leading blank entry (untagged), then the presets in display order.
  QVERIFY(!choices.isEmpty());
  QVERIFY(choices.first().isEmpty());
  QCOMPARE(choices.mid(1, 5), CollectionUtils::standardCollectionTypes());
  // The custom in-use type is appended after the presets.
  QCOMPARE(choices.last(), QStringLiteral("Podcasts"));
}

void TestCollectionUtils::collectionTypeChoices_dropsInUseTypesThatDuplicatePresets() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("games"); // case-insensitively duplicates the "Games" preset
  cs << a;
  const QStringList choices = CollectionUtils::collectionTypeChoices(cs);
  // blank + 5 presets, with no extra entry for the duplicate.
  QCOMPARE(choices.size(), 6);
  QVERIFY(!choices.contains(QStringLiteral("games")));
}

// ─────────────────────────────────────────────────────────────────────────────
// wouldCreateCircularReference
// ─────────────────────────────────────────────────────────────────────────────

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

void TestCollectionUtils::circularRef_outOfRangeIndices_treatedAsCircular() {
  const auto cs = sampleHierarchy();
  QVERIFY(CollectionUtils::wouldCreateCircularReference(-1, 0, cs));
  QVERIFY(CollectionUtils::wouldCreateCircularReference(0, -1, cs));
  QVERIFY(CollectionUtils::wouldCreateCircularReference(99, 0, cs));
  QVERIFY(CollectionUtils::wouldCreateCircularReference(0, 99, cs));
}

void TestCollectionUtils::circularRef_selfParenting_isCircular() {
  const auto cs = sampleHierarchy();
  QVERIFY(CollectionUtils::wouldCreateCircularReference(2, 2, cs));
}

void TestCollectionUtils::circularRef_directCycle_detected() {
  // Reparent ChildA (1) under its own child GrandchildA (2) — direct cycle.
  const auto cs = sampleHierarchy();
  QVERIFY(CollectionUtils::wouldCreateCircularReference(1, 2, cs));
}

void TestCollectionUtils::circularRef_multiHopCycle_detected() {
  // Reparent Root (0) under GrandchildA (2) — would make Root descend from
  // itself via 0 → 2 → 1 → 0.
  const auto cs = sampleHierarchy();
  QVERIFY(CollectionUtils::wouldCreateCircularReference(0, 2, cs));
}

void TestCollectionUtils::circularRef_unrelatedSubtree_isAllowed() {
  // Move ChildB (3) under OtherRoot (4) — different subtree, no cycle.
  const auto cs = sampleHierarchy();
  QVERIFY(!CollectionUtils::wouldCreateCircularReference(3, 4, cs));
}

void TestCollectionUtils::circularRef_topLevelReparent_isAllowed() {
  // Move ChildA (1) under OtherRoot (4) — top-level swap, no cycle.
  const auto cs = sampleHierarchy();
  QVERIFY(!CollectionUtils::wouldCreateCircularReference(1, 4, cs));
}

void TestCollectionUtils::circularRef_existingDataCycle_isBounded() {
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

// ─────────────────────────────────────────────────────────────────────────────
// CollectionContext::isValid + synthetic root-view mode (Kartend-83iu)
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::context_isValid_defaultIsInvalid() {
  CollectionContext ctx;
  QVERIFY(!ctx.isValid());
}

void TestCollectionUtils::context_isValid_realCollectionIsValid() {
  CollectionContext ctx;
  ctx.currentIndex = 0;
  QVERIFY(ctx.isValid());
}

void TestCollectionUtils::context_isValid_rootViewWithNoIndexIsValid() {
  CollectionContext ctx;
  ctx.currentIndex = -1;
  ctx.isRootView = true;
  QVERIFY(ctx.isValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// collectDescendantIndices + applyCollectionRemoval (collection delete path)
// ─────────────────────────────────────────────────────────────────────────────

namespace {
// Pull the names / parent indices out of a remap result so it can be
// asserted against plain QList literals (the whole-list QCOMPARE idiom used
// throughout this file).
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

void TestCollectionUtils::collectDescendantIndices_returnsNestedSubtree() {
  // Root(0) → Games(1) → SNES(2); Root(0) → Audio(3). Descendants of the
  // root are every nested collection; descendants of a leaf are empty.
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  cs << makeCollection("Games", /*isSub=*/true, 0);
  cs << makeCollection("SNES", /*isSub=*/true, 1);
  cs << makeCollection("Audio", /*isSub=*/true, 0);
  QCOMPARE(CollectionUtils::collectDescendantIndices(0, cs), (QList<int>{1, 2, 3}));
  QCOMPARE(CollectionUtils::collectDescendantIndices(1, cs), (QList<int>{2}));
  QCOMPARE(CollectionUtils::collectDescendantIndices(2, cs), QList<int>{});
}

void TestCollectionUtils::collectDescendantIndices_cycleIsBounded() {
  // Corrupt input: A(0) and B(1) each name the other as parent. The walk
  // must terminate via the visited-set guard instead of recursing until the
  // stack overflows (the test would crash, not merely fail, otherwise).
  QList<CollectionConfig> cs;
  cs << makeCollection("A", /*isSub=*/true, 1);
  cs << makeCollection("B", /*isSub=*/true, 0);
  const QList<int> descendants = CollectionUtils::collectDescendantIndices(0, cs);
  QVERIFY(descendants.size() <= cs.size());
}

void TestCollectionUtils::applyCollectionRemoval_remapsParentsAfterRemoval() {
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

void TestCollectionUtils::applyCollectionRemoval_orphansSurvivorWhoseParentWasRemoved() {
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

void TestCollectionUtils::applyCollectionRemoval_healsStaleParentIndex() {
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

void TestCollectionUtils::applyCollectionRemoval_shiftsSurvivingParentIndex() {
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

QTEST_APPLESS_MAIN(TestCollectionUtils)
#include "test_collectionutils.moc"
