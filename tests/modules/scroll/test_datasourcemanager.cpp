// DataSourceCoordinator — the ownership bundle for ScrollManager's four
// data-source sub-managers (FilterManager, ScrollDataStore,
// PreSearchStateCache, SearchLoadingOverlay).
//
// The coordinator's value is wiring, not logic: it must construct and own the
// sub-managers (unique_ptr + Qt parent as the runtime lifetime guard),
// re-emit FilterManager::filterChanged outward, and route the thin facade
// helpers (overlay show/hide, pre-search query, filtered-index mapping) to
// the right sub-object. Each case drives the coordinator's public surface and
// observes real sub-manager behavior — no mocks.

#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include "datasourcemanager.h"
#include "filtermanager.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "searchloadingoverlay.h"

class TestDataSourceCoordinator : public QObject {
  Q_OBJECT

private slots:
  // Ownership / wiring
  void construction_createsAllFourSubManagers();
  void subManagers_areParentedToCoordinator();

  // filterChanged re-emission
  void filterChanged_reEmittedWithFilterManagerCounts();

  // Facade helpers
  void getFilteredIndex_passesThroughWhenUnfiltered();
  void getFilteredIndex_mapsVisualToActualWhenFiltered();
  void hasPreSearchState_isFalseOnFreshCoordinator();
  void searchOverlay_showWithoutParentIsSafeNoOp();
  void searchOverlay_showsAndHidesUnderParentWidget();
};

namespace {

/// Two preloaded media items (no subcollections / virtual folders), so the
/// FilterManager's actual-index space is [Alpha 0, Beta 1].
///
/// setSourceData stores POINTERS to the caller's containers (production
/// callers pass long-lived ScrollDataStore members), so this seed must
/// outlive every FilterManager call — hold it in the test-function scope.
struct SeededSources {
  QStringList filePaths{QStringLiteral("/media/Alpha.bin"), QStringLiteral("/media/Beta.bin")};
  QHash<QString, QString> fileNames;
  QHash<QString, QString> displayNames;
  QList<int> subcollections;
  QStringList virtualFolders;

  SeededSources() {
    fileNames.insert(filePaths.at(0), QStringLiteral("Alpha"));
    fileNames.insert(filePaths.at(1), QStringLiteral("Beta"));
  }

  void install(FilterManager *fm) {
    fm->setSourceData(filePaths, fileNames, displayNames, subcollections, virtualFolders);
  }
};

} // namespace

void TestDataSourceCoordinator::construction_createsAllFourSubManagers() {
  DataSourceCoordinator coordinator;
  QVERIFY(coordinator.filterManager());
  QVERIFY(coordinator.dataManager());
  QVERIFY(coordinator.preSearchStateManager());
  QVERIFY(coordinator.searchLoadingOverlay());
}

void TestDataSourceCoordinator::subManagers_areParentedToCoordinator() {
  // parent() is a runtime lifetime guard in this codebase (see
  // docs/dev/architecture.md), so each sub-manager must be parented to the
  // coordinator even though unique_ptr owns the deletes.
  DataSourceCoordinator coordinator;
  QCOMPARE(coordinator.filterManager()->parent(), &coordinator);
  QCOMPARE(coordinator.dataManager()->parent(), &coordinator);
  QCOMPARE(coordinator.preSearchStateManager()->parent(), &coordinator);
  QCOMPARE(coordinator.searchLoadingOverlay()->parent(), &coordinator);
}

void TestDataSourceCoordinator::filterChanged_reEmittedWithFilterManagerCounts() {
  DataSourceCoordinator coordinator;
  SeededSources sources;
  sources.install(coordinator.filterManager());

  QSignalSpy spy(&coordinator, &DataSourceCoordinator::filterChanged);
  coordinator.filterManager()->applyFilter(QStringLiteral("beta"));

  // The coordinator forwards the (visibleItems, totalOriginal) pair
  // untouched: one media match out of two source items.
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), 1);
  QCOMPARE(spy.at(0).at(1).toInt(), 2);
}

void TestDataSourceCoordinator::getFilteredIndex_passesThroughWhenUnfiltered() {
  DataSourceCoordinator coordinator;
  // No filter active: visual space == actual space, any index passes
  // through unchanged (including out-of-range values — mapping is the
  // filter's job, bounds are the caller's).
  QCOMPARE(coordinator.getFilteredIndex(0), 0);
  QCOMPARE(coordinator.getFilteredIndex(7), 7);
}

void TestDataSourceCoordinator::getFilteredIndex_mapsVisualToActualWhenFiltered() {
  DataSourceCoordinator coordinator;
  SeededSources sources;
  sources.install(coordinator.filterManager());

  // Filter keeps only Beta (actual index 1). The filtered view's visual
  // slot 0 must map back to actual index 1 — returning 0 would point the
  // caller at Alpha's data.
  coordinator.filterManager()->applyFilter(QStringLiteral("beta"));
  QVERIFY(coordinator.filterManager()->isFiltered());
  QCOMPARE(coordinator.getFilteredIndex(0), 1);
}

void TestDataSourceCoordinator::hasPreSearchState_isFalseOnFreshCoordinator() {
  DataSourceCoordinator coordinator;
  QVERIFY(!coordinator.hasPreSearchState());
}

void TestDataSourceCoordinator::searchOverlay_showWithoutParentIsSafeNoOp() {
  DataSourceCoordinator coordinator;
  // No parent widget wired: the overlay cannot build its child widget, so
  // show() must be a safe no-op (search kicked off before the scroll area
  // exists during startup).
  coordinator.showSearchLoadingOverlay();
  QVERIFY(!coordinator.searchLoadingOverlay()->isVisible());
  coordinator.hideSearchLoadingOverlay(); // no crash on hide-without-show
}

void TestDataSourceCoordinator::searchOverlay_showsAndHidesUnderParentWidget() {
  DataSourceCoordinator coordinator;
  QWidget parent;
  parent.resize(200, 200);
  parent.show();

  coordinator.setSearchOverlayParent(&parent);
  coordinator.showSearchLoadingOverlay();
  QVERIFY(coordinator.searchLoadingOverlay()->isVisible());

  // hide() fades out asynchronously; the overlay must end hidden once the
  // animation completes.
  coordinator.hideSearchLoadingOverlay();
  QTRY_VERIFY(!coordinator.searchLoadingOverlay()->isVisible());
}

QTEST_MAIN(TestDataSourceCoordinator)
#include "test_datasourcemanager.moc"
