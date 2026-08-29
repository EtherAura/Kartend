#include "test_attractmanager.h"

#include "applicationmanager.h"
#include "attractmanager.h"
#include "collection/generalsettings.h"
#include "eventmanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "selectionmanager.h"
// Kartend-xrj9r: this suite asserts only on in-memory coordinator state
// (never on persisted rows/INI), so it runs against the mocked fixture —
// no SQLite/QSettings setup per slot.
#include "mocks/fakescrollmanager.h"
#include "mocks/mockedmainwindowfixture.h"

#include "applicationcontext.h"

#include <QScrollArea>
#include <QScrollBar>
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

// ─────────────────────────────────────────────────────────────────────────────
// Cover Flow autoscroll (Kartend-wmxwg)
//
// Attract's autoscroll drives m_itemScrollArea's scrollbar. Cover Flow hides
// that scroll area outright and forces both bar policies to ScrollBarAlwaysOff,
// so `bar->maximum() > 0` is permanently false there and autoscroll silently
// never started. These pin the carousel path that replaces it: the fake
// reports itself driftable, and every scrollbar in play deliberately has zero
// range so nothing can pass through the old branch by accident.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// A scroll area whose bars have no range — exactly what Cover Flow leaves
/// behind, and what made the old `bar->maximum() > 0` gate refuse to start.
struct AttractCoverFlowRig {
  KartendTest::FakeScrollManager scroll;
  ApplicationContext ctx;
  QScrollArea area;
  GeneralSettings settings;
  AttractManager mgr;

  explicit AttractCoverFlowRig(bool coverFlow) {
    scroll.coverFlowDrift = coverFlow;
    ctx.managers.seedScrollRoles(&scroll);

    settings.attract.attractModeEnabled = true;
    settings.attract.attractModeAutoScrollEnabled = true;
    // Off, so nothing but the scroll tick can move the selection — otherwise
    // a drift assertion could not tell the two apart.
    settings.attract.attractModeAdvanceSelectionEnabled = false;

    AttractManagerSetup setup;
    setup.ctx = &ctx;
    setup.itemScrollArea = &area;
    setup.generalSettings = &settings;
    mgr.setupReferences(setup);
  }

  /// startAttract() is private and reached in production through the idle
  /// timer, whose minimum interval is 10s. Invoking the slot by name drives
  /// the same entry point without the wait.
  void goIdle() { QMetaObject::invokeMethod(&mgr, "onIdleTimeout", Qt::DirectConnection); }
  void tick() { QMetaObject::invokeMethod(&mgr, "onScrollTick", Qt::DirectConnection); }
};

} // namespace

void TestAttractManager::testCoverFlowStartsAutoscrollWithNoScrollbarRange() {
  AttractCoverFlowRig rig(/*coverFlow=*/true);
  QCOMPARE(rig.area.verticalScrollBar()->maximum(), 0);

  rig.goIdle();
  // THE REGRESSION: with only the scrollbar test, wantScroll && !scrollable &&
  // !wantAdvance re-armed the idle timer and returned, so attract never became
  // active in Cover Flow at all.
  QVERIFY2(rig.mgr.isActive(),
           "attract refused to start in Cover Flow — the carousel-driftable test is not being "
           "consulted, so autoscroll is gated on a scrollbar Cover Flow does not have");
}

void TestAttractManager::testCoverFlowTickDriftsCarouselAtConfiguredSpeed() {
  AttractCoverFlowRig rig(/*coverFlow=*/true);
  rig.settings.attract.attractModeScrollSpeed = 2.5;
  rig.goIdle();
  QVERIFY(rig.mgr.isActive());

  rig.tick();
  QCOMPARE(rig.scroll.driftCalls.size(), 1);
  // Speed is signed by the scroll direction, which starts at +1. Passed
  // through verbatim in pixels: the carousel converts against its own card
  // pitch, so the setting keeps its documented pixels-per-tick meaning in
  // both views. No integer accumulator here — the carousel position is a
  // qreal, so a 0.1px/tick speed is representable rather than rounding to 0.
  QCOMPARE(rig.scroll.driftCalls.first(), 2.5);

  rig.tick();
  rig.tick();
  QCOMPARE(rig.scroll.driftCalls.size(), 3);
}

void TestAttractManager::testCoverFlowDriftRunsUnderTheDrivingSelectionGuard() {
  AttractCoverFlowRig rig(/*coverFlow=*/true);
  bool drivingDuringDrift = false;
  rig.scroll.onDrift = [&]() { drivingDuringDrift = rig.mgr.isDrivingSelection(); };

  rig.goIdle();
  rig.tick();

  // The drift carries the canonical selection with it, so it reaches
  // SelectionManager::selectionChanged → InteractionManager::onActivityDetected
  // exactly as onAdvanceSelectionTick does. Without the guard held across the
  // call, attract reads its own carousel move as user input and stops itself on
  // the first tick.
  QVERIFY2(drivingDuringDrift,
           "driftCoverFlow ran outside the isDrivingSelection() guard — the selection change it "
           "causes will be read as user activity and stop attract on its first tick");
  // And it must not be left stuck on afterwards, or real input stops being
  // detected for the rest of the session (Kartend-77ay).
  QVERIFY(!rig.mgr.isDrivingSelection());
}

