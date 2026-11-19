#include "test_navigationmanager.h"

#include "applicationcontext.h"
#include "applicationmanager.h"
#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include "collection/typehelpers.h"
#include "emptystatewidget.h"
#include "errorutils.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "mainwindow.h"
// Kartend-rxaru: the after-count start-up restore needs the real count reply,
// so that one slot runs on the real on-disk SQLite fixture instead.
#include "mainwindowfixture.h"
// Kartend-xrj9r: the rest of this suite asserts only on in-memory coordinator
// state (never on persisted rows/INI), so it runs against the mocked fixture —
// no SQLite/QSettings setup per slot.
#include "mocks/mockedmainwindowfixture.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
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

// Kartend-1fhgz: the rescan's cacheInvalidated wait was one-shot for the
// FIRST invalidation of anything — but the signal is shared, and since
// Kartend-xkdxn a background launcher enrichment routinely invalidates
// OTHER collections' caches. Firing on one of those ran the reload before
// this rescan's cache was actually cleared, silently serving stale rows.
// The handler must hold until ITS uuid echoes back.
void TestNavigationManager::testForceRescanWaitsForItsOwnCacheInvalidation() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *appMgr = win->getApplicationManager();
  NavigationManager *nav = appMgr->getNavigationManager();
  auto *db = appMgr->getDatabaseManager();
  QVERIFY(nav);
  QVERIFY(db);

  win->m_collections.append(makeCollectionStub(QStringLiteral("Films")));
  win->m_currentCollectionIndex = 0;
  win->rebuildHierarchyCache();

  // The mock's invalidateCollectionCache is a no-op, so nothing echoes on
  // its own — the test controls exactly which invalidation arrives when.
  nav->forceRescanCollection(0);
  QCOMPARE(nav->pendingRescanCollectionIndexForTesting(), 0);

  // A DIFFERENT collection's invalidation lands first (the Kartend-xkdxn
  // background-enrichment case). The pending rescan must survive it.
  emit db->cacheInvalidated(QStringLiteral("not-our-uuid"));
  QCOMPARE(nav->pendingRescanCollectionIndexForTesting(), 0);

  // Our own uuid — derived exactly as forceRescanCollection derives it —
  // consumes the wait and hands off to the reload.
  const CollectionConfig &cfg = win->m_collections[0];
  const QString uuid = CollectionUtils::computeCollectionUuid(
      cfg.name, SettingsUtils::expandConfigVariables(cfg.mediaDirectory, cfg.name));
  emit db->cacheInvalidated(uuid);
  QCOMPARE(nav->pendingRescanCollectionIndexForTesting(), -1);
}

// Kartend-rxaru: closing with the fifth of six items selected saves index 4,
// yet the next launch selected the first item. The start-up load takes the
// cached-viewport fast path, which scheduled the restore for the cached
// viewport's FIRST VISIBLE index (0 when all six tiles fit) instead of the
// remembered selection. Drive the real start-up entry (showCollectionItems
// with no current collection yet) and pin the index handed to the restore.
void TestNavigationManager::testStartupLoadSchedulesSavedSelectionRestore() {
  constexpr int kItemCount = 6;
  constexpr int kSavedIndex = 4;

  QTemporaryDir mediaDir;
  QVERIFY(mediaDir.isValid());
  QStringList filePaths;
  QHash<QString, QString> fileNames;
  for (int i = 0; i < kItemCount; ++i) {
    const QString name = QStringLiteral("item%1.rom").arg(i);
    const QString path = mediaDir.filePath(name);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    filePaths.append(path);
    fileNames[path] = name;
  }

  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *appMgr = win->getApplicationManager();
  NavigationManager *nav = appMgr->getNavigationManager();
  auto *session = appMgr->getSessionManager();
  QVERIFY(nav);
  QVERIFY(session);
  QVERIFY(appMgr->getScrollManager());

  CollectionConfig cfg = makeCollectionStub(QStringLiteral("Library"));
  cfg.mediaDirectory = mediaDir.path();
  win->m_collections.append(cfg);
  win->m_currentCollectionIndex = -1;
  win->m_generalSettings.input.rememberSelection = true;
  win->rebuildHierarchyCache();

  // The session as the previous run left it: the fifth item remembered, and a
  // cached viewport whose first visible tile is item 0.
  const QString key =
      CollectionUtils::hierarchicalNameFor(win->m_collections[0], win->m_collections);
  session->setLastSelected(key, kSavedIndex, QStringLiteral("item4"));
  session->setCachedViewport(key, 0, kItemCount, filePaths, fileNames, {});
  QCOMPARE(session->getLastSelectedIndex(key), kSavedIndex);

  QVERIFY(nav->showCollectionItems(0));

  // The fast path really ran (the mock DB never answers the count query).
  QCOMPARE(appMgr->getScrollManager()->getTotalItems(), kItemCount);
  QCOMPARE(nav->startupSelectionRestoreIndexForTesting(), kSavedIndex);
}

