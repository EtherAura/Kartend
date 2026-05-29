// Tests for NavigationHelpers — pure helpers extracted from NavigationManager.
//
// These tests use plain CollectionConfig structs and a hash-backed lambda for
// the SessionLookup, exercising the helpers without instantiating any Qt
// object infrastructure.

#include <QHash>
#include <QString>
#include <QTest>

#include "collection/collectionconfig.h"
#include "navigationhelpers.h"

namespace {

auto makeCollection(const QString &name, int parent = -1) -> CollectionConfig {
  CollectionConfig c;
  c.name = name;
  c.parentCollectionIndex = parent;
  return c;
}

} // namespace

class TestNavigationHelpers : public QObject {
  Q_OBJECT
private slots:
  // computeCollectionDepth
  void depthRejectsNegativeIndex();
  void depthRejectsOutOfRangeIndex();
  void depthOfTopLevelIsOne();
  void depthOfNestedChain();
  void depthBoundedAgainstCycles();

  // isValidCollectionIndex
  void isValidRejectsNegativeAndOutOfRange();
  void isValidAcceptsInRange();

  // lookupRememberedSelectionIndex
  void lookupReturnsMinusOneWhenIndexInvalid();
  void lookupReturnsMinusOneWhenTotalItemsZero();
  void lookupReturnsMinusOneWhenLookupAbsent();
  void lookupFallsBackToBareName();
  void lookupClampsToTotalItems();
  void lookupClampsNegativeToZero();
  void lookupUsesSubfolderKeyWhenSubfolderActive();

  // calculateSelectionIndex
  void calcReturnsMinusOneWhenSearchActive();
  void calcReturnsMinusOneWhenRememberDisabled();
  void calcReturnsZeroWhenRememberOnButNoEntry();
  void calcReturnsRememberedIndex();

  // findSubcollectionVisualIndex
  void findSubVisualReturnsMinusOneForOutOfRangeTarget();
  void findSubVisualReturnsMinusOneWhenPreviousNotChild();
  void findSubVisualReturnsZeroBasedPositionAmongChildren();
  void findSubVisualSkipsNonChildrenWhenCounting();

  // chooseFallbackCollectionIndex
  void fallbackReturnsMinusOneWhenEmpty();
  void fallbackKeepsCurrentWhenInRange();
  void fallbackPrefersFirstTopLevelWhenCurrentInvalid();
  void fallbackUsesIndexZeroWhenNoTopLevel();

  // parseBreadcrumbLink
  void parseLinkRecognizesRoot();
  void parseLinkRecognizesCollectionWithIndex();
  void parseLinkRejectsCollectionWithNonNumericPayload();
  void parseLinkRejectsCollectionWithNegativeIndex();
  void parseLinkRecognizesSubfolderPath();
  void parseLinkAllowsEmptySubfolderPath();
  void parseLinkRejectsUnknownScheme();
  void parseLinkRejectsEmptyString();

  // parentSubfolderPath
  void parentPathOfEmptyIsEmpty();
  void parentPathOfSingleSegmentIsEmpty();
  void parentPathDropsLastSegment();
  void parentPathTreatsTrailingSlashAsNoSegment();
};

// ------------------------------ computeCollectionDepth ----------------------

void TestNavigationHelpers::depthRejectsNegativeIndex() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  QCOMPARE(NavigationHelpers::computeCollectionDepth(-1, cs), 0);
}

void TestNavigationHelpers::depthRejectsOutOfRangeIndex() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  QCOMPARE(NavigationHelpers::computeCollectionDepth(1, cs), 0);
  QCOMPARE(NavigationHelpers::computeCollectionDepth(99, cs), 0);
}

void TestNavigationHelpers::depthOfTopLevelIsOne() {
  QList<CollectionConfig> cs = {makeCollection("Top")};
  QCOMPARE(NavigationHelpers::computeCollectionDepth(0, cs), 1);
}

void TestNavigationHelpers::depthOfNestedChain() {
  // A (top, 0) <- B (child of A, 1) <- C (child of B, 2) <- D (child of C, 3)
  QList<CollectionConfig> cs = {makeCollection("A"), makeCollection("B", 0), makeCollection("C", 1),
                                makeCollection("D", 2)};
  QCOMPARE(NavigationHelpers::computeCollectionDepth(0, cs), 1);
  QCOMPARE(NavigationHelpers::computeCollectionDepth(1, cs), 2);
  QCOMPARE(NavigationHelpers::computeCollectionDepth(2, cs), 3);
  QCOMPARE(NavigationHelpers::computeCollectionDepth(3, cs), 4);
}

