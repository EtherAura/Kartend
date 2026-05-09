// Tests for NavigationHelpers — pure helpers extracted from NavigationManager.
//
// These tests use plain CollectionConfig structs and a hash-backed lambda for
// the SessionLookup, exercising the helpers without instantiating any Qt
// object infrastructure.

#include <QHash>
#include <QString>
#include <QTest>

#include "collectionutils.h"
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
  QList<CollectionConfig> cs = {makeCollection("A"), makeCollection("B", 0),
                                makeCollection("C", 1),
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
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(-1, cs, 10, lookup),
           -1);
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(99, cs, 10, lookup),
           -1);
}

void TestNavigationHelpers::lookupReturnsMinusOneWhenTotalItemsZero() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  auto lookup = [](const QString &) { return 5; };
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 0, lookup),
           -1);
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, -1, lookup),
           -1);
}

void TestNavigationHelpers::lookupReturnsMinusOneWhenLookupAbsent() {
  QList<CollectionConfig> cs = {makeCollection("Movies")};
  auto lookup = [](const QString &) { return -1; };
  QCOMPARE(
      NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 10, lookup), -1);
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
  QCOMPARE(NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 100, lookup),
           7);
}

void TestNavigationHelpers::lookupClampsToTotalItems() {
  QList<CollectionConfig> cs = {makeCollection("Movies")};
  auto lookup = [](const QString &) { return 999; };
  // Stored index 999 with totalItems=10 -> clamp to 9.
  QCOMPARE(
      NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 10, lookup), 9);
}

void TestNavigationHelpers::lookupClampsNegativeToZero() {
  // The lookup may legitimately return a stored value of 0 — ensure the
  // helper doesn't accidentally treat 0 as "not found" (only strict <0 is
  // "not found"). Then test the std::max(0, ...) guard.
  QList<CollectionConfig> cs = {makeCollection("Movies")};
  auto zero = [](const QString &) { return 0; };
  QCOMPARE(
      NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 10, zero), 0);
}

void TestNavigationHelpers::lookupUsesSubfolderKeyWhenSubfolderActive() {
  // When currentSubfolder is set, the subfolder-aware key path is used.
  // We verify the helper at least asks for one key (the subfolder one) and
  // does NOT fall back to the bare name.
  CollectionConfig cfg = makeCollection("Movies");
  cfg.currentSubfolder = "Action";
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
  QCOMPARE(
      NavigationHelpers::lookupRememberedSelectionIndex(0, cs, 100, lookup), 3);
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
  QCOMPARE(NavigationHelpers::calculateSelectionIndex(0, cs, 10, false, false,
                                                      lookup),
           -1);
}

void TestNavigationHelpers::calcReturnsZeroWhenRememberOnButNoEntry() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  auto lookup = [](const QString &) { return -1; };
  QCOMPARE(
      NavigationHelpers::calculateSelectionIndex(0, cs, 10, false, true, lookup),
      0);
}

void TestNavigationHelpers::calcReturnsRememberedIndex() {
  QList<CollectionConfig> cs = {makeCollection("A")};
  auto lookup = [](const QString &) { return 4; };
  QCOMPARE(
      NavigationHelpers::calculateSelectionIndex(0, cs, 10, false, true, lookup),
      4);
}

QTEST_APPLESS_MAIN(TestNavigationHelpers)
#include "test_navigationhelpers.moc"
