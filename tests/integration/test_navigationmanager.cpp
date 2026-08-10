#include "test_navigationmanager.h"

#include "applicationmanager.h"
#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include "emptystatewidget.h"
#include "errorutils.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "mainwindow.h"
// Kartend-xrj9r: this suite asserts only on in-memory coordinator state
// (never on persisted rows/INI), so it runs against the mocked fixture —
// no SQLite/QSettings setup per slot.
#include "mocks/mockedmainwindowfixture.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "selectionmanager.h"
#include "sessionmanager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTest>
#include <QWidget>

namespace {

CollectionConfig makeCollectionStub(const QString &name, int parentIndex = -1) {
  CollectionConfig cfg;
  cfg.name = name;
  cfg.parentCollectionIndex = parentIndex;
  cfg.isSubcollection = (parentIndex >= 0);
  // mediaDirectory left empty intentionally — onSubcollectionEntered's
  // navigation attempt should fail validateCollectionIndex (no media, no
  // descendants), which is exactly the unwind-on-failure path we want to
  // exercise.
  return cfg;
}

} // namespace

void TestNavigationManager::testOnCollectionSelectedClearsNavigationStack() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();
  QVERIFY(nav);

  // Pre-load the stack so we can observe the clear() in onCollectionSelected.
  // The contract is that selecting a top-level collection always discards
  // any pending hierarchy traversal — otherwise "Back" from the new view
  // would jump into a stale parent path.
  nav->stackManager()->push(7);
  nav->stackManager()->push(11);
  QVERIFY(!nav->stackManager()->isEmpty());

  // collectionIndex is out of range for the empty m_collections, so
  // showCollectionItems short-circuits via validateCollectionIndex. The
  // contract under test is the clear() that runs *before* the navigation
  // attempt — it must execute regardless of whether the target is valid.
  nav->onCollectionSelected(0);

  QVERIFY(nav->stackManager()->isEmpty());
  QCOMPARE(nav->stackManager()->depth(), 0);
}

void TestNavigationManager::testOnSubcollectionEnteredIgnoresOutOfRangeIndex() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  // Seed a single in-range entry so we can assert that the only rejected
  // call paths are the out-of-range ones.
  win->m_collections.append(makeCollectionStub(QStringLiteral("Solo")));
  win->m_currentCollectionIndex = 0;
  nav->stackManager()->push(42); // sentinel — must remain after the calls

  nav->onSubcollectionEntered(-1);
  nav->onSubcollectionEntered(99);

  // Out-of-range indices fail the outer guard before any push/pop logic
  // runs, so the sentinel must survive untouched. A regression that pushed
  // on every call (or popped unconditionally) would change this.
  QCOMPARE(nav->stackManager()->size(), 1);
  QCOMPARE(nav->stackManager()->top(), 42);
}

void TestNavigationManager::testOnSubcollectionEnteredUnwindsPushOnNavigationFailure() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Parent")));
  win->m_collections.append(makeCollectionStub(QStringLiteral("Child"), /*parentIndex=*/0));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();

  QVERIFY(nav->stackManager()->isEmpty());

  // The child has no media directory and no descendants, so
  // validateCollectionIndex → showCollectionItems returns false. The slot
  // must undo the speculative push it performed before showCollectionItems
  // was called; otherwise the back-stack would accumulate entries for
  // navigations that never actually happened.
  nav->onSubcollectionEntered(1);

  QVERIFY(nav->stackManager()->isEmpty());
}

void TestNavigationManager::testOnSubcollectionEnteredSkipsPushWhenNoCurrentCollection() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Solo")));
  win->m_currentCollectionIndex = -1; // pre-startup: nothing selected yet
  QVERIFY(nav->stackManager()->isEmpty());

  // Even though the target index is in range, the stack push is gated on
  // `*m_currentCollectionIndex >= 0`. With no current collection, there is
  // nothing meaningful to "go back to," so the stack must stay empty even
  // after the navigation attempt fails and triggers the defensive pop.
  nav->onSubcollectionEntered(0);

  QVERIFY(nav->stackManager()->isEmpty());
}