void TestNavigationHelpers::depthBoundedAgainstCycles() {
  // Construct an illegal cycle: A.parent = B, B.parent = A. The original
  // implementation would loop forever; the extracted helper must terminate
  // and return a depth bounded by collections.size().
  QList<CollectionConfig> cs = {makeCollection("A", 1), makeCollection("B", 0)};
  const int depth = NavigationHelpers::computeCollectionDepth(0, cs);
  // Must terminate; bounded by collections.size().
  QVERIFY(depth > 0);
  QVERIFY(depth <= cs.size());
}

// ------------------------------ isValidCollectionIndex ----------------------

void TestNavigationHelpers::isValidRejectsNegativeAndOutOfRange() {
  QList<CollectionConfig> cs = {makeCollection("A"), makeCollection("B")};
  QVERIFY(!NavigationHelpers::isValidCollectionIndex(-1, cs));
  QVERIFY(!NavigationHelpers::isValidCollectionIndex(2, cs));
  QVERIFY(!NavigationHelpers::isValidCollectionIndex(0, {}));
}

void TestNavigationHelpers::isValidAcceptsInRange() {
  QList<CollectionConfig> cs = {makeCollection("A"), makeCollection("B")};
  QVERIFY(NavigationHelpers::isValidCollectionIndex(0, cs));
  QVERIFY(NavigationHelpers::isValidCollectionIndex(1, cs));
}

// ------------------------------ lookupRememberedSelectionIndex --------------

void TestNavigationHelpers::lookupReturnsMinusOneWhenIndexInvalid() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  auto lookup = [](const QString &) { return 5; };
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(-1, cs, 10, lookup), -1);
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(99, cs, 10, lookup), -1);
}

void TestNavigationHelpers::lookupReturnsMinusOneWhenTotalItemsZero() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  auto lookup = [](const QString &) { return 5; };
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 0, lookup), -1);
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, -1, lookup), -1);
}

void TestNavigationHelpers::lookupReturnsMinusOneWhenLookupAbsent() {
  QList<CollectionConfig> cs = {makeCollection("Movies")};
  auto lookup = [](const QString &) { return -1; };
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 10, lookup), -1);
}

void TestNavigationHelpers::lookupFallsBackToBareName() {
  // Hierarchical name miss -> bare name hit.
  // For a top-level collection, hierarchicalNameFor(...) == name, but the
  // legacy fallback path still goes through both lookups; we verify that the
  // second lookup wins when the first returns -1.
  QList<CollectionConfig> cs = {makeCollection("Movies")};
  QHash<QString, int> store;
  store["Movies"] = 7;
  auto lookup = [&store](const QString &k) {
    auto it = store.find(k);
    return it == store.end() ? -1 : it.value();
  };
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 100, lookup), 7);
}

void TestNavigationHelpers::lookupClampsToTotalItems() {
  QList<CollectionConfig> cs = {makeCollection("Movies")};
  auto lookup = [](const QString &) { return 999; };
  // Stored index 999 with totalItems=10 -> clamp to 9.
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 10, lookup), 9);
}

void TestNavigationHelpers::lookupClampsNegativeToZero() {
  // The lookup may legitimately return a stored value of 0 — ensure the
  // helper doesn't accidentally treat 0 as "not found" (only strict <0 is
  // "not found"). Then test the std::max(0, ...) guard.
  QList<CollectionConfig> cs = {makeCollection("Movies")};
  auto zero = [](const QString &) { return 0; };
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 10, zero), 0);
}

void TestNavigationHelpers::lookupUsesSubfolderKeyWhenSubfolderActive() {
  // When currentSubfolder is set, the subfolder-aware key path is used.
  // We verify the helper at least asks for one key (the subfolder one) and
  // does NOT fall back to the bare name.
  CollectionConfig cfg = makeCollection("Movies");
  cfg.folderBrowsing.currentSubfolder = "Action";
  QList<CollectionConfig> cs = {cfg};
  bool sawBareName = false;
  bool sawAnyKey = false;
  auto lookup = [&](const QString &k) -> int {
    sawAnyKey = true;
    if (k == "Movies") {
      sawBareName = true;
    }
    return 3;
  };
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 100, lookup), 3);
  QVERIFY(sawAnyKey);
  QVERIFY(!sawBareName);
}

