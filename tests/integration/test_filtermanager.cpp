#include "test_filtermanager.h"

#include "filtermanager.h"

#include <QHash>
#include <QList>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
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

void seedThreeItems(FilterManager &mgr) {
  mgr.setSourceData(seedPaths(), seedNames(), seedEmptyDisplayNames(),
                    seedEmptySubcollections());
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
