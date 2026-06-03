// Unit tests for ArtworkPathCatalog::collectArtworkDirs — the static collection-
// tree walk that gathers a collection's artwork directory plus its descendants'.
// Focus: descendant collection across the tree, and that a cyclic
// parentCollectionIndex terminates instead of overflowing the stack
// (Kartend-tqg3r).
#include "artworkpathcatalog.h"

#include "collection/collectionconfig.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QTest>

namespace {
CollectionConfig makeCollection(const QString &name, const QString &artworkDir, int parentIndex) {
  CollectionConfig c;
  c.name = name;
  c.artworkDirectory = artworkDir;
  c.parentCollectionIndex = parentIndex;
  return c;
}
} // namespace

class TestArtworkPathCatalog : public QObject {
  Q_OBJECT
private slots:
  void collectsRootOnlyWhenDescendantsDisabled();
  void collectsRootAndDescendantsAcyclic();
  void cyclicParentIndexTerminates();
  void selfParentTerminates();
};

void TestArtworkPathCatalog::collectsRootOnlyWhenDescendantsDisabled() {
  const QList<CollectionConfig> collections = {
      makeCollection("root", "/art/root", -1),
      makeCollection("child", "/art/child", 0),
  };
  const QStringList dirs = ArtworkPathCatalog::collectArtworkDirs(&collections, 0, false);
  QCOMPARE(dirs.size(), 1);
  QVERIFY(dirs.contains(QStringLiteral("/art/root")));
  QVERIFY(!dirs.contains(QStringLiteral("/art/child")));
}

void TestArtworkPathCatalog::collectsRootAndDescendantsAcyclic() {
  // root(0) -> child(1) -> grandchild(2), plus a sibling(3) directly under root.
  const QList<CollectionConfig> collections = {
      makeCollection("root", "/art/root", -1),
      makeCollection("child", "/art/child", 0),
      makeCollection("grandchild", "/art/grand", 1),
      makeCollection("sibling", "/art/sib", 0),
  };
  const QStringList dirs = ArtworkPathCatalog::collectArtworkDirs(&collections, 0, true);
  QCOMPARE(dirs.size(), 4);
  for (const QString &d : QStringList{"/art/root", "/art/child", "/art/grand", "/art/sib"}) {
    QVERIFY2(dirs.contains(d), qPrintable(d));
  }
}

void TestArtworkPathCatalog::cyclicParentIndexTerminates() {
  // 0's parent is 1 and 1's parent is 0 — a cycle. The walk must terminate (the
  // old recursive version overflowed the stack) and still report both dirs.
  const QList<CollectionConfig> collections = {
      makeCollection("a", "/art/a", 1),
      makeCollection("b", "/art/b", 0),
  };
  const QStringList dirs = ArtworkPathCatalog::collectArtworkDirs(&collections, 0, true);
  QCOMPARE(dirs.size(), 2);
  QVERIFY(dirs.contains(QStringLiteral("/art/a")));
  QVERIFY(dirs.contains(QStringLiteral("/art/b")));
}

void TestArtworkPathCatalog::selfParentTerminates() {
  // A collection whose parent is itself must not loop forever.
  const QList<CollectionConfig> collections = {
      makeCollection("self", "/art/self", 0),
  };
  const QStringList dirs = ArtworkPathCatalog::collectArtworkDirs(&collections, 0, true);
  QCOMPARE(dirs.size(), 1);
  QVERIFY(dirs.contains(QStringLiteral("/art/self")));
}

QTEST_GUILESS_MAIN(TestArtworkPathCatalog)
#include "test_artworkpathcatalog.moc"
