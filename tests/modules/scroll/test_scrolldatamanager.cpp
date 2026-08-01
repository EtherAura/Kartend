/**
 * @file test_scrolldatamanager.cpp
 * @brief Tile-population tests for ScrollDataStore. Focused on the
 *        synthetic Home view path (CollectionContext::isRootView) added in
 *        Kartend-83iu.
 */

#include "collection/collectioncontext.h"
#include "scrolldatamanager.h"

#include <algorithm>
#include <QDir>
#include <QList>
#include <QTemporaryDir>
#include <QTest>

class TestScrollDataManager : public QObject {
  Q_OBJECT

private slots:
  void rootView_populatesTilesFromOverride();
  void rootView_emptyOverrideProducesNoTiles();
  void rootView_currentIndexNegativeIsAccepted();
  void nonRootView_currentIndexNegativeIsRejected();
  void rootView_virtualFoldersSuppressed();
  void unifiedSortMap_roundTripsConcatSpace();
  void unifiedSortMap_emptyWhenSortInactive();
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

void TestScrollDataManager::unifiedSortMap_roundTripsConcatSpace() {
  const auto cs = threeRootsAndOneChild();

  // Two on-disk subfolders make real virtual folder entries.
  QTemporaryDir mediaDir;
  QVERIFY(mediaDir.isValid());
  QVERIFY(QDir(mediaDir.path()).mkdir(QStringLiteral("beta_folder")));
  QVERIFY(QDir(mediaDir.path()).mkdir(QStringLiteral("delta_folder")));

  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.hasSubcollectionOverride = true;
  ctx.subcollectionOverride = {2}; // "Films"
  ctx.config.mediaDirectory = mediaDir.path();
  ctx.config.folderBrowsing.includeContentSubfolders = true;
  ctx.config.folderBrowsing.showAllSubfolderItems = false;

  ScrollDataStore sdm;
  sdm.initializeSubcollections(ctx, &cs, /*hierarchyCache=*/nullptr);
  sdm.initializeVirtualFolders(ctx);
  sdm.filePaths() = {QStringLiteral("/m/Aardvark.bin"), QStringLiteral("/m/Zebra.bin")};
  sdm.applyUnifiedSort(ctx, &cs);
  QVERIFY(sdm.isUnifiedSortActive());

  const QList<int> map = sdm.unifiedConcatToActualMap();
  const int subCount = sdm.subcollectionCount();
  const int folderCount = sdm.virtualFolderCount();
  QCOMPARE(map.size(), sdm.totalItemCount());

  // The map is a bijection onto [0, totalItemCount).
  QList<int> coverage = map;
  std::sort(coverage.begin(), coverage.end());
  for (int i = 0; i < coverage.size(); ++i) {
    QCOMPARE(coverage[i], i);
  }

  // Each concat band round-trips through the store's unified-aware
  // classification helpers.
  for (int i = 0; i < subCount; ++i) {
    const int actual = map[i];
    QVERIFY(sdm.isSubcollectionIndex(actual));
    QCOMPARE(sdm.subcollectionIndexFromActual(actual), sdm.subcollections()[i]);
  }
  for (int j = 0; j < folderCount; ++j) {
    const int actual = map[subCount + j];
    QVERIFY(sdm.isVirtualFolderIndex(actual));
    QCOMPARE(sdm.virtualFolderFromActual(actual), sdm.virtualFolders()[j]);
  }
  for (int k = 0; k < sdm.fileCount(); ++k) {
    const int actual = map[subCount + folderCount + k];
    QVERIFY(sdm.isMediaIndex(actual));
    QCOMPARE(sdm.mediaIndexFromActual(actual), k);
  }
}

void TestScrollDataManager::unifiedSortMap_emptyWhenSortInactive() {
  const auto cs = threeRootsAndOneChild();
  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.hasSubcollectionOverride = true;
  ctx.subcollectionOverride = {2};
  // Exclusion turns unified sorting off; concat and actual spaces coincide,
  // signalled by an empty (identity) map.
  ctx.excludeSubfoldersFromSort = true;

  ScrollDataStore sdm;
  sdm.initializeSubcollections(ctx, &cs, /*hierarchyCache=*/nullptr);
  sdm.filePaths() = {QStringLiteral("/m/Aardvark.bin")};
  sdm.applyUnifiedSort(ctx, &cs);
  QVERIFY(!sdm.isUnifiedSortActive());
  QVERIFY(sdm.unifiedConcatToActualMap().isEmpty());
}

QTEST_APPLESS_MAIN(TestScrollDataManager)
#include "test_scrolldatamanager.moc"