void TestNavigationManager::testOnMediaLibraryErrorRendersErrorWidget() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();
  QWidget *gridContainer = win->m_gridContainer;
  QVERIFY(gridContainer);

  // Pre-existing noItemsWidget children must be removed by the slot so the
  // new error widget is the unambiguous result of this call. Insert a
  // sentinel and confirm it gets scheduled for deletion.
  auto *stale = new QWidget(gridContainer);
  stale->setObjectName(QStringLiteral("noItemsWidget"));
  QPointer<QWidget> stalePtr(stale);

  const auto error = ErrorUtils::ErrorContext::error(
      ErrorUtils::ErrorCode::DatabaseQueryFailed, QStringLiteral("integration test forced error"),
      QStringLiteral("TestNavigationManager"));

  // test_main.cpp installs an ErrorPresentation override that no-ops the
  // modal, so onMediaLibraryError returns synchronously without spinning a
  // nested QDialog::exec() loop. We just assert on the noItemsWidget tree
  // the slot builds in m_gridContainer (Kartend-hlnl).
  nav->onMediaLibraryError(error);

  // The stale sentinel was removed via deleteLater(); pump the queue so the
  // assertion observes the post-deletion state.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY2(stalePtr.isNull(), "Pre-existing noItemsWidget should have been deleteLater()'d");

  // Exactly one fresh error widget should now exist as a child of the grid
  // container, carrying the error message in a QLabel descendant.
  const auto labels = gridContainer->findChildren<QWidget *>(QStringLiteral("noItemsWidget"));
  QCOMPARE(labels.size(), 1);
  auto *errorLabel = labels.first()->findChild<QLabel *>();
  QVERIFY(errorLabel);
  QCOMPARE(errorLabel->text(), QStringLiteral("integration test forced error"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Root/Home view routing
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationManager::testLoadRootViewEntersRootState() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("RootA")));
  win->m_collections.append(makeCollectionStub(QStringLiteral("RootB")));
  win->m_collections.append(makeCollectionStub(QStringLiteral("ChildOfA"), /*parentIndex=*/0));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();
  nav->stackManager()->push(0); // stale traversal that Home must discard

  QVERIFY(!nav->isInRootView());

  nav->loadRootView();

  // The synthetic Home view has no host collection: index parks at -1, the
  // root-view flag drives search routing, and any pending hierarchy
  // traversal is discarded (Back from Home must not pop into stale state).
  QVERIFY(nav->isInRootView());
  QCOMPARE(win->m_currentCollectionIndex, -1);
  QVERIFY(nav->stackManager()->isEmpty());

  // Default label when no custom homeViewLabel is configured.
  auto *titleLabel = win->m_itemsPage->findChild<QLabel *>(QStringLiteral("itemsTitleLabel"));
  QVERIFY(titleLabel);
  QCOMPARE(titleLabel->text(), QStringLiteral("Home"));
}

void TestNavigationManager::testLoadRootViewHonorsCustomHomeLabel() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Root")));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();
  win->m_generalSettings.startup.homeViewLabel = QStringLiteral("  My Hub  ");

  nav->loadRootView();

  // The configured label is trimmed before display; only a (trimmed-)empty
  // value falls back to the localized "Home".
  auto *titleLabel = win->m_itemsPage->findChild<QLabel *>(QStringLiteral("itemsTitleLabel"));
  QVERIFY(titleLabel);
  QCOMPARE(titleLabel->text(), QStringLiteral("My Hub"));
}

void TestNavigationManager::testLoadRootViewWithNoCollectionsShowsEmptyState() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  QVERIFY(win->m_collections.isEmpty());

  nav->loadRootView();

  QVERIFY(nav->isInRootView());
  QCOMPARE(win->m_currentCollectionIndex, -1);

  // With zero root collections there are no tiles to render, so the empty
  // state widget must explain the situation instead of leaving a blank grid.
  QVERIFY(win->m_loadingLabel);
  bool found = false;
  const auto labels = win->m_loadingLabel->findChildren<QLabel *>();
  for (const QLabel *label : labels) {
    if (label->text().contains(QStringLiteral("No collections yet"))) {
      found = true;
      break;
    }
  }
  QVERIFY2(found, "EmptyStateWidget should carry the 'No collections yet' message");
}