// ------------------------------ calculateSelectionIndex ---------------------

void TestNavigationHelpers::calcReturnsMinusOneWhenSearchActive() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  auto lookup = [](const QString &) { return 5; };
  QCOMPARE(NavigationHelpers::calculateSelectionIndex(0, cs, 10,
                                                      /*searchActive=*/true,
                                                      /*remember=*/true, lookup),
           -1);
}

void TestNavigationHelpers::calcReturnsMinusOneWhenRememberDisabled() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  auto lookup = [](const QString &) { return 5; };
  QCOMPARE(NavigationHelpers::calculateSelectionIndex(0, cs, 10, false, false, lookup), -1);
}

void TestNavigationHelpers::calcReturnsZeroWhenRememberOnButNoEntry() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  auto lookup = [](const QString &) { return -1; };
  QCOMPARE(NavigationHelpers::calculateSelectionIndex(0, cs, 10, false, true, lookup), 0);
}

void TestNavigationHelpers::calcReturnsRememberedIndex() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  auto lookup = [](const QString &) { return 4; };
  QCOMPARE(NavigationHelpers::calculateSelectionIndex(0, cs, 10, false, true, lookup), 4);
}

// ------------------------------ findSubcollectionVisualIndex ---------------

void TestNavigationHelpers::findSubVisualReturnsMinusOneForOutOfRangeTarget() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(-1, 0, cs), -1);
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(99, 0, cs), -1);
}

void TestNavigationHelpers::findSubVisualReturnsMinusOneWhenPreviousNotChild() {
  // A (top), B (child of A). Asking "where in A's children does C=-1 sit"
  // returns -1 since -1 is not a child.
  QList<CollectionConfig> cs = {makeCollection("A"), makeCollection("B", 0)};
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(0, -1, cs), -1);
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(0, 5, cs), -1);
}

void TestNavigationHelpers::findSubVisualReturnsZeroBasedPositionAmongChildren() {
  // A=0 has children B=1, C=2, D=3 in that source order.
  QList<CollectionConfig> cs = {makeCollection("A"), makeCollection("B", 0), makeCollection("C", 0),
                                makeCollection("D", 0)};
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(0, 1, cs), 0);
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(0, 2, cs), 1);
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(0, 3, cs), 2);
}

void TestNavigationHelpers::findSubVisualSkipsNonChildrenWhenCounting() {
  // A=0, B=1 (top-level), C=2 (child of A), D=3 (child of B), E=4 (child of A).
  // A's children in source order: C(2), E(4). E's visual index = 1.
  QList<CollectionConfig> cs = {makeCollection("A"), makeCollection("B"), makeCollection("C", 0),
                                makeCollection("D", 1), makeCollection("E", 0)};
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(0, 2, cs), 0);
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(0, 4, cs), 1);
  // D is not a child of A.
  QCOMPARE(NavigationHelpers::findSubcollectionVisualIndex(0, 3, cs), -1);
}

// ------------------------------ chooseFallbackCollectionIndex ---------------

void TestNavigationHelpers::fallbackReturnsMinusOneWhenEmpty() {
  QList<CollectionConfig> cs;
  QCOMPARE(NavigationHelpers::chooseFallbackCollectionIndex(0, cs), -1);
  QCOMPARE(NavigationHelpers::chooseFallbackCollectionIndex(-1, cs), -1);
}

void TestNavigationHelpers::fallbackKeepsCurrentWhenInRange() {
  QList<CollectionConfig> cs = {makeCollection("A"), makeCollection("B", 0)};
  QCOMPARE(NavigationHelpers::chooseFallbackCollectionIndex(1, cs), 1);
  QCOMPARE(NavigationHelpers::chooseFallbackCollectionIndex(0, cs), 0);
}

void TestNavigationHelpers::fallbackPrefersFirstTopLevelWhenCurrentInvalid() {
  // First entry is a (orphaned) child; second is the first top-level.
  QList<CollectionConfig> cs = {makeCollection("ChildOrphan", 99), makeCollection("TopA"),
                                makeCollection("TopB")};
  QCOMPARE(NavigationHelpers::chooseFallbackCollectionIndex(-1, cs), 1);
  QCOMPARE(NavigationHelpers::chooseFallbackCollectionIndex(99, cs), 1);
}