// Kartend-rxaru follow-up: the handoff test above stops at the value given to
// the restore, because its mock DB never answers the background count query.
// Here the window's own start-up timer drives the load against the real
// sandboxed SQLite database: the cached viewport renders first, the worker
// scans and answers the count, and the remembered item must be the selection
// once that reply has been applied and the restore has finished.
void TestNavigationManager::testStartupSelectionRestoredAfterCountReply() {
  constexpr int kItemCount = 12;
  constexpr int kSavedIndex = 9;
  // The cached viewport covers items 2..6: its first visible tile is neither
  // item 0 nor the saved item, and the saved item lies outside its range, so
  // only a range load from the database can supply the saved item's path.
  constexpr int kViewportStart = 2;
  constexpr int kViewportSize = 5;

  QTemporaryDir mediaDir;
  QVERIFY(mediaDir.isValid());
  QStringList filePaths;
  for (int i = 0; i < kItemCount; ++i) {
    // Zero-padded names, so the name sort matches the creation order.
    const QString path =
        QDir(mediaDir.path())
            .filePath(QStringLiteral("clip%1.mp4").arg(i, 2, 10, QLatin1Char('0')));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("fake video bytes");
    file.close();
    filePaths.append(path);
  }

  CollectionConfig cfg;
  cfg.name = QStringLiteral("Clips");
  cfg.type = QStringLiteral("video");
  cfg.mediaDirectory = mediaDir.path();
  cfg.extensions = QStringList{QStringLiteral("mp4")};

  // The collection must be in the sandbox INI before MainWindow loads it: an
  // empty-collections start-up queues a modal prompt instead of the load.
  KartendTest::MainWindowFixture fixture([&cfg]() {
    ApplicationContext seedCtx; // saveCollections never reads ctx
    SettingsManager seedSettings(&seedCtx);
    if (seedSettings.saveCollections({cfg}).isError()) {
      qFatal("failed to seed the sandbox collection INI");
    }
  });
  MainWindow *win = fixture.window();
  auto *appMgr = win->getApplicationManager();
  NavigationManager *nav = appMgr->getNavigationManager();
  InteractionManager *im = appMgr->getInteractionManager();
  ScrollManager *scroll = appMgr->getScrollManager();
  auto *session = appMgr->getSessionManager();
  IDatabaseManager *db = appMgr->getDatabaseManager();
  QVERIFY(nav);
  QVERIFY(im);
  QVERIFY(im->selectionManager());
  QVERIFY(scroll);
  QVERIFY(session);
  QVERIFY(db);
  QCOMPARE(win->m_collections.size(), 1);

  // Start-up has not run yet: it is queued on the event loop, which nothing
  // has pumped since construction, and no collection is current.
  QCOMPARE(win->m_currentCollectionIndex, -1);
  QCOMPARE(nav->startupSelectionRestoreIndexForTesting(), -1);

  // The session as the previous run left it: item 9 remembered, and a cached
  // viewport of items 2..6.
  win->m_generalSettings.input.rememberSelection = true;
  const QString key =
      CollectionUtils::hierarchicalNameFor(win->m_collections[0], win->m_collections);
  QStringList viewportPaths;
  QHash<QString, QString> viewportNames;
  for (int i = kViewportStart; i < kViewportStart + kViewportSize; ++i) {
    viewportPaths.append(filePaths[i]);
    viewportNames[filePaths[i]] = QFileInfo(filePaths[i]).completeBaseName();
  }
  session->setLastSelected(key, kSavedIndex, QStringLiteral("clip09"));
  session->setCachedViewport(key, kViewportStart, kItemCount, viewportPaths, viewportNames, {});
  QCOMPARE(session->getLastSelectedIndex(key), kSavedIndex);

  QSignalSpy countReplies(db, &IDatabaseManager::itemCountLoadedWithToken);
  QVERIFY(countReplies.isValid());

  // Let the start-up timer fire: showCollectionItems with no current
  // collection, the cached-viewport fast path, then the background count.
  QTRY_COMPARE_WITH_TIMEOUT(nav->startupSelectionRestoreIndexForTesting(), kSavedIndex, 10000);
  QCOMPARE(win->m_currentCollectionIndex, 0);

  // The worker's count reply for this (only) collection: the full item count.
  const auto sawFullCount = [&countReplies]() {
    for (const QList<QVariant> &reply : countReplies) {
      if (reply.at(0).toInt() == kItemCount) {
        return true;
      }
    }
    return false;
  };
  QTRY_VERIFY_WITH_TIMEOUT(sawFullCount(), 30000);

  // Settle: a selection exists, the restore has finished, and the saved item's
  // row has been loaded from the database (it is outside the cached viewport).
  SelectionManager *sel = im->selectionManager();
  QTRY_VERIFY_WITH_TIMEOUT(sel->currentSelectedIndex() >= 0 && !sel->isRestoringSelection() &&
                               scroll->filePathForVisualIndex(kSavedIndex) ==
                                   filePaths[kSavedIndex],
                           30000);

  QCOMPARE(scroll->getTotalItems(), kItemCount);
  QCOMPARE(sel->currentSelectedIndex(), kSavedIndex);
  QCOMPARE(scroll->filePathForVisualIndex(sel->currentSelectedIndex()), filePaths[kSavedIndex]);
  QVERIFY(sel->currentSelectedIndex() != 0);
  QVERIFY(sel->currentSelectedIndex() != kViewportStart);
}