void TestNavigationManager::testGoBackIsNoOpInRootView() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Root")));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();

  nav->loadRootView();
  QVERIFY(nav->isInRootView());

  // Home is the top of the navigation hierarchy: Back must not kick the
  // user into a root collection (the pre-root-view fallback behavior).
  nav->goBackToCollections();

  QVERIFY(nav->isInRootView());
  QCOMPARE(win->m_currentCollectionIndex, -1);
}

void TestNavigationManager::testGoBackEscapesToHomeViewForRootCollection() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Root")));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();
  QVERIFY(nav->stackManager()->isEmpty());

  // Without the home-view opt-in, an empty-stack Back from a root collection
  // falls back to chooseFallbackCollectionIndex (stays put) — no root view.
  win->m_generalSettings.startup.useHomeView = false;
  nav->goBackToCollections();
  QVERIFY(!nav->isInRootView());
  QCOMPARE(win->m_currentCollectionIndex, 0);

  // With the opt-in, the same Back routes to the synthetic Home view rather
  // than re-entering the first root collection.
  win->m_generalSettings.startup.useHomeView = true;
  nav->goBackToCollections();
  QVERIFY(nav->isInRootView());
  QCOMPARE(win->m_currentCollectionIndex, -1);
}

void TestNavigationManager::testCollectionsModifiedClearsNavigationStack() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();
  ISettingsManager *settings = win->getApplicationManager()->getSettingsManager();
  QVERIFY(settings);

  nav->stackManager()->push(3);
  nav->stackManager()->push(5);
  QVERIFY(!nav->stackManager()->isEmpty());

  // The stack stores raw collection indices. A removal/reorder (announced by
  // the coarse collectionsModified signal) shifts them, so keeping the
  // history would silently retarget Back — the handler must drop it.
  emit settings->collectionsModified();

  QVERIFY(nav->stackManager()->isEmpty());
}

void TestNavigationManager::testGoBackFallsBackWhenPoppedTargetIsInvalid() {
  // Seeding ctor: this test pumps the event loop (QTRY below), so the window
  // must construct with a non-empty library — an empty one queues the modal
  // empty-collections prompt during construction, which would block the
  // pumped loop forever.
  KartendTest::MockedMainWindowFixture fixture({makeCollectionStub(QStringLiteral("Root"))});
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();
  win->m_generalSettings.startup.useHomeView = true;

  // A stack entry that no longer resolves to a valid collection (e.g. the
  // collection was removed while the user was browsing). The pop runs its
  // cleanup synchronously; the deferred showCollectionItems then fails
  // validation, and the lambda must reroute to handleNavigationFallback —
  // observable here as the home-view escape — instead of stopping silently
  // and stranding a blank items view.
  nav->stackManager()->push(99);
  nav->goBackToCollections();
  QVERIFY(nav->stackManager()->isEmpty());
  QVERIFY(!nav->isInRootView());

  // The view switch is deferred on a SHORT_DELAY timer; spin the event loop
  // until the fallback lands in the synthetic Home view.
  QTRY_VERIFY_WITH_TIMEOUT(nav->isInRootView(), 2000);
  QCOMPARE(win->m_currentCollectionIndex, -1);
}

void TestNavigationManager::testGoBackPopsNavigationStack() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("RootA")));
  win->m_collections.append(makeCollectionStub(QStringLiteral("RootB")));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();

  nav->stackManager()->push(1);

  // A non-empty stack takes the pop branch: the entry is consumed
  // synchronously (the actual view switch is deferred on a timer that never
  // fires in this test), and the home-view escape is not consulted.
  win->m_generalSettings.startup.useHomeView = true; // must NOT divert to Home
  nav->goBackToCollections();

  QVERIFY(nav->stackManager()->isEmpty());
  QVERIFY(!nav->isInRootView());
}

// ─────────────────────────────────────────────────────────────────────────────
// Virtual-folder navigation
// ─────────────────────────────────────────────────────────────────────────────