void TestAttractManager::testCoverFlowDriftEndBouncesAndReversesDirection() {
  AttractCoverFlowRig rig(/*coverFlow=*/true);
  rig.settings.attract.attractModeScrollSpeed = 1.0;
  // Second call reports "hit an end", the carousel's equivalent of a scrollbar
  // reaching maximum().
  rig.scroll.driftCallsBeforeEnd = 2;
  rig.goIdle();

  rig.tick();
  rig.tick(); // returns false -> bounce pause starts
  QCOMPARE(rig.scroll.driftCalls.size(), 2);

  // While bounce-paused the tick is inert, so the carousel holds at the end
  // for BOUNCE_PAUSE_MS rather than grinding against it.
  rig.tick();
  QCOMPARE(rig.scroll.driftCalls.size(), 2);

  QMetaObject::invokeMethod(&rig.mgr, "onBouncePauseFinished", Qt::DirectConnection);
  rig.scroll.driftCallsBeforeEnd = -1;
  rig.tick();
  QCOMPARE(rig.scroll.driftCalls.size(), 3);
  // Reversed: travel is now toward the other end of the carousel.
  QCOMPARE(rig.scroll.driftCalls.last(), -1.0);
}

void TestAttractManager::testNonCoverFlowViewStillRefusesToStartWithoutScrollbarRange() {
  // The complement of the first test, and the reason coverFlowDriftable() is a
  // distinct question rather than "is the view Cover Flow": a grid whose content
  // fits the viewport has nothing to scroll either, and must keep declining.
  AttractCoverFlowRig rig(/*coverFlow=*/false);
  rig.goIdle();
  QVERIFY(!rig.mgr.isActive());
  rig.tick();
  QVERIFY(rig.scroll.driftCalls.isEmpty());
}

void TestAttractManager::testStoppingAttractSettlesTheCarousel() {
  AttractCoverFlowRig rig(/*coverFlow=*/true);
  rig.goIdle();
  QVERIFY(rig.mgr.isActive());
  rig.tick();
  rig.tick();
  QCOMPARE(rig.scroll.settleCalls, 0); // nothing settled while still drifting

  // A drift halts on whatever sub-card position the last tick reached, so
  // stopping without settling leaves the selected card visibly off-centre with
  // its neighbour crowding the middle. Scrollbar views need no equivalent: they
  // stop at a scroll offset the user could have reached themselves.
  rig.mgr.onActivityDetected();
  QVERIFY(!rig.mgr.isActive());
  QCOMPARE(rig.scroll.settleCalls, 1);

  // Idempotent under a repeated stop — onActivityDetected fires on every user
  // input, and stopAttract early-returns when already inactive.
  rig.mgr.onActivityDetected();
  QCOMPARE(rig.scroll.settleCalls, 1);
}

void TestAttractManager::testKeyboardSelectionPathIsWiredToActivityDetection() {
  // Kartend-k9utx. Attract's activity detection was wired ONLY to
  // SelectionManager::selectionChanged. That signal never fires for a keyboard
  // move: SelectionManager::selectItemByIndex, the one emitter that would cover
  // one, has no callers at all — InteractionManager::selectItemByIndex
  // reimplements the job and emits its OWN selectionChanged, and that is what
  // ArrowNavigationHandler::requestFullSelectionUpdate reaches. The result was
  // that arrow keys could not stop attract in any view, while the wheel could
  // (it reaches SelectionManager::notifySelectionChanged), which is exactly the
  // confusing half-working behaviour observed in the guest.
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *interaction = win->getApplicationManager()->getInteractionManager();
  QVERIFY(interaction);
  AttractManager *attract = interaction->attractManager();
  QVERIFY(attract != nullptr);

  // disconnect() reports whether a connection was actually present. Asserting
  // on the wire rather than on isActive() for the same reason the wheel test
  // does: driving it end-to-end needs attract already running, and starting it
  // is deliberately gated behind the idle timer and a scrollable view.
  const bool keyboardPathWired =
      QObject::disconnect(interaction, &InteractionManager::selectionChanged, attract, nullptr);
  QVERIFY2(keyboardPathWired,
           "InteractionManager::selectionChanged is not wired to attract's activity detection — "
           "keyboard navigation cannot stop attract mode, because the arrow path never reaches "
           "SelectionManager::selectionChanged");

  // The original wire must survive too: it is what still covers a wheel landing,
  // a hover and a click, none of which go through InteractionManager's emit.
  auto *selection = interaction->selectionManager();
  QVERIFY(selection != nullptr);
  const bool pointerPathWired =
      QObject::disconnect(selection, &SelectionManager::selectionChanged, attract, nullptr);
  QVERIFY2(pointerPathWired,
           "SelectionManager::selectionChanged is no longer wired to attract's activity detection "
           "— wheel-landed, hover and click selections would stop counting as user activity");
}
