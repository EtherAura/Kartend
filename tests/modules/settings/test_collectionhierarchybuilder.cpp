// Unit tests for CollectionHierarchyBuilder::build (Kartend-93vuk) — the pure
// algorithm that turns the per-INI-section CollectionConfig hash into the final
// ordered, parent-linked collection list. Correctness-critical for
// reorder/rename round-trips (a stranded child = a lost subcollection), and
// designed to be tested without a SettingsManager.
#include <QtTest/QtTest>

#include "collection/collectionconfig.h"
#include "collectionhierarchybuilder.h"

namespace {
CollectionConfig cfg(const QString &name) {
  CollectionConfig c;
  c.name = name;
  return c;
}
// Find a built collection by name; returns -1 if absent.
int indexOfName(const QList<CollectionConfig> &list, const QString &name) {
  for (int i = 0; i < list.size(); ++i) {
    if (list[i].name == name) return i;
  }
  return -1;
}
} // namespace

class TestCollectionHierarchyBuilder : public QObject {
  Q_OBJECT
private slots:
  void topLevelOnly_appendedSortedAsRoots();
  void parentChild_linksChildToParent();
  void grandchild_resolvesViaHierarchicalPath();
  void orphanSubcollection_isDropped();
  void rootsPrecedeSubcollections();
};

void TestCollectionHierarchyBuilder::topLevelOnly_appendedSortedAsRoots() {
  QHash<QString, CollectionConfig> temp;
  temp.insert(QStringLiteral("Zebra"), cfg(QStringLiteral("Zebra")));
  temp.insert(QStringLiteral("Apple"), cfg(QStringLiteral("Apple")));

  QList<CollectionConfig> out;
  CollectionHierarchyBuilder::build(temp, out);

  QCOMPARE(out.size(), 2);
  // Section names are sorted, so Apple precedes Zebra.
  QCOMPARE(out[0].name, QStringLiteral("Apple"));
  QCOMPARE(out[1].name, QStringLiteral("Zebra"));
  for (const auto &c : out) {
    QVERIFY(!c.isSubcollection);
    QCOMPARE(c.parentCollectionIndex, -1);
  }
}

void TestCollectionHierarchyBuilder::parentChild_linksChildToParent() {
  QHash<QString, CollectionConfig> temp;
  temp.insert(QStringLiteral("A"), cfg(QStringLiteral("A")));
  temp.insert(QStringLiteral("A/B"), cfg(QStringLiteral("B")));

  QList<CollectionConfig> out;
  CollectionHierarchyBuilder::build(temp, out);

  QCOMPARE(out.size(), 2);
  const int a = indexOfName(out, QStringLiteral("A"));
  const int b = indexOfName(out, QStringLiteral("B"));
  QVERIFY(a >= 0 && b >= 0);
  // Roots come first, so A is index 0 and B follows.
  QCOMPARE(a, 0);
  QVERIFY(!out[a].isSubcollection);
  QVERIFY(out[b].isSubcollection);
  QCOMPARE(out[b].parentCollectionIndex, a);
}

void TestCollectionHierarchyBuilder::grandchild_resolvesViaHierarchicalPath() {
  QHash<QString, CollectionConfig> temp;
  temp.insert(QStringLiteral("A"), cfg(QStringLiteral("A")));
  temp.insert(QStringLiteral("A/B"), cfg(QStringLiteral("B")));
  temp.insert(QStringLiteral("A/B/C"), cfg(QStringLiteral("C")));

  QList<CollectionConfig> out;
  CollectionHierarchyBuilder::build(temp, out);

  QCOMPARE(out.size(), 3);
  const int a = indexOfName(out, QStringLiteral("A"));
  const int b = indexOfName(out, QStringLiteral("B"));
  const int c = indexOfName(out, QStringLiteral("C"));
  QVERIFY(a >= 0 && b >= 0 && c >= 0);
  QCOMPARE(out[b].parentCollectionIndex, a);
  // C resolves its parent via the full hierarchical path "A/B", not just the
  // leaf name "B" — the grandchild path-matching branch.
  QCOMPARE(out[c].parentCollectionIndex, b);
  QVERIFY(out[c].isSubcollection);
}

void TestCollectionHierarchyBuilder::orphanSubcollection_isDropped() {
  // A subcollection whose parent section is absent has no resolvable parent, so
  // it must NOT be appended (otherwise it would surface as a parentless ghost
  // row). This is the safety contract for a corrupted / partially-edited INI.
  QHash<QString, CollectionConfig> temp;
  temp.insert(QStringLiteral("Ghost/Child"), cfg(QStringLiteral("Child")));

  QList<CollectionConfig> out;
  CollectionHierarchyBuilder::build(temp, out);

  QCOMPARE(out.size(), 0);
}

void TestCollectionHierarchyBuilder::rootsPrecedeSubcollections() {
  // Build order is "all roots first (sorted), then all subcollections
  // (sorted)" so parentCollectionIndex references always point at an
  // already-appended root. Mixed input must still come out roots-first.
  QHash<QString, CollectionConfig> temp;
  temp.insert(QStringLiteral("A/B"), cfg(QStringLiteral("B")));
  temp.insert(QStringLiteral("A"), cfg(QStringLiteral("A")));
  temp.insert(QStringLiteral("Z"), cfg(QStringLiteral("Z")));

  QList<CollectionConfig> out;
  CollectionHierarchyBuilder::build(temp, out);

  QCOMPARE(out.size(), 3);
  // First two entries are the roots (A, Z sorted); the subcollection B is last.
  QVERIFY(!out[0].isSubcollection);
  QVERIFY(!out[1].isSubcollection);
  QCOMPARE(out[0].name, QStringLiteral("A"));
  QCOMPARE(out[1].name, QStringLiteral("Z"));
  QCOMPARE(out[2].name, QStringLiteral("B"));
  QVERIFY(out[2].isSubcollection);
  QCOMPARE(out[2].parentCollectionIndex, 0);
}

QTEST_MAIN(TestCollectionHierarchyBuilder)
#include "test_collectionhierarchybuilder.moc"
