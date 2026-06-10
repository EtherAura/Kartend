#include "test_detailspanemanager.h"

#include "applicationmanager.h"
#include "detailspanemanager.h"
#include "mainwindow.h"
#include "mainwindowfixture.h"

#include <QSignalSpy>
#include <QTest>

void TestDetailsPaneManager::testConstructionInitialDefaults() {
  DetailsPaneManager mgr;

  // The default state is "no sidebar yet, nothing externally hidden, no
  // overlay active, no current collection". applySidebarStateForCollection
  // and setupReferences both rely on this baseline; a drifted default
  // here would cascade into the sidebar appearing for a never-selected
  // collection on startup.
  QVERIFY(!mgr.isSidebarVisible());
  QVERIFY(!mgr.isExternallyHidden());
  QCOMPARE(mgr.currentCollectionIndex(), -1);
  QVERIFY(mgr.sidebarWidget() == nullptr);
}

void TestDetailsPaneManager::testToggleSidebarIsNoOpWithoutDetailsPane() {
  DetailsPaneManager mgr;
  // toggleSidebar early-returns when m_DetailsPane is null. This is the
  // contract that lets InteractionManager hand off the toggle action
  // before setupReferences has run during early startup wiring. State
  // must remain pristine and no signal must fire — a regression that
  // mutated m_sidebarVisible before the null guard would emit a spurious
  // visibility change for a sidebar that doesn't exist yet.
  QSignalSpy visibilitySpy(&mgr, &DetailsPaneManager::sidebarVisibilityChanged);

  mgr.toggleSidebar();

  QVERIFY(!mgr.isSidebarVisible());
  QCOMPARE(visibilitySpy.count(), 0);
}

void TestDetailsPaneManager::testSetExternallyHiddenTogglesAndEmits() {
  DetailsPaneManager mgr;
  // setExternallyHidden is the cover-flow escape hatch: it forces the
  // sidebar hidden without touching the per-collection persisted
  // preference. The emitted visibility is (sidebarVisible && !hidden) so
  // a freshly-constructed manager whose persisted state is also false
  // emits "false" — the spy must see exactly one emission and the
  // accessor must observe the new override.
  QSignalSpy spy(&mgr, &DetailsPaneManager::sidebarVisibilityChanged);

  mgr.setExternallyHidden(true);

  QVERIFY(mgr.isExternallyHidden());
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toBool(), false);
}

void TestDetailsPaneManager::testSetExternallyHiddenIdempotentDoesNotEmit() {
  DetailsPaneManager mgr;
  mgr.setExternallyHidden(true);
  QSignalSpy spy(&mgr, &DetailsPaneManager::sidebarVisibilityChanged);

  // Calling with the same value must short-circuit — without the guard,
  // a window resize or cover-flow re-entry that re-asserts the same
  // "hidden" state would emit a redundant visibility change and force
  // the layout pass to re-run for no reason.
  mgr.setExternallyHidden(true);

  QCOMPARE(spy.count(), 0);
  QVERIFY(mgr.isExternallyHidden());
}

void TestDetailsPaneManager::testSetOverlayActiveDoesNotCrash() {
  DetailsPaneManager mgr;
  // setOverlayActive has no readback API — its effect is internal to
  // updateSidebarLayout (skipping raise() while an overlay is on top).
  // Verifying it doesn't crash on a bare manager (where updateSidebarLayout
  // early-returns) is enough; the layout-side behaviour is exercised
  // implicitly by the live fixture path.
  mgr.setOverlayActive(true);
  mgr.setOverlayActive(false);
  mgr.setOverlayActive(true);
  // No assertion needed beyond "did not crash" — survival is the contract.
  QVERIFY(true);
}

void TestDetailsPaneManager::testApplySidebarStateForCollectionInvalidIndexIsNoOp() {
  DetailsPaneManager mgr;
  // Without collections wired, every index is out of range. The method
  // must early-return; m_currentCollectionIndex must stay at its -1
  // sentinel so downstream code that reads it (e.g. updateSidebarLayout
  // via the toggle path) can still tell "no selection".
  mgr.applySidebarStateForCollection(0);
  mgr.applySidebarStateForCollection(-1);
  mgr.applySidebarStateForCollection(999);

  QCOMPARE(mgr.currentCollectionIndex(), -1);
}

void TestDetailsPaneManager::testRefreshCollectionSummaryEarlyReturnsWithoutDetailsPane() {
  DetailsPaneManager mgr;
  // refreshCollectionSummary is called from many places (collection switch,
  // scan completion, settings save). It must be safe to invoke before the
  // sidebar widget is wired — a missing early-return would dereference
  // m_DetailsPane and crash on cold startup if the call beat setupReferences.
  mgr.refreshCollectionSummary();
  QVERIFY(true);
}

void TestDetailsPaneManager::testSidebarWidgetReturnsNullBeforeSetup() {
  DetailsPaneManager mgr;
  // sidebarWidget() is the IDetailsPaneManager role accessor used by
  // sibling managers (e.g. EventManager) to reach the pane. Before
  // setupReferences resolves the dynamic_cast<DetailsPane*>(setup.sidebar),
  // it must return null so callers' null-checks succeed instead of
  // dereferencing a stale pointer.
  QVERIFY(mgr.sidebarWidget() == nullptr);
}

void TestDetailsPaneManager::testFixtureExposesDetailsPaneManagerViaApplicationManager() {
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *details = win->getApplicationManager()->getDetailsPaneManager();

  // The live wiring constructs DetailsPaneManager during ApplicationManager
  // init and feeds it via setupReferences. After fixture construction it
  // must be reachable and exhibit the same default state contract the
  // bare-construction tests above lock in. A regression that left the
  // unique_ptr null after init would surface here as a null return.
  QVERIFY(details != nullptr);
  QVERIFY(!details->isExternallyHidden());
  // m_currentCollectionIndex stays at the -1 sentinel until the first
  // applySidebarStateForCollection call (which the fixture doesn't
  // trigger because no collections are configured).
  QCOMPARE(details->currentCollectionIndex(), -1);
}