void TestNavigationManager::testVirtualFolderNavigationUpdatesSubfolderPath() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Root")));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();

  const auto subfolder = [&]() -> QString {
    return win->m_collections[0].folderBrowsing.currentSubfolder;
  };
  QCOMPARE(subfolder(), QString());

  // Descend two levels. Each entry updates the persisted subfolder scope on
  // the collection config (the reload itself is debounced and async).
  nav->onVirtualFolderEntered(QStringLiteral("Videos"));
  QCOMPARE(subfolder(), QStringLiteral("Videos"));
  nav->onVirtualFolderEntered(QStringLiteral("Videos/2024"));
  QCOMPARE(subfolder(), QStringLiteral("Videos/2024"));

  // Back walks up exactly one level per call...
  nav->goBackFromVirtualFolder();
  QCOMPARE(subfolder(), QStringLiteral("Videos"));
  nav->goBackFromVirtualFolder();
  QCOMPARE(subfolder(), QString());

  // ...and is a no-op at the collection root.
  nav->goBackFromVirtualFolder();
  QCOMPARE(subfolder(), QString());
}

void TestNavigationManager::testVirtualFolderEnterIgnoredWithoutCurrentCollection() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Root")));
  win->m_currentCollectionIndex = -1; // pre-startup / Home view: no host scope

  // With no current collection there is no config to scope the subfolder
  // to — the call must bail before mutating any collection entry.
  nav->onVirtualFolderEntered(QStringLiteral("Videos"));

  QCOMPARE(win->m_collections[0].folderBrowsing.currentSubfolder, QString());
}

void TestNavigationManager::testBreadcrumbLinksDriveSubfolderNavigation() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getApplicationManager()->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Root")));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();
  win->m_collections[0].folderBrowsing.currentSubfolder = QStringLiteral("Videos/2024/Q1");

  // A "subfolder:" link jumps straight to that level (not one-at-a-time).
  nav->onBreadcrumbLinkClicked(QStringLiteral("subfolder:Videos"));
  QCOMPARE(win->m_collections[0].folderBrowsing.currentSubfolder, QStringLiteral("Videos"));

  // A malformed link parses as Kind::Invalid and must change nothing.
  nav->onBreadcrumbLinkClicked(QStringLiteral("bogus-link"));
  QCOMPARE(win->m_collections[0].folderBrowsing.currentSubfolder, QStringLiteral("Videos"));

  // A "root:" link clears the subfolder scope entirely.
  nav->onBreadcrumbLinkClicked(QStringLiteral("root:"));
  QCOMPARE(win->m_collections[0].folderBrowsing.currentSubfolder, QString());
}

// Kartend-ic4h6: the boundary persist (safeReloadCollection, navigation,
// shutdown — all funnel through persistCurrentSelection) only wrote the
// SETTINGS store, while SelectionRestoreCoordinator::getSelectionRestoreIndex
// restores from the SESSION store. A selection moved through the bare
// ISelectionCore setter — the wheel-selection step does exactly this, and
// persists nothing per-move — therefore never reached the value the restore
// reads, so any background reload (e.g. Steam enrichment finishing on another
// collection) "restored" a stale index and visibly reverted the user's
// selection. The boundary persist must land the LIVE selection in the session
// store, under the same key the restore reads.
void TestNavigationManager::testBoundaryPersistWritesSessionStoreForRestore() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *appMgr = win->getApplicationManager();
  NavigationManager *nav = appMgr->getNavigationManager();
  InteractionManager *im = appMgr->getInteractionManager();
  auto *session = appMgr->getSessionManager();
  QVERIFY(nav);
  QVERIFY(im);
  QVERIFY(session);
  QVERIFY(im->selectionManager());

  win->m_collections.append(makeCollectionStub(QStringLiteral("Films")));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();

  // Move the selection through the bare setter — the wheel path's exact
  // write, which sets no flags and persists nothing on its own.
  im->selectionManager()->setSelectedIndex(42);

  // prepareForShutdown is the public wrapper over persistCurrentSelection,
  // the same boundary persist every reload and navigation runs.
  nav->prepareForShutdown();

  const QString key =
      CollectionUtils::selectionSessionKeyFor(win->m_collections[0], win->m_collections);
  QCOMPARE(session->getLastSelectedIndex(key), 42);
}
