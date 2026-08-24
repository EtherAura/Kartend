#include "test_eventmanager_detailspane.h"
#include "applicationmanager.h"

#include "detailspanemanager.h"
#include "eventmanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
// Kartend-xrj9r: this suite asserts only on in-memory coordinator state
// (never on persisted rows/INI), so it runs against the mocked fixture —
// no SQLite/QSettings setup per slot.
#include "mocks/mockdatabasemanager.h"
#include "mocks/mockedmainwindowfixture.h"

#include "detailspane.h"
#include "uiconstants/detailspaneconstants.h"
#include <QFile>
#include <QLabel>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <QWidget>

void TestEventManagerDetailsPane::eventManager_isWiredOnInteractionManager() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  QVERIFY(win);
  InteractionManager *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  EventManager *em = im->eventManager();
  QVERIFY2(em, "InteractionManager must own a constructed EventManager after MainWindow setup");
}

void TestEventManagerDetailsPane::detailsPaneManager_toggleSidebar_flipsVisibilityAndEmits() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  DetailsPaneManager *mgr = win->getApplicationManager()->getDetailsPaneManager();
  QVERIFY(mgr);

  const bool initial = mgr->isSidebarVisible();
  QSignalSpy spy(mgr, &DetailsPaneManager::sidebarVisibilityChanged);
  mgr->toggleSidebar();
  QCOMPARE(mgr->isSidebarVisible(), !initial);
  // The toggle cascades through updateSidebarLayout which can also emit, so
  // we don't pin spy.count(); the contract is that at least one emission
  // reflects the new (post-toggle) effective visibility.
  QVERIFY2(spy.count() >= 1, "toggleSidebar must emit sidebarVisibilityChanged");
  QCOMPARE(spy.last().at(0).toBool(), !initial);
}

void TestEventManagerDetailsPane::
    detailsPaneManager_toggleSidebar_doubleToggleRestoresInitialState() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  DetailsPaneManager *mgr = win->getApplicationManager()->getDetailsPaneManager();
  QVERIFY(mgr);

  const bool initial = mgr->isSidebarVisible();
  mgr->toggleSidebar();
  mgr->toggleSidebar();
  QCOMPARE(mgr->isSidebarVisible(), initial);
}

void TestEventManagerDetailsPane::detailsPaneManager_subcollectionSelection_survivesTabRoundTrip() {
  QList<CollectionConfig> seed;
  CollectionConfig games;
  games.name = QStringLiteral("Games");
  CollectionConfig sony;
  sony.name = QStringLiteral("Sony");
  sony.parentCollectionIndex = 0;
  sony.isSubcollection = true;
  seed = {games, sony};
  KartendTest::MockedMainWindowFixture fixture(seed);
  MainWindow *win = fixture.window();
  DetailsPaneManager *mgr = win->getApplicationManager()->getDetailsPaneManager();
  QVERIFY(mgr);
  auto *pane = win->findChild<DetailsPane *>();
  QVERIFY(pane);
  auto *nameLabel = pane->findChild<QLabel *>(QStringLiteral("itemNameValue"));
  QVERIFY(nameLabel);
  // Enter the parent collection so the tab-switch handler's
  // current-collection guard passes — in the app this happens on every
  // collection switch; without it the handler early-returns and the stale
  // re-push under test never fires.
  mgr->applySidebarStateForCollection(0, /*reloadBackground=*/false);

  // An item is displayed first — this is what seeds the stale context.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("game.kartlink"));
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral("x"));
  }
  mgr->updateSidebarMetadata(path, QStringLiteral("A Game"));
  QTRY_COMPARE_WITH_TIMEOUT(nameLabel->text(), QStringLiteral("A Game"), 2000);

  // The subcollection tile takes the selection.
  mgr->showSubcollectionSummary(1);
  pane->setActiveTab(DetailsPaneTab::Item);
  QCOMPARE(nameLabel->text(), QStringLiteral("Sony"));

  // The round trip must keep the overview — not resurrect "A Game" via the
  // tab-switch re-push. Drive the QTabBar itself: the user's click path
  // emits activeTabChanged (programmatic setActiveTab deliberately does
  // not), and that emission is what runs the manager's re-push handler.
  auto *tabBar = pane->findChild<QTabBar *>();
  QVERIFY(tabBar);
  tabBar->setCurrentIndex(static_cast<int>(DetailsPaneTab::Collection));
  tabBar->setCurrentIndex(static_cast<int>(DetailsPaneTab::Item));
  QTest::qWait(UIConstants::DetailsPane::METADATA_DEBOUNCE_MS * 3); // let any stray re-push land
  QCOMPARE(nameLabel->text(), QStringLiteral("Sony"));
}

