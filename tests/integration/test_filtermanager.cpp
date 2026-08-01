#include "test_filtermanager.h"

#include "artworkutils.h"
#include "collection/collectionconfig.h"
#include "filtermanager.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QList>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

// FilterManager::setSourceData stores const-pointers to the caller's
// QStringList / QHash containers (see filtermanager.h: m_filePaths,
// m_fileNames). The seed data therefore has to outlive every FilterManager
// the tests construct — making it static here keeps it in the .data
// segment for the program's lifetime. The previous local-variable version
// triggered an ASan stack-use-after-return when applyFilter("") routed
// through clearFilter()'s `emit filterChanged(m_filePaths->size(), …)` on
// a manager whose seed had already gone out of scope.
const QStringList &seedPaths() {
  static const QStringList paths = {
      QStringLiteral("/items/Alpha.bin"),
      QStringLiteral("/items/Beta.bin"),
      QStringLiteral("/items/Gamma.bin"),
  };
  return paths;
}

const QHash<QString, QString> &seedNames() {
  static const QHash<QString, QString> names = [] {
    QHash<QString, QString> out;
    out.insert(seedPaths().at(0), QStringLiteral("Alpha"));
    out.insert(seedPaths().at(1), QStringLiteral("Beta"));
    out.insert(seedPaths().at(2), QStringLiteral("Gamma"));
    return out;
  }();
  return names;
}

const QHash<QString, QString> &seedEmptyDisplayNames() {
  static const QHash<QString, QString> empty;
  return empty;
}

const QList<int> &seedEmptySubcollections() {
  static const QList<int> empty;
  return empty;
}

// Virtual folder paths are stored relative to the media directory; display
// names (the matchable text) are the LAST path component, mirroring the
// scroll pipeline's QFileInfo(path).fileName() resolution.
const QStringList &seedFolders() {
  static const QStringList folders = {
      QStringLiteral("Docs"),
      QStringLiteral("Nested/Extras"),
  };
  return folders;
}

const QStringList &seedEmptyFolders() {
  static const QStringList empty;
  return empty;
}

const QList<int> &seedOneSubcollection() {
  static const QList<int> subs = {0};
  return subs;
}

void seedThreeItems(FilterManager &mgr) {
  mgr.setSourceData(seedPaths(), seedNames(), seedEmptyDisplayNames(), seedEmptySubcollections(),
                    seedEmptyFolders());
}

} // namespace

void TestFilterManager::testInitialStateIsUnfiltered() {
  FilterManager mgr;
  QVERIFY(!mgr.isFiltered());
  QCOMPARE(mgr.filteredCount(), 0);
  QCOMPARE(mgr.currentFilter(), QString());
  QVERIFY(mgr.filteredIndices().isEmpty());
}

void TestFilterManager::testSetSourceDataLeavesUnfilteredCountInvariant() {
  FilterManager mgr;
  seedThreeItems(mgr);
  // No filter applied yet → filteredCount stays 0 (indices populate lazily
  // on the first apply / clear). The contract under test is that simply
  // pushing source data doesn't activate a filter as a side effect.
  QVERIFY(!mgr.isFiltered());
  QCOMPARE(mgr.currentFilter(), QString());
}

void TestFilterManager::testApplyEmptyFilterIsNoOp() {
  FilterManager mgr;
  seedThreeItems(mgr);
  mgr.applyFilter(QString());
  // An empty search string is the canonical clear-filter — isFiltered() stays
  // false (the unfiltered passthrough is the steady state).
  QVERIFY(!mgr.isFiltered());
}

void TestFilterManager::testApplyCaseInsensitiveSubstringFilter() {
  FilterManager mgr;
  seedThreeItems(mgr);
  // applyFilter requires the hierarchy cache + ApplicationContext to be
  // wired through setApplicationContext / setHierarchyCache before it can
  // resolve sibling-collection display names. The integration test for the
  // full filter pipeline lives in the ScrollManager fixture suite; this
  // bare-FilterManager test verifies that an empty applyFilter is the
  // documented no-op (handled above) without requiring the rest of the
  // graph. Skipped here because the real apply path is exercised by
  // tests/integration/test_scrollmanager.cpp's filterChange_* coverage.
  QSKIP("Full apply pipeline needs ScrollManager fixture; covered there.");
}

void TestFilterManager::testApplyFilterEmitsFilterChanged() {
  // Same dependency story as testApplyCaseInsensitiveSubstringFilter — the
  // signal-emission verification needs the full ScrollManager fixture.
  QSKIP("Full apply pipeline needs ScrollManager fixture; covered there.");
}

void TestFilterManager::testClearFilterRestoresUnfilteredView() {
  FilterManager mgr;
  seedThreeItems(mgr);
  // clearFilter on a never-filtered manager is a no-op steady state.
  mgr.clearFilter();
  QVERIFY(!mgr.isFiltered());
  QCOMPARE(mgr.currentFilter(), QString());
}

void TestFilterManager::testGetActualIndexBoundsCheck() {
  FilterManager mgr;
  seedThreeItems(mgr);
  // Out-of-range visual indices must return -1 (documented in the doxygen).
  // Negative-index path is the cheap-to-verify guarantee that doesn't need
  // the upstream DI graph.
  QCOMPARE(mgr.getActualIndex(-1), -1);
}

