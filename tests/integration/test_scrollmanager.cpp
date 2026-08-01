#include "test_scrollmanager.h"

#include "applicationmanager.h"
#include "collection/collectioncontext.h"
#include "mainwindow.h"
#include "mocks/mockdatabasemanager.h"
#include "mocks/mockedmainwindowfixture.h"
#include "scrollmanager.h"

#include <QSignalSpy>
#include <QStringList>
#include <QTest>

void TestScrollManager::getTotalItems_isZeroOnFreshFixture() {
  KartendTest::MockedMainWindowFixture fixture;
  // Sanity check: confirm the factory hooks actually substituted the mock.
  // Without this, a regression that broke the factory plumbing would leave
  // tests silently exercising the real SQLite-backed DatabaseManager.
  QVERIFY(qobject_cast<KartendTest::MockDatabaseManager *>(
      fixture.window()->getApplicationManager()->getDatabaseManager()));
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);
  // No collections have been loaded into the fixture's MainWindow, so
  // setupVirtualScrolling has never been called and the data model is
  // empty. getTotalItems must reflect that.
  QCOMPARE(sm->getTotalItems(), 0);
}

void TestScrollManager::getCurrentGridWidth_returnsPositive() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);
  // Even without a collection loaded, getCurrentGridWidth falls back to
  // the default constant. Asserting >0 catches a regression where the
  // fallback path returns 0 (which would break layout math downstream).
  QVERIFY2(sm->getCurrentGridWidth() > 0, "getCurrentGridWidth must return a positive default");
}

void TestScrollManager::sidebarShrinkingActive_roundTripsThroughSetter() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);

  const bool initial = sm->sidebarShrinkingActive();
  sm->setSidebarShrinkingActive(!initial);
  QCOMPARE(sm->sidebarShrinkingActive(), !initial);
  sm->setSidebarShrinkingActive(initial);
  QCOMPARE(sm->sidebarShrinkingActive(), initial);
}

void TestScrollManager::hasPendingSelectionRestoreByPath_isFalseInitially() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);
  QVERIFY(!sm->hasPendingSelectionRestoreByPath());
}

void TestScrollManager::hasPendingSelectionRestoreByPath_flipsAfterSet() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);

  sm->setPendingSelectionRestoreByPath(QStringLiteral("/some/path/file.bin"));
  QVERIFY(sm->hasPendingSelectionRestoreByPath());
  // Setting empty path clears the pending restore — guards against the
  // sort-change handler leaving stale restore state after the user
  // navigates elsewhere.
  sm->setPendingSelectionRestoreByPath(QString());
  QVERIFY(!sm->hasPendingSelectionRestoreByPath());
}

void TestScrollManager::hasPreSearchState_isFalseBeforeSave() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);
  // savePreSearchState must be called explicitly before hasPreSearchState
  // returns true; the fresh fixture has never entered search mode.
  QVERIFY(!sm->hasPreSearchState());
}

void TestScrollManager::filterChange_clearOnEmptyStateIsSafe() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);
  // clearFilter() on a manager that never applied a filter must be a
  // safe no-op. Without this guarantee, search-bar focus/blur cycles in
  // the empty-state widget could crash on first launch.
  sm->clearFilter();
  // Same for applyFilter with empty text.
  sm->applyFilter(QString());
  QCOMPARE(sm->getTotalItems(), 0);
}

void TestScrollManager::visualIndexForPathRestore_mapsThroughActiveFilter() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);

  // Three preloaded media items, no subcollections / virtual folders — the
  // store's actual-index space is [Alpha 0, Beta 1, Gamma 2].
  CollectionContext context;
  context.filePaths = QStringList{QStringLiteral("/items/Alpha.bin"),
                                  QStringLiteral("/items/Beta.bin"),
                                  QStringLiteral("/items/Gamma.bin")};
  context.fileNames.insert(context.filePaths.at(0), QStringLiteral("Alpha"));
  context.fileNames.insert(context.filePaths.at(1), QStringLiteral("Beta"));
  context.fileNames.insert(context.filePaths.at(2), QStringLiteral("Gamma"));
  sm->setupVirtualScrolling(context.filePaths.size(), context);
  QCOMPARE(sm->getTotalItems(), 3);

  // In-memory filter keeps only Gamma: filtered visual slot 0 → actual 2.
  sm->applyFilter(QStringLiteral("gamma"));
  QCOMPARE(sm->getTotalItems(), 1);

  QSignalSpy spy(sm, &ScrollManager::selectItemByIndex);

  // The DB answers with Gamma's media-only position (2). The restore must
  // emit the FILTERED visual index 0 — emitting the store-space 2 would
  // select a tile that does not exist in the filtered view.
  sm->setPendingSelectionRestoreByPath(context.filePaths.at(2));
  sm->onVisualIndexForPathLoaded(2, context.filePaths.at(2));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), 0);

  // Beta is filtered out: the restore must be skipped, not land on a wrong
  // tile.
  sm->setPendingSelectionRestoreByPath(context.filePaths.at(1));
  sm->onVisualIndexForPathLoaded(1, context.filePaths.at(1));
  QCOMPARE(spy.count(), 1);
}

