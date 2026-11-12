/**
 * @file test_filtermanager.h
 * @brief Integration tests for FilterManager's public filter surface.
 *
 * Construction-only and source-data-only paths can be tested without a
 * full MainWindowFixture because FilterManager exposes setSourceData /
 * setCollections / setHierarchyCache as a self-contained DI seam. These
 * tests exercise the apply / clear / index-mapping API in that mode.
 */

#ifndef KARTEND_TESTS_TEST_FILTERMANAGER_H
#define KARTEND_TESTS_TEST_FILTERMANAGER_H

#include <QObject>

class TestFilterManager : public QObject {
  Q_OBJECT

private slots:
  void testInitialStateIsUnfiltered();
  void testSetSourceDataLeavesUnfilteredCountInvariant();
  void testApplyEmptyFilterIsNoOp();
  void testApplyCaseInsensitiveSubstringFilter();
  void testApplyFilterEmitsFilterChanged();
  void testClearFilterRestoresUnfilteredView();
  void testGetActualIndexBoundsCheck();
  void testHideMissingArtworkFlagToggles();

  // [subcollections][virtualFolders][media] index-space coverage.
  void testSearchWithVirtualFoldersOffsetsMediaIndices();
  void testSearchMatchesVirtualFolderByDisplayName();
  void testHideMissingArtworkBaselineKeepsPrefixBands();
  void testHideMissingArtworkKeepsUnloadedRowsVisible();
  void testHideMissingArtworkColdCascadeFailsOpen();
  void testActualIndexRoundTripAcrossPrefixBands();
  void testUnifiedSortMapRemapsFilteredIndices();
  // Re-running the active filter keeps a subcollection filter a subcollection
  // filter instead of turning it into a text search on its name (Kartend-3ve3n).
  void testReapplyKeepsSubcollectionFilterSemantics();

  // applySubcollectionFilter directly (Kartend-kie1n): early-return guards,
  // unconditional prefix bands, media-only counts and the artwork predicate.
  // Without an ApplicationContext, membership falls back to name matching.
  void testSubcollectionFilterIgnoresOutOfRangeIndex();
  void testSubcollectionFilterIgnoresMissingSourceData();
  void testSubcollectionFilterKeepsPrefixBandsAndCountsMediaOnly();
  void testSubcollectionFilterComposesWithHideMissingArtwork();

  // Precomputed artwork key set (ArtworkUtils::buildArtworkKeySet) backing
  // the hideMissingArtwork predicate.
  void testBuildArtworkKeySetFromCachedListings();
  void testHideMissingArtworkResolvesSubdirAndFullNameKeys();

  // Hand-linked covers (Kartend-1js9j): the name-based key set cannot see a
  // manual item_artwork link, so the predicate consults the manual-cover map
  // the render surfaces paint from before it consults the key set.
  void testHideMissingArtworkKeepsHandLinkedItems();
  void testHideMissingArtworkHandLinkAnswersEvenOnAColdCascade();

  // A mirroring collection keeps each subfolder's art under the matching
  // artwork subdirectory, which is where the render path looks (Kartend-7f76f).
  void testHideMissingArtworkMirrorsSubfolderIntoArtworkTree();
  void testHideMissingArtworkWithoutMirroringStillUsesTheRoot();
};

#endif // KARTEND_TESTS_TEST_FILTERMANAGER_H