void TestEventManagerDetailsPane::detailsPaneManager_collectionTab_showsSelectedItemsOwner() {
  QList<CollectionConfig> seed;
  CollectionConfig games;
  games.name = QStringLiteral("Games");
  CollectionConfig ds;
  ds.name = QStringLiteral("Nintendo DS");
  ds.parentCollectionIndex = 0;
  ds.isSubcollection = true;
  seed = {games, ds};
  KartendTest::MockedMainWindowFixture fixture(seed);
  MainWindow *win = fixture.window();
  DetailsPaneManager *mgr = win->getApplicationManager()->getDetailsPaneManager();
  QVERIFY(mgr);
  auto *pane = win->findChild<DetailsPane *>();
  QVERIFY(pane);
  auto *nameLabel = pane->findChild<QLabel *>(QStringLiteral("itemNameValue"));
  QVERIFY(nameLabel);
  mgr->applySidebarStateForCollection(0, /*reloadBackground=*/false);

  // The item lives in the CHILD collection while the parent shell is viewed.
  // The fixture installs MockDatabaseManager, so the downcast is safe here.
  auto *mockDb = static_cast<KartendTest::MockDatabaseManager *>(
      win->getApplicationManager()->getDatabaseManager());
  QVERIFY(mockDb);
  mockDb->collectionIndexForFileResult = 1;

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("ds-game.kartlink"));
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral("x"));
  }
  mgr->updateSidebarMetadata(path, QStringLiteral("A DS Game"));
  QTRY_COMPARE_WITH_TIMEOUT(nameLabel->text(), QStringLiteral("A DS Game"), 2000);

  // The Collection tab describes the item's parent, not the viewed shell.
  auto *tabBar = pane->findChild<QTabBar *>();
  QVERIFY(tabBar);
  tabBar->setCurrentIndex(static_cast<int>(DetailsPaneTab::Collection));
  QTest::qWait(UIConstants::DetailsPane::METADATA_DEBOUNCE_MS * 3);
  QCOMPARE(nameLabel->text(), QStringLiteral("Nintendo DS"));

  // And when the file map cannot place the path at all (the user report:
  // exact-key miss on differently-spelled paths), the config-derived
  // media-directory prefix fallback still finds the owner.
  mockDb->collectionIndexForFileResult = -1;
  QTemporaryDir dsMedia;
  QVERIFY(dsMedia.isValid());
  (*const_cast<QList<CollectionConfig> *>(&win->collections()))[1].mediaDirectory = dsMedia.path();
  const QString path2 = dsMedia.filePath(QStringLiteral("other-game.kartlink"));
  {
    QFile f(path2);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral("x"));
  }
  tabBar->setCurrentIndex(static_cast<int>(DetailsPaneTab::Item));
  mgr->updateSidebarMetadata(path2, QStringLiteral("Other Game"));
  QTRY_COMPARE_WITH_TIMEOUT(nameLabel->text(), QStringLiteral("Other Game"), 2000);
  tabBar->setCurrentIndex(static_cast<int>(DetailsPaneTab::Collection));
  QTest::qWait(UIConstants::DetailsPane::METADATA_DEBOUNCE_MS * 3);
  QCOMPARE(nameLabel->text(), QStringLiteral("Nintendo DS"));
}

