#include "test_attractmanager.h"

#include "applicationmanager.h"
#include "attractmanager.h"
#include "collection/generalsettings.h"
#include "eventmanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
// Kartend-xrj9r: this suite asserts only on in-memory coordinator state
// (never on persisted rows/INI), so it runs against the mocked fixture —
// no SQLite/QSettings setup per slot.
#include "mocks/mockedmainwindowfixture.h"

#include <QSignalSpy>
#include <QTest>

void TestAttractManager::testConstructionInitialDefaults() {
  AttractManager mgr;
  // Attract mode starts disengaged on every construction. m_attractActive
  // would otherwise drive autoscroll the moment the idle timer fires —
  // which without an attached scroll area is a NOP, but a regression
  // that flipped the default would mask startup misconfigurations.
  QVERIFY(!mgr.isActive());
  QVERIFY(!mgr.isDrivingSelection());
  // Without a GeneralSettings pointer, isEnabled() must short-circuit to
  // false — the production setupReferences calls reloadSettings(), which
  // gracefully no-ops in this state.
  QVERIFY(!mgr.isEnabled());
}

void TestAttractManager::testIsEnabledFollowsSettings() {
  AttractManager mgr;
  GeneralSettings settings;
  // Default GeneralSettings has attractModeEnabled=false. Wire the
  // pointer through setupReferences and observe the accessor change
  // when we mutate the flag — the read path is a direct pointer
  // dereference so a regression that copied the bool by value (and
  // missed live edits) would surface here.
  AttractManagerSetup setup;
  setup.generalSettings = &settings;
  mgr.setupReferences(setup);

  QVERIFY(!mgr.isEnabled());

  settings.attract.attractModeEnabled = true;
  QVERIFY(mgr.isEnabled());

  settings.attract.attractModeEnabled = false;
  QVERIFY(!mgr.isEnabled());
}

void TestAttractManager::testReloadSettingsWithoutSettingsIsSafe() {
  AttractManager mgr;
  // reloadSettings is called by setupReferences and by the settings
  // dialog's "apply" path. With no GeneralSettings pointer wired, it
  // must early-return without crashing — m_idleTimer->stop() is safe
  // on an idle timer, but reading attractModeEnabled off a null
  // settings pointer would crash without the guard.
  mgr.reloadSettings();
  QVERIFY(!mgr.isActive());
}

void TestAttractManager::testOnActivityDetectedIsNoOpWhenInactive() {
  AttractManager mgr;
  // onActivityDetected stops attract if active and re-arms the idle
  // countdown. On a fresh manager with no scroll area, no settings,
  // and no active attract, it must be a safe no-op that leaves the
  // accessors at their defaults — InteractionManager calls this from
  // every user input event, including before setupReferences runs
  // during early startup wiring.
  mgr.onActivityDetected();
  QVERIFY(!mgr.isActive());
  QVERIFY(!mgr.isDrivingSelection());
}

void TestAttractManager::testSetSuspendedToggleIsIdempotent() {
  AttractManager mgr;
  QSignalSpy startedSpy(&mgr, &AttractManager::attractStarted);
  QSignalSpy stoppedSpy(&mgr, &AttractManager::attractStopped);

  // setSuspended early-returns when the requested state matches the
  // current. MainWindow may call setSuspended(true) repeatedly during
  // a launched-app lifetime; the guard prevents idle-timer thrash
  // and avoids the resetIdleTimer reset that resume normally
  // performs. No signals should fire for these no-ops.
  mgr.setSuspended(false);
  mgr.setSuspended(false);
  mgr.setSuspended(true);
  mgr.setSuspended(true);
  mgr.setSuspended(false);

  // Attract was never running (no settings/scroll area), so neither
  // signal could fire even without the guard — but the guard prevents
  // any internal state churn that downstream observers might react to.
  QCOMPARE(startedSpy.count(), 0);
  QCOMPARE(stoppedSpy.count(), 0);
}

void TestAttractManager::testSetSuspendedDoesNotStartAttract() {
  AttractManager mgr;
  // Suspending then resuming a manager that has no settings/scroll area
  // must not start attract. resetIdleTimer (called on resume) checks
  // isEnabled() first; without settings, isEnabled is false and the
  // idle timer stays unarmed — so no path could possibly reach
  // startAttract().
  mgr.setSuspended(true);
  mgr.setSuspended(false);
  QVERIFY(!mgr.isActive());
}

void TestAttractManager::testFixtureExposesAttractManagerViaInteractionManager() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *interaction = win->getApplicationManager()->getInteractionManager();
  QVERIFY(interaction);

  // The live wiring constructs AttractManager during InteractionManager
  // init and feeds it via setupReferences. After fixture construction
  // it must be reachable and inactive (no user activity has happened,
  // and the fixture pre-seeds firstRunComplete=true so no startup
  // wizard interferes with the idle timer).
  AttractManager *attract = interaction->attractManager();
  QVERIFY(attract != nullptr);
  QVERIFY(!attract->isActive());
  QVERIFY(!attract->isDrivingSelection());
}

void TestAttractManager::testWheelScrollCountsAsActivity() {
  // Scrolling the grid is browsing, and attract mode must yield to it. The
  // selectionChanged wiring covers a wheel only when it LANDS on a new index;
  // applySelectionDelta returns early for wheelSteps == 0, which is what
  // fine-grained trackpad and high-resolution wheel deltas produce. Those
  // scrolls moved the view with no selectionChanged, so attract never heard
  // about them and its next advance tick yanked the selection back — reported
  // 2026-08-19 as "selection reverting on mouse scroll sometimes".
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *interaction = win->getApplicationManager()->getInteractionManager();
  QVERIFY(interaction);
  AttractManager *attract = interaction->attractManager();
  EventManager *events = interaction->eventManager();
  QVERIFY(attract != nullptr);
  QVERIFY(events != nullptr);

  // disconnect() reports whether a connection was actually there, which is
  // the assertion: the wheel signal must reach attract's activity slot. The
  // alternative — driving a real wheel event and watching isActive() — needs
  // attract to already be running, and starting it is deliberately gated.
  const bool wheelWiredToActivity = QObject::disconnect(
      events, &EventManager::wheelScrollStarted, attract, &AttractManager::onActivityDetected);
  QVERIFY2(wheelWiredToActivity,
           "EventManager::wheelScrollStarted is not wired to AttractManager::onActivityDetected — "
           "a wheel scroll that does not move the selection leaves attract mode running, and its "
           "next tick reverts the user's position");
}
