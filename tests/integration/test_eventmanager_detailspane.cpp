#include "test_eventmanager_detailspane.h"
#include "applicationmanager.h"

#include "detailspanemanager.h"
#include "eventmanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "mainwindowfixture.h"

#include <QSignalSpy>
#include <QTest>

void TestEventManagerDetailsPane::eventManager_isWiredOnInteractionManager() {
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  QVERIFY(win);
  InteractionManager *im = win->getApplicationManager()->getInteractionManager();
  QVERIFY(im);
  EventManager *em = im->eventManager();
  QVERIFY2(em, "InteractionManager must own a constructed EventManager after MainWindow setup");
}

void TestEventManagerDetailsPane::detailsPaneManager_toggleSidebar_flipsVisibilityAndEmits() {
  KartendTest::MainWindowFixture fixture;
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
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  DetailsPaneManager *mgr = win->getApplicationManager()->getDetailsPaneManager();
  QVERIFY(mgr);

  const bool initial = mgr->isSidebarVisible();
  mgr->toggleSidebar();
  mgr->toggleSidebar();
  QCOMPARE(mgr->isSidebarVisible(), initial);
}

void TestEventManagerDetailsPane::
    detailsPaneManager_externallyHidden_overridesVisibilityWhileSet() {
  KartendTest::MainWindowFixture fixture;
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
  KartendTest::MainWindowFixture fixture;
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
  KartendTest::MainWindowFixture fixture;
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
