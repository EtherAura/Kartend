// Kartend-ob1c9: structure tests for the collection tree panel's pure model
// builder. The interesting shapes: alias parents duplicating a subtree (DAG,
// intended), a hostile parent cycle rendering finitely, playlists grouped
// out of the tree with reserved kinds first, and out-of-range child indices
// ignored.

#include <QTest>

#include "collection/collectiontreemodel.h"

namespace {

CollectionConfig plain(const QString &name, int parent = -1) {
  CollectionConfig c;
  c.name = name;
  c.mediaDirectory = "/media/" + name;
  c.parentCollectionIndex = parent;
  c.isSubcollection = parent != -1;
  return c;
}

CollectionConfig playlist(const QString &name, const QString &reservedKind = QString(),
                          int parent = -1) {
  CollectionConfig c = plain(name, parent);
  c.isPlaylist = true;
  c.playlistReservedKind = reservedKind;
  return c;
}

/// Flattened "index(children…)" rendering for compact structure asserts.
QString render(const CollectionTreeModel::Node &node) {
  QString out = QString::number(node.collectionIndex);
  if (!node.children.isEmpty()) {
    QStringList parts;
    for (const CollectionTreeModel::Node &child : node.children) {
      parts.append(render(child));
    }
    out += "(" + parts.join(",") + ")";
  }
  return out;
}

QString renderRoots(const CollectionTreeModel::Model &model) {
  QStringList parts;
  for (const CollectionTreeModel::Node &root : model.collectionRoots) {
    parts.append(render(root));
  }
  return parts.join(" ");
}

} // namespace

class TestCollectionTreeModel : public QObject {
  Q_OBJECT

private slots:
  void nestingBuildsRecursiveNodes();
  void aliasParentDuplicatesSubtreeUnderBothParents();
  void playlistsAreGroupedReservedFirstAndNeverInline();
  void hostileCycleRendersFinitely();
  void outOfRangeChildIsIgnored();
  void childrenSortCaseInsensitivelyLikeTheGrid();
  void caseOnlyTiesKeepConfigOrder();
};

void TestCollectionTreeModel::nestingBuildsRecursiveNodes() {
  QList<CollectionConfig> cols;
  cols << plain("Films") << plain("Concerts", 0) << plain("Bootlegs", 1) << plain("Audio");
  CollectionHierarchyCache cache;
  cache.rebuild(cols);

  const auto model = CollectionTreeModel::build(cols, cache);
  QCOMPARE(renderRoots(model), QStringLiteral("0(1(2)) 3"));
  QVERIFY(model.playlistIndices.isEmpty());
}

void TestCollectionTreeModel::aliasParentDuplicatesSubtreeUnderBothParents() {
  // 2 is primarily a child of 0, and additionally linked under 1 — the DAG
  // case. The tree intentionally shows it in both places.
  QList<CollectionConfig> cols;
  cols << plain("A") << plain("B") << plain("Shared", 0);
  cols[2].additionalParentNames = QStringList{QStringLiteral("B")};
  CollectionHierarchyCache cache;
  cache.rebuild(cols);

  const auto model = CollectionTreeModel::build(cols, cache);
  QCOMPARE(renderRoots(model), QStringLiteral("0(2) 1(2)"));
}

void TestCollectionTreeModel::playlistsAreGroupedReservedFirstAndNeverInline() {
  // The synthesized Favorites row is parented under collection 0 for the
  // home-view tiles; the tree still pulls every playlist into the grouped
  // section instead, reserved kinds first.
  QList<CollectionConfig> cols;
  cols << plain("Films") << playlist("Road Trip Mix") << playlist("Favorites", "favorites", 0);
  CollectionHierarchyCache cache;
  cache.rebuild(cols);

  const auto model = CollectionTreeModel::build(cols, cache);
  QCOMPARE(renderRoots(model), QStringLiteral("0"));
  QCOMPARE(model.playlistIndices, (QList<int>{2, 1}));
}

void TestCollectionTreeModel::hostileCycleRendersFinitely() {
  // parentCollectionIndex cycles are prevented at edit time, but a
  // hand-edited INI can still carry one — the builder must terminate and
  // render each node once per path.
  QList<CollectionConfig> cols;
  cols << plain("A", 1) << plain("B", 0);
  CollectionHierarchyCache cache;
  cache.rebuild(cols);

  const auto model = CollectionTreeModel::build(cols, cache);
  // Neither is a root (both have parents), so the cycle simply renders
  // nothing rather than hanging or overflowing.
  QCOMPARE(renderRoots(model), QString());
}

void TestCollectionTreeModel::outOfRangeChildIsIgnored() {
  QList<CollectionConfig> cols;
  cols << plain("Films") << plain("Concerts", 0);
  CollectionHierarchyCache cache;
  cache.rebuild(cols);
  // Shrink the list after the cache was built — the builder must bounds-check
  // against the CURRENT list, mirroring how navigation code treats indices.
  cols.removeLast();

  const auto model = CollectionTreeModel::build(cols, cache);
  QCOMPARE(renderRoots(model), QStringLiteral("0"));
}

// Kartend-fxn4v: the reporter's exact vendor set. Config order here is the
// case-SENSITIVE directory-scan order a recursive import produces on Linux,
// which is what the sidebar used to render verbatim — "NEC, Nintendo, SNK,
// Sega, Sharp", because 'N' < 'e' in ASCII. The grid sorted the same five
// case-insensitively, so one screen showed two different orders.
void TestCollectionTreeModel::childrenSortCaseInsensitivelyLikeTheGrid() {
  QList<CollectionConfig> cols;
  cols << plain("Consoles") << plain("NEC", 0) << plain("Nintendo", 0) << plain("SNK", 0)
       << plain("Sega", 0) << plain("Sharp", 0);
  CollectionHierarchyCache cache;
  cache.rebuild(cols);

  const auto model = CollectionTreeModel::build(cols, cache);

  // Indices 1..5 are NEC, Nintendo, SNK, Sega, Sharp in config order. Sorted
  // case-insensitively that is NEC, Nintendo, Sega, Sharp, SNK → 1,2,4,5,3.
  QCOMPARE(renderRoots(model), QStringLiteral("0(1,2,4,5,3)"));
}

// Case-insensitive compare rates "Sega" and "SEGA" equal, so the sort must be
// STABLE — an unstable pass would order such pairs arbitrarily and the sidebar
// could reshuffle between builds for no visible reason.
void TestCollectionTreeModel::caseOnlyTiesKeepConfigOrder() {
  QList<CollectionConfig> cols;
  cols << plain("Root") << plain("SEGA", 0) << plain("sega", 0) << plain("Sega", 0);
  CollectionHierarchyCache cache;
  cache.rebuild(cols);

  const auto model = CollectionTreeModel::build(cols, cache);
  QCOMPARE(renderRoots(model), QStringLiteral("0(1,2,3)"));
}

QTEST_MAIN(TestCollectionTreeModel)
#include "test_collectiontreemodel.moc"