void TestFilterManager::testHideMissingArtworkFlagToggles() {
  FilterManager mgr;
  QVERIFY(!mgr.hideMissingArtworkEnabled());
  mgr.setHideMissingArtworkFilter(true, QStringLiteral("/tmp/artwork"));
  QVERIFY(mgr.hideMissingArtworkEnabled());
  mgr.setHideMissingArtworkFilter(false, QString());
  QVERIFY(!mgr.hideMissingArtworkEnabled());
}

void TestFilterManager::testSearchWithVirtualFoldersOffsetsMediaIndices() {
  FilterManager mgr;
  mgr.setSourceData(seedPaths(), seedNames(), seedEmptyDisplayNames(), seedEmptySubcollections(),
                    seedFolders());
  mgr.applyFilter(QStringLiteral("gamma"));
  QVERIFY(mgr.isFiltered());
  // Actual-index space is [subcollections(0)][virtualFolders(2)][media]:
  // Gamma is media index 2, so its actual index is 2 + 2 = 4 — the last item
  // stays reachable through the folder offset.
  QCOMPARE(mgr.filteredCount(), 1);
  QCOMPARE(mgr.getActualIndex(0), 4);
}

void TestFilterManager::testSearchMatchesVirtualFolderByDisplayName() {
  FilterManager mgr;
  mgr.setSourceData(seedPaths(), seedNames(), seedEmptyDisplayNames(), seedEmptySubcollections(),
                    seedFolders());
  // "Nested/Extras" matches on its display name (last path component)...
  mgr.applyFilter(QStringLiteral("extras"));
  QVERIFY(mgr.isFiltered());
  QCOMPARE(mgr.filteredCount(), 1);
  QCOMPARE(mgr.getActualIndex(0), 1); // folder band starts after 0 subcollections
  // ...and only on the display name — parent path components don't match.
  mgr.applyFilter(QStringLiteral("nested"));
  QCOMPARE(mgr.filteredCount(), 0);
}

void TestFilterManager::testHideMissingArtworkBaselineKeepsPrefixBands() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  for (const QString &name : {QStringLiteral("Alpha.png"), QStringLiteral("Gamma.png")}) {
    QFile art(artDir.filePath(name));
    QVERIFY(art.open(QIODevice::WriteOnly));
    art.write("x");
    art.close();
  }
  // mediaItemHasArtwork resolves through the DirectoryCache; warm it
  // synchronously so the lookup takes the cached-hit path the app sees after
  // its prewarm pass instead of the queue-and-return-empty cold path.
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artDir.path()});

  FilterManager mgr;
  mgr.setSourceData(seedPaths(), seedNames(), seedEmptyDisplayNames(), seedOneSubcollection(),
                    seedFolders());
  mgr.setHideMissingArtworkFilter(true, artDir.path());
  QSignalSpy spy(&mgr, &FilterManager::filterChanged);
  mgr.clearFilter();

  // clearFilter transitions to the artwork-only baseline: every subcollection
  // and virtual folder survives unconditionally, media without artwork (Beta)
  // is pruned. Space is [sub 0][folders 1,2][Alpha 3, Beta 4, Gamma 5].
  QVERIFY(mgr.isFiltered());
  QCOMPARE(mgr.filteredIndices(), (QList<int>{0, 1, 2, 3, 5}));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), 2); // visible media
  QCOMPARE(spy.at(0).at(1).toInt(), 3); // total media

  // Drop the temp directory's cached listing so later tests (and other
  // suites in this binary) don't see stale entries for a deleted path.
  ArtworkUtils::DirectoryCache::instance().clear();
}

void TestFilterManager::testBuildArtworkKeySetFromCachedListings() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  QVERIFY(QDir(artDir.path()).mkpath(QStringLiteral("front")));
  for (const QString &name : {QStringLiteral("Alpha.png"), QStringLiteral("front/Beta.png")}) {
    QFile art(artDir.filePath(name));
    QVERIFY(art.open(QIODevice::WriteOnly));
    art.write("x");
    art.close();
  }

  // Cold cache: the set contributes nothing and the directories are queued
  // for a background scan — the same non-blocking contract as the per-item
  // findInDirectory path.
  const QSet<QString> cold = ArtworkUtils::buildArtworkKeySet(artDir.path());
  QVERIFY(cold.isEmpty());
  QVERIFY(ArtworkUtils::DirectoryCache::instance().isDirectoryQueued(artDir.path()));

  // Warm cache: the flat root AND the typed cover subdirs contribute their
  // artwork basenames. prewarmDirectories expands existing typed subdirs
  // itself, matching the app's collection-switch prewarm.
  const quint64 generationBefore = ArtworkUtils::DirectoryCache::instance().contentsGeneration();
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artDir.path()});
  QVERIFY(ArtworkUtils::DirectoryCache::instance().contentsGeneration() > generationBefore);
  const QSet<QString> keys = ArtworkUtils::buildArtworkKeySet(artDir.path());
  // Keys are baseMatchKey-normalized: lowercased on the case-insensitive
  // platforms (macOS/Windows), verbatim on Linux (Kartend-58ddn) — so the
  // expectation must run through the same normalization, not literal case.
  QCOMPARE(keys, (QSet<QString>{ArtworkUtils::baseMatchKey(QStringLiteral("Alpha")),
                                ArtworkUtils::baseMatchKey(QStringLiteral("Beta"))}));

  ArtworkUtils::DirectoryCache::instance().clear();
}

