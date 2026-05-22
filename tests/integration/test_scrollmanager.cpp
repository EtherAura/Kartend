#include "test_scrollmanager.h"

#include "applicationmanager.h"
#include "mainwindow.h"
#include "mocks/mockdatabasemanager.h"
#include "mocks/mockedmainwindowfixture.h"
#include "scrollmanager.h"

#include <QStringList>
#include <QTest>

void TestScrollManager::getTotalItems_isZeroOnFreshFixture() {
  KartendTest::MockedMainWindowFixture fixture;
  // Sanity check: confirm the factory hooks actually substituted the mock.
  // Without this, a regression that broke the factory plumbing would leave
  // tests silently exercising the real SQLite-backed DatabaseManager.
  QVERIFY(qobject_cast<KartendTest::MockDatabaseManager *>(fixture.window()->getApplicationManager()->getDatabaseManager()));
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
  QVERIFY2(sm->getCurrentGridWidth() > 0,
           "getCurrentGridWidth must return a positive default");
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