void TestEventManagerDetailsPane::
    detailsPaneManager_externallyHidden_overridesVisibilityWhileSet() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  DetailsPaneManager *mgr = win->getApplicationManager()->getDetailsPaneManager();
  QVERIFY(mgr);

  // Force the persisted state to "visible" so the override has something to
  // suppress. If toggling left it visible already, this is a no-op flip;
  // otherwise it brings it up.
  if (!mgr->isSidebarVisible()) {
    mgr->toggleSidebar();
  }
  QVERIFY(mgr->isSidebarVisible());

  // Capture every signal across the override toggle so we can assert the
  // emitted bool matches the visibility AND of (!externallyHidden) and
  // m_sidebarVisible — the contract documented in setExternallyHidden().
  // updateSidebarLayout cascades may emit additional times; we pin only on
  // the last emission per setExternallyHidden call.
  QSignalSpy spy(mgr, &DetailsPaneManager::sidebarVisibilityChanged);
  mgr->setExternallyHidden(true);
  // While externally hidden the manager reports m_sidebarVisible unchanged
  // (it's the persisted flag) but the last emitted visibility was false.
  QVERIFY(mgr->isSidebarVisible()); // persisted flag untouched
  QVERIFY2(spy.count() >= 1, "setExternallyHidden(true) must emit at least once");
  QCOMPARE(spy.last().at(0).toBool(), false);
  spy.clear();

  // Clearing the override should fire another change with the persisted
  // value (true).
  mgr->setExternallyHidden(false);
  QVERIFY2(spy.count() >= 1, "setExternallyHidden(false) must emit at least once");
  QCOMPARE(spy.last().at(0).toBool(), true);
}

void TestEventManagerDetailsPane::detailsPaneManager_externallyHidden_redundantSetIsNoOp() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  DetailsPaneManager *mgr = win->getApplicationManager()->getDetailsPaneManager();
  QVERIFY(mgr);

  // First call sets the override (and emits); second identical call must be
  // a no-op (no signal, no extra layout work). Without the early-return
  // guard this would re-run updateSidebarLayout on every redundant call.
  QSignalSpy spy(mgr, &DetailsPaneManager::sidebarVisibilityChanged);
  mgr->setExternallyHidden(true);
  const int afterFirst = spy.count();
  mgr->setExternallyHidden(true);
  QCOMPARE(spy.count(), afterFirst);
}

void TestEventManagerDetailsPane::detailsPaneManager_toggleClearsExternallyHidden() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  DetailsPaneManager *mgr = win->getApplicationManager()->getDetailsPaneManager();
  QVERIFY(mgr);

  // Establish externally-hidden state then deliberately toggle. Per
  //, the toggle must clear m_externallyHidden so subsequent
  // selections in cover flow respect the user's pull-back-in intent.
  mgr->setExternallyHidden(true);
  const bool persistedBefore = mgr->isSidebarVisible();

  mgr->toggleSidebar();

  // Toggling flips the persisted m_sidebarVisible AND clears the override.
  // The post-condition is the visibility = !persistedBefore (since override
  // is now cleared).
  QCOMPARE(mgr->isSidebarVisible(), !persistedBefore);

  // Setting externallyHidden=false again should now be a no-op (the toggle
  // already cleared it). Verifies the override was actually cleared, not
  // just bypassed.
  QSignalSpy spy(mgr, &DetailsPaneManager::sidebarVisibilityChanged);
  mgr->setExternallyHidden(false);
  QCOMPARE(spy.count(), 0);
}

void TestEventManagerDetailsPane::eventManager_wheelOverForeignWindow_isNotClaimedByGridScroll() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  QVERIFY(win);
  EventManager *em = win->getApplicationManager()->getInteractionManager()->eventManager();
  QVERIFY(em);

  // A widget living in a SEPARATE top-level window (no parent) stands in for a
  // dialog like the DAT Manager. The app-level filter must NOT consume a wheel
  // tick over it as a main-window grid scroll — returning false lets Qt deliver
  // the event to the dialog's own list/table so it scrolls.
  QWidget foreign;
  QWidget child(&foreign);
  QWheelEvent wheel(QPointF(5, 5), QPointF(5, 5), QPoint(0, 0), QPoint(0, -120), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, /*inverted=*/false);
  QVERIFY2(!em->filterEvent(&child, &wheel),
           "wheel over a foreign window must pass through, not scroll the grid");
}