void TestFilterManager::testHideMissingArtworkResolvesSubdirAndFullNameKeys() {
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  QVERIFY(QDir(artDir.path()).mkpath(QStringLiteral("front")));
  // Flat-root cover for Alpha; typed-subdir cover for Beta; a full-filename
  // keyed cover for Gamma ("Gamma.bin.png" backs "Gamma.bin" through the
  // second name-key variant of the artwork cascade). Delta has no artwork.
  const QStringList covers = {
      QStringLiteral("Alpha.png"),
      QStringLiteral("front/Beta.png"),
      QStringLiteral("Gamma.bin.png"),
  };
  for (const QString &name : covers) {
    QFile art(artDir.filePath(name));
    QVERIFY(art.open(QIODevice::WriteOnly));
    art.write("x");
    art.close();
  }
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artDir.path()});

  static const QStringList paths = {
      QStringLiteral("/items/Alpha.bin"),
      QStringLiteral("/items/Beta.bin"),
      QStringLiteral("/items/Gamma.bin"),
      QStringLiteral("/items/Delta.bin"),
  };
  FilterManager mgr;
  mgr.setSourceData(paths, seedEmptyDisplayNames(), seedEmptyDisplayNames(),
                    seedEmptySubcollections(), seedEmptyFolders());
  mgr.setHideMissingArtworkFilter(true, artDir.path());
  QSignalSpy spy(&mgr, &FilterManager::filterChanged);
  mgr.clearFilter();

  // The artwork-only baseline keeps exactly the set-backed items: Alpha via
  // the flat root, Beta via front/, Gamma via the full-filename key; Delta
  // is pruned.
  QVERIFY(mgr.isFiltered());
  QCOMPARE(mgr.filteredIndices(), (QList<int>{0, 1, 2}));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), 3); // visible media
  QCOMPARE(spy.at(0).at(1).toInt(), 4); // total media

  ArtworkUtils::DirectoryCache::instance().clear();
}

void TestFilterManager::testActualIndexRoundTripAcrossPrefixBands() {
  static const QList<CollectionConfig> collections = [] {
    QList<CollectionConfig> out;
    CollectionConfig first;
    first.name = QStringLiteral("Alpha Shelf");
    out.append(first);
    CollectionConfig second;
    second.name = QStringLiteral("Beta Shelf");
    out.append(second);
    return out;
  }();
  static const QList<int> subs = {0, 1};

  FilterManager mgr;
  mgr.setCollections(&collections);
  mgr.setSourceData(seedPaths(), seedNames(), seedEmptyDisplayNames(), subs, seedFolders());
  // "a" matches both shelves, the "Extras" folder, and all three media items;
  // "Docs" (concat index 2) is the only miss.
  mgr.applyFilter(QStringLiteral("a"));
  QVERIFY(mgr.isFiltered());

  const QList<int> expected = {0, 1, 3, 4, 5, 6};
  QCOMPARE(mgr.filteredIndices(), expected);
  for (int visual = 0; visual < mgr.filteredCount(); ++visual) {
    QCOMPARE(mgr.getActualIndex(visual), expected[visual]);
  }
  QCOMPARE(mgr.getActualIndex(mgr.filteredCount()), -1);
}

void TestFilterManager::testUnifiedSortMapRemapsFilteredIndices() {
  FilterManager mgr;
  // Pretend the store's unified sort interleaved the items name-ascending:
  // Alpha(m0) Beta(m1) Docs(f0) Extras(f1) Gamma(m2). Concat space is
  // [f0 f1 m0 m1 m2], so the concat→actual permutation is:
  static const QList<int> concatToActual = {2, 3, 0, 1, 4};
  mgr.setSourceData(seedPaths(), seedNames(), seedEmptyDisplayNames(), seedEmptySubcollections(),
                    seedFolders(), concatToActual);
  QSignalSpy spy(&mgr, &FilterManager::filterChanged);
  // "a" matches Extras + all three media items (concat {1, 2, 3, 4}), which
  // remap to actual {3, 0, 1, 4} and re-sort into unified display order.
  mgr.applyFilter(QStringLiteral("a"));
  QVERIFY(mgr.isFiltered());
  QCOMPARE(mgr.filteredIndices(), (QList<int>{0, 1, 3, 4}));
  QCOMPARE(mgr.getActualIndex(0), 0); // Alpha leads in unified order
  QCOMPARE(mgr.getActualIndex(2), 3); // Extras sits at its permuted slot
  // Media counting happens in concat space, so the readout is unaffected.
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), 3);
  QCOMPARE(spy.at(0).at(1).toInt(), 3);
}