namespace {

/// Builds a preloaded context of @p count media items named "Item NNN"
/// (no subcollections / virtual folders), mirroring the shape the DB load
/// path hands to setupVirtualScrolling for an already-fetched collection.
CollectionContext makePopulatedContext(int count) {
  CollectionContext context;
  for (int i = 0; i < count; ++i) {
    const QString path = QStringLiteral("/items/Item %1.bin").arg(i, 3, 10, QLatin1Char('0'));
    context.filePaths.append(path);
    context.fileNames.insert(path, QStringLiteral("Item %1").arg(i, 3, 10, QLatin1Char('0')));
  }
  return context;
}

} // namespace

void TestScrollManager::setupVirtualScrolling_populatedItemsProduceScrollableLayout() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);

  constexpr int kItemCount = 120;
  const CollectionContext context = makePopulatedContext(kItemCount);
  sm->setupVirtualScrolling(kItemCount, context);
  QCOMPARE(sm->getTotalItems(), kItemCount);

  // The computed geometry must be internally consistent: rows follow from
  // the configured items-per-row, and the container height covers them.
  const GridMetrics &metrics = sm->getMetrics();
  QCOMPARE(metrics.itemsPerRow, sm->getCurrentGridWidth());
  QVERIFY(metrics.itemsPerRow > 0);
  const int expectedRows = (kItemCount + metrics.itemsPerRow - 1) / metrics.itemsPerRow;
  QCOMPARE(metrics.totalRows, expectedRows);
  QVERIFY(metrics.itemHeight > 0);
  QVERIFY2(metrics.totalHeight >= metrics.totalRows * metrics.itemHeight,
           "container height must cover every computed row");

  // 30 default-height rows tower over any viewport this fixture can have:
  // the scrollbar prediction (which drives MainWindow's scrollbar-policy
  // routing) must say a vertical scrollbar is coming.
  QVERIFY(sm->willNeedVerticalScrollbar());
}

void TestScrollManager::applyFilter_shrinksLayoutExtentAndClearRestoresIt() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);

  constexpr int kItemCount = 120;
  sm->setupVirtualScrolling(kItemCount, makePopulatedContext(kItemCount));
  const int unfilteredHeight = sm->getMetrics().totalHeight;
  const int unfilteredRows = sm->getMetrics().totalRows;
  QVERIFY(unfilteredRows > 1);

  // One match ("Item 007") → a single row whose container is strictly
  // shorter than the unfiltered grid. applyFilter recalculates the metrics
  // in place, so the layout must shrink immediately, not on the next
  // collection switch.
  sm->applyFilter(QStringLiteral("Item 007"));
  QCOMPARE(sm->getTotalItems(), 1);
  QCOMPARE(sm->getMetrics().totalRows, 1);
  QVERIFY2(sm->getMetrics().totalHeight < unfilteredHeight,
           "filtered container must be shorter than the unfiltered grid");

  // Clearing the filter restores the full extent (same row count and
  // container height as before the search).
  sm->clearFilter();
  QCOMPARE(sm->getTotalItems(), kItemCount);
  QCOMPARE(sm->getMetrics().totalRows, unfilteredRows);
  QCOMPARE(sm->getMetrics().totalHeight, unfilteredHeight);
}

void TestScrollManager::getFilePaths_isEmptyOnFreshFixture() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);
  QVERIFY(sm->getFilePaths().isEmpty());
}

void TestScrollManager::willNeedVerticalScrollbar_returnsBoolWithoutCrash() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);
  // With zero items the scrollbar shouldn't be needed; this also exercises
  // the metrics path that callers (MainWindow resize / scrollbar policy
  // routing) hit on every layout event.
  const bool needed = sm->willNeedVerticalScrollbar();
  QVERIFY(!needed);
}

void TestScrollManager::gridLayoutChanged_appliesToActiveCollection() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);

  const int baseline = sm->getCurrentGridWidth();
  QVERIFY(baseline > 0);
  // Emit the per-cluster signal for the active collection (currentIndex
  // defaults to -1 in the empty fixture; the slot's guard reads
  // m_context.currentIndex). Use -1 so the slot treats the diff as
  // matching the (uninitialized) active context.
  GridLayoutPreferences newGrid;
  newGrid.gridWidth = baseline + 3;
  sm->onGridLayoutChanged(-1, newGrid);
  // updateGridWidth ran → calculateMetrics picks up the new value via
  // m_context.config.gridLayout.gridWidth, which getCurrentGridWidth
  // reads. So the gating worked: the alternate-save path applied.
  QCOMPARE(sm->getCurrentGridWidth(), baseline + 3);
}

void TestScrollManager::gridLayoutChanged_ignoresNonActiveCollection() {
  KartendTest::MockedMainWindowFixture fixture;
  ScrollManager *sm = fixture.window()->getApplicationManager()->getScrollManager();
  QVERIFY(sm);

  const int baseline = sm->getCurrentGridWidth();
  GridLayoutPreferences otherGrid;
  otherGrid.gridWidth = baseline + 5;
  // Signal targets collectionIndex=42 — definitely not the active one
  // (currentIndex == -1 in the empty fixture). The guard must drop the
  // diff so the active layout stays put.
  sm->onGridLayoutChanged(42, otherGrid);
  QCOMPARE(sm->getCurrentGridWidth(), baseline);
}