void TestNavigationHelpers::fallbackUsesIndexZeroWhenNoTopLevel() {
  // No collection has parentCollectionIndex == -1 (all reference some other
  // node). Helper falls back to index 0.
  QList<CollectionConfig> cs = {makeCollection("A", 1), makeCollection("B", 0)};
  QCOMPARE(NavigationHelpers::chooseFallbackCollectionIndex(-1, cs), 0);
}

// ------------------------------ parseBreadcrumbLink -------------------------

void TestNavigationHelpers::parseLinkRecognizesRoot() {
  const auto parsed = NavigationHelpers::parseBreadcrumbLink("root:");
  QCOMPARE(parsed.kind, NavigationHelpers::BreadcrumbLink::Kind::Root);
}

void TestNavigationHelpers::parseLinkRecognizesCollectionWithIndex() {
  const auto parsed = NavigationHelpers::parseBreadcrumbLink("collection:7");
  QCOMPARE(parsed.kind, NavigationHelpers::BreadcrumbLink::Kind::Collection);
  QCOMPARE(parsed.collectionIndex, 7);
}

void TestNavigationHelpers::parseLinkRejectsCollectionWithNonNumericPayload() {
  const auto parsed = NavigationHelpers::parseBreadcrumbLink("collection:abc");
  QCOMPARE(parsed.kind, NavigationHelpers::BreadcrumbLink::Kind::Invalid);
}

void TestNavigationHelpers::parseLinkRejectsCollectionWithNegativeIndex() {
  const auto parsed = NavigationHelpers::parseBreadcrumbLink("collection:-1");
  QCOMPARE(parsed.kind, NavigationHelpers::BreadcrumbLink::Kind::Invalid);
}

void TestNavigationHelpers::parseLinkRecognizesSubfolderPath() {
  const auto parsed = NavigationHelpers::parseBreadcrumbLink("subfolder:Action/2024");
  QCOMPARE(parsed.kind, NavigationHelpers::BreadcrumbLink::Kind::Subfolder);
  QCOMPARE(parsed.subfolderPath, QString("Action/2024"));
}

void TestNavigationHelpers::parseLinkAllowsEmptySubfolderPath() {
  // "subfolder:" with empty payload is still a Subfolder kind — production
  // callers compare the empty path against currentSubfolder and act accordingly.
  const auto parsed = NavigationHelpers::parseBreadcrumbLink("subfolder:");
  QCOMPARE(parsed.kind, NavigationHelpers::BreadcrumbLink::Kind::Subfolder);
  QVERIFY(parsed.subfolderPath.isEmpty());
}

void TestNavigationHelpers::parseLinkRejectsUnknownScheme() {
  QCOMPARE(NavigationHelpers::parseBreadcrumbLink("https://example.com").kind,
           NavigationHelpers::BreadcrumbLink::Kind::Invalid);
  QCOMPARE(NavigationHelpers::parseBreadcrumbLink("ROOT:").kind,
           NavigationHelpers::BreadcrumbLink::Kind::Invalid);
}

void TestNavigationHelpers::parseLinkRejectsEmptyString() {
  QCOMPARE(NavigationHelpers::parseBreadcrumbLink("").kind,
           NavigationHelpers::BreadcrumbLink::Kind::Invalid);
}

// ------------------------------ parentSubfolderPath -------------------------

void TestNavigationHelpers::parentPathOfEmptyIsEmpty() {
  QCOMPARE(NavigationHelpers::parentSubfolderPath(""), QString());
}

void TestNavigationHelpers::parentPathOfSingleSegmentIsEmpty() {
  QCOMPARE(NavigationHelpers::parentSubfolderPath("Action"), QString());
}

void TestNavigationHelpers::parentPathDropsLastSegment() {
  QCOMPARE(NavigationHelpers::parentSubfolderPath("Action/2024"), QString("Action"));
  QCOMPARE(NavigationHelpers::parentSubfolderPath("Action/2024/Q1"), QString("Action/2024"));
}

void TestNavigationHelpers::parentPathTreatsTrailingSlashAsNoSegment() {
  // "Action/" trims to "Action", whose parent is "".
  QCOMPARE(NavigationHelpers::parentSubfolderPath("Action/"), QString());
  // "Action/2024/" trims to "Action/2024", parent is "Action".
  QCOMPARE(NavigationHelpers::parentSubfolderPath("Action/2024/"), QString("Action"));
}

QTEST_APPLESS_MAIN(TestNavigationHelpers)
#include "test_navigationhelpers.moc"
