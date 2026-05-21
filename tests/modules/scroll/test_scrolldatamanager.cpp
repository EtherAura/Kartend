/**
 * @file test_scrolldatamanager.cpp
 * @brief Tile-population tests for ScrollDataStore. Focused on the
 *        synthetic Home view path (CollectionContext::isRootView) added in
 *        Kartend-83iu.
 */

#include "collectionutils.h"
#include "scrolldatamanager.h"

#include <QList>
#include <QTest>

class TestScrollDataManager : public QObject {
  Q_OBJECT

private slots:
  void rootView_populatesTilesFromOverride();
  void rootView_emptyOverrideProducesNoTiles();
  void rootView_currentIndexNegativeIsAccepted();
  void nonRootView_currentIndexNegativeIsRejected();
  void rootView_virtualFoldersSuppressed();
};

namespace {
QList<CollectionConfig> threeRootsAndOneChild() {
  QList<CollectionConfig> cs;
  CollectionConfig a;
  a.name = QStringLiteral("Video");
  a.parentCollectionIndex = -1;
  cs.append(a);
  CollectionConfig b;
  b.name = QStringLiteral("Audio");
  b.parentCollectionIndex = -1;
  cs.append(b);
  CollectionConfig child;
  child.name = QStringLiteral("Films");
  child.parentCollectionIndex = 0;
  cs.append(child);
  CollectionConfig c;
  c.name = QStringLiteral("Reference");
  c.parentCollectionIndex = -1;
  cs.append(c);
  return cs;
}
} // namespace

void TestScrollDataManager::rootView_populatesTilesFromOverride() {
  const auto cs = threeRootsAndOneChild();
  CollectionContext ctx;
  ctx.isRootView = true;
  ctx.currentIndex = -1;
  ctx.hasSubcollectionOverride = true;
  // Indices of the three parentCollectionIndex == -1 entries.
  ctx.subcollectionOverride = {0, 1, 3};
  ctx.suppressVirtualFolders = true;

  ScrollDataStore sdm;
  sdm.initializeSubcollections(ctx, &cs, /*hierarchyCache=*/nullptr);

  // After alphabetic sort (NameAscending default): Audio, Reference, Video → 1, 3, 0.
  const QList<int> expected{1, 3, 0};
  QCOMPARE(sdm.subcollections(), expected);
  QCOMPARE(sdm.fileCount(), 0);
}

void TestScrollDataManager::rootView_emptyOverrideProducesNoTiles() {
  const QList<CollectionConfig> cs;
  CollectionContext ctx;
  ctx.isRootView = true;
  ctx.currentIndex = -1;
  ctx.hasSubcollectionOverride = true;
  ctx.subcollectionOverride = {};

  ScrollDataStore sdm;
  sdm.initializeSubcollections(ctx, &cs, /*hierarchyCache=*/nullptr);

  QCOMPARE(sdm.subcollectionCount(), 0);
}

void TestScrollDataManager::rootView_currentIndexNegativeIsAccepted() {
  const auto cs = threeRootsAndOneChild();
  CollectionContext ctx;
  ctx.isRootView = true;
  ctx.currentIndex = -1;
  ctx.hasSubcollectionOverride = true;
  ctx.subcollectionOverride = {0};

  ScrollDataStore sdm;
  sdm.initializeSubcollections(ctx, &cs, /*hierarchyCache=*/nullptr);

  // The synthetic-root bypass lets currentIndex=-1 through; the override
  // populates the tile list.
  QCOMPARE(sdm.subcollectionCount(), 1);
}

void TestScrollDataManager::nonRootView_currentIndexNegativeIsRejected() {
  const auto cs = threeRootsAndOneChild();
  CollectionContext ctx;
  // Default isRootView = false. currentIndex = -1 must early-return so a
  // bogus context doesn't accidentally pull in the override list.
  ctx.currentIndex = -1;
  ctx.hasSubcollectionOverride = true;
  ctx.subcollectionOverride = {0, 1};

  ScrollDataStore sdm;
  sdm.initializeSubcollections(ctx, &cs, /*hierarchyCache=*/nullptr);

  QCOMPARE(sdm.subcollectionCount(), 0);
}

void TestScrollDataManager::rootView_virtualFoldersSuppressed() {
  const auto cs = threeRootsAndOneChild();
  CollectionContext ctx;
  ctx.isRootView = true;
  ctx.currentIndex = -1;
  ctx.suppressVirtualFolders = true;

  ScrollDataStore sdm;
  sdm.initializeVirtualFolders(ctx);

  QCOMPARE(sdm.virtualFolderCount(), 0);
}

QTEST_APPLESS_MAIN(TestScrollDataManager)
#include "test_scrolldatamanager.moc"
