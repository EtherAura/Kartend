/**
 * @file test_gamepadmanager.cpp
 * @brief Unit tests for GamepadManager's input-translation layer (Kartend-npoh2).
 *
 * test_gamepadhelpers covers the pure input math (axis hysteresis, direction
 * combine, button-action resolution). These tests cover the manager logic
 * around it: settings-driven button→action dispatch, binding-capture mode,
 * suspend gating, the keyboard-repeat handshake on direction changes, and
 * the dpad/stick→signal pipeline, and the application-focus gate
 * (Kartend-4nf4a). The backend callbacks (Qt6 Gamepad signals
 * / SDL polling) need real hardware, so the test is a friend of the manager
 * and injects the same private state / calls the backends would.
 *
 * No backend is required at runtime: everything here drives the translation
 * layer directly. Only updateDirectionFromInputs() has a backend-gated body,
 * so those slots QSKIP on builds with no gamepad backend compiled in.
 */

#include "applicationcontext.h"
#include "gamepadmanager.h"
#include "ikeyboardmanager.h"

#include <memory>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QTimer>

namespace {

/// Records the keyboard-repeat handshake calls GamepadManager makes, in
/// order, so tests can assert the exact press/release sequences.
class FakeKeyboardManager : public IKeyboardManager {
public:
  QStringList log;

  bool handleKeyPress(QKeyEvent * /*event*/, bool /*searchBarFocused*/) override { return false; }
  bool handleKeyRelease(QKeyEvent * /*event*/) override { return false; }

  void stopRepeat(bool suppressRecentering) override {
    log << (suppressRecentering ? QStringLiteral("stopRepeat(suppress)")
                                : QStringLiteral("stopRepeat(recenter)"));
  }
  [[nodiscard]] bool isRepeating() const override { return false; }
  void finalizeKeyRepeatForKey(Qt::Key /*key*/, int /*direction*/, bool /*vertical*/) override {}
  [[nodiscard]] bool consumePendingNavigationKey(Qt::Key & /*outKey*/) override { return false; }

  [[nodiscard]] bool isPhysicalKeyDown() const override { return m_physicalKeyDown; }
  void setPhysicalKeyDown(bool down) override {
    m_physicalKeyDown = down;
    log << (down ? QStringLiteral("physDown(true)") : QStringLiteral("physDown(false)"));
  }
  [[nodiscard]] int repeatDelta() const override { return 0; }
  [[nodiscard]] bool repeatVertical() const override { return false; }

  [[nodiscard]] bool isWrapSequenceActive() const override { return false; }
  void setWrapSequenceActive(bool /*active*/) override {}

  [[nodiscard]] bool isContinuousScrollActive() const override { return false; }
  void setContinuousScrollActive(bool /*active*/) override {}

  void prepareKeyNavigationState() override {}
  void finalizeKeyRepeat(QKeyEvent * /*event*/, int /*direction*/, bool /*vertical*/) override {}

private:
  bool m_physicalKeyDown = false;
};

} // namespace

class TestGamepadManager : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  // handleMappedButtonPress — settings-driven dispatch
  void button_defaultConfirmEmitsEnter();
  void button_defaultBackEmitsEscape();
  void button_defaultToggleSidebarEmitsToggle();
  void button_remappedConfirmFollowsSettings();
  void button_unboundEmitsNothing();
  void button_whitespaceNameIgnored();
  void button_droppedWhileSuspended();
  void button_droppedWhenShuttingDown();
  void chord_selectHeldDirectionMovesFocusSectionNotSelection();
  void rightStick_flickHopsFocusSection_oncePerDeflection_settingGates();

  // Binding-capture mode
  void capture_buttonRoutedToCaptureSignalOnly();
  void capture_beginStopsKeyboardRepeat();
  void capture_endClearsHeldDirection();

  // applyActiveDirection — keyboard-repeat handshake
  void direction_changeEmitsScrollStopMoveAndKeyboardSequence();
  void direction_unchangedEmitsNothing();
  void direction_toNoneReleasesKeyboardWithRecenter();
  void direction_nullKeyboardManagerIsSafe();

  // updateDirectionFromInputs — dpad/stick state → signals (backend-gated body)
  void inputs_dpadLeftEmitsHorizontalMove();
  void inputs_useDpadFalseSuppressesDpad();
  void inputs_stickUpEmitsVerticalMove();
  void inputs_useStickFalseSuppressesStick();
  void inputs_suspendedProducesNoMove();

  // setSuspended
  void suspend_clearsHeldDirectionAndReleasesKeyboard();
  void suspend_resumeRestoresButtonDispatch();

  // Application-focus gate (Kartend-4nf4a)
  void focus_seededFromApplicationState();
  void focus_applicationSignalDrivesGate();
  void focus_lostDropsButtonsDirectionsAndRightStick();
  void focus_lostClearsHeldDirectionAndReleasesKeyboard();
  void focus_lostStopsRightStickPaneScroll();
  void focus_heldDeflectionIsNoFreshFlickOnRefocus();
  void focus_lostDropsBindingCapture();
  void focus_handsModifierHudOverAtTheGate();
  void focus_composesWithSuspend();

private:
  // Calls the gated body; helper so backend-dependent slots can QSKIP in
  // one place on no-backend builds.
  [[nodiscard]] bool backendCompiledIn() const {
#if defined(KARTEND_HAS_QT_GAMEPAD) || defined(KARTEND_HAS_SDL2_GAMEPAD)
    return true;
#else
    return false;
#endif
  }

  GeneralSettings m_settings;
  bool m_shutdown = false;
  ApplicationContext m_appCtx;
  FakeKeyboardManager m_kb;
  std::unique_ptr<GamepadManager> m_mgr;
};

void TestGamepadManager::init() {
  m_settings = GeneralSettings{};
  m_shutdown = false;
  m_appCtx = ApplicationContext{};
  m_appCtx.managers.keyboardManager = &m_kb;
  m_kb.log.clear();

  m_mgr = std::make_unique<GamepadManager>();

#if defined(KARTEND_HAS_SDL2_GAMEPAD)
  // The SDL backend polls real controller state on a timer; stop it so a
  // controller plugged into the dev box can't race the injected state.
  if (m_mgr->m_pollTimer) {
    m_mgr->m_pollTimer->stop();
  }
#endif

  GamepadManagerSetup setup;
  setup.ctx = &m_appCtx;
  setup.generalSettings = &m_settings;
  setup.isShuttingDown = &m_shutdown;
  m_mgr->setupReferences(setup);
  // The manager seeds its focus gate from QGuiApplication::applicationState(),
  // and nothing is focused under the test runner, so it starts blocked.
  // Every slot except the focus_* ones wants a focused frontend.
  m_mgr->onApplicationStateChanged(Qt::ApplicationActive);
  m_kb.log.clear();
}

void TestGamepadManager::cleanup() {
  m_mgr.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// handleMappedButtonPress
// ─────────────────────────────────────────────────────────────────────────────

void TestGamepadManager::button_defaultConfirmEmitsEnter() {
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);
  QSignalSpy escape(m_mgr.get(), &GamepadManager::requestEscapeAction);

  m_mgr->handleMappedButtonPress(QStringLiteral("A"));

  QCOMPARE(enter.count(), 1);
  QCOMPARE(escape.count(), 0);
}

void TestGamepadManager::button_defaultBackEmitsEscape() {
  QSignalSpy escape(m_mgr.get(), &GamepadManager::requestEscapeAction);

  m_mgr->handleMappedButtonPress(QStringLiteral("B"));

  QCOMPARE(escape.count(), 1);
}

void TestGamepadManager::button_defaultToggleSidebarEmitsToggle() {
  // Shoulder defaults since 2026-08-17: R1 toggles the details pane (right
  // sidebar), L1 the collection tree (left sidebar); the old Y default is
  // unbound.
  QSignalSpy toggle(m_mgr.get(), &GamepadManager::requestToggleSidebarAction);
  QSignalSpy tree(m_mgr.get(), &GamepadManager::requestToggleCollectionTreeAction);

  m_mgr->handleMappedButtonPress(QStringLiteral("R1"));
  QCOMPARE(toggle.count(), 1);
  QCOMPARE(tree.count(), 0);

  m_mgr->handleMappedButtonPress(QStringLiteral("L1"));
  QCOMPARE(tree.count(), 1);

  m_mgr->handleMappedButtonPress(QStringLiteral("Y"));
  QCOMPARE(toggle.count(), 1); // Y no longer bound
}

void TestGamepadManager::chord_selectHeldDirectionMovesFocusSectionNotSelection() {
  QSignalSpy focusMove(m_mgr.get(), &GamepadManager::requestFocusSectionMove);
  QSignalSpy selMove(m_mgr.get(), &GamepadManager::requestSelectionMove);

  m_mgr->m_buttonBack = true; // Select held
  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Left);
  QCOMPARE(focusMove.count(), 1);
  QCOMPARE(focusMove.at(0).at(0).toInt(), -1);
  QCOMPARE(focusMove.at(0).at(1).toInt(), 0);
  QCOMPARE(selMove.count(), 0);

  m_mgr->applyActiveDirection(GamepadHelpers::Direction::None);
  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Up);
  QCOMPARE(focusMove.count(), 2);
  QCOMPARE(focusMove.at(1).at(1).toInt(), -1);
  QCOMPARE(selMove.count(), 0);

  // Select released: directions drive the selection again.
  m_mgr->applyActiveDirection(GamepadHelpers::Direction::None);
  m_mgr->m_buttonBack = false;
  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Left);
  QCOMPARE(focusMove.count(), 2);
  QCOMPARE(selMove.count(), 1);
}

void TestGamepadManager::rightStick_flickHopsFocusSection_oncePerDeflection_settingGates() {
  if (!backendCompiledIn()) {
    QSKIP("No gamepad backend compiled in; updateRightStickSection is a no-op");
  }

  QSignalSpy focusMove(m_mgr.get(), &GamepadManager::requestRightStickFlick);
  QSignalSpy selMove(m_mgr.get(), &GamepadManager::requestSelectionMove);

  m_mgr->m_axisRightX = 1.0; // flick right
  m_mgr->updateRightStickSection();
  QCOMPARE(focusMove.count(), 1);
  QCOMPARE(focusMove.at(0).at(0).toInt(), 1);
  QCOMPARE(focusMove.at(0).at(1).toInt(), 0);

  m_mgr->updateRightStickSection(); // held deflection: no repeat
  QCOMPARE(focusMove.count(), 1);

  m_mgr->m_axisRightX = 0.0; // recentre arms the next flick
  m_mgr->updateRightStickSection();
  QCOMPARE(focusMove.count(), 1);

  m_mgr->m_axisRightY = -1.0; // flick up
  m_mgr->updateRightStickSection();
  QCOMPARE(focusMove.count(), 2);
  QCOMPARE(focusMove.at(1).at(0).toInt(), 0);
  QCOMPARE(focusMove.at(1).at(1).toInt(), -1);

  // The right stick never drives the grid selection.
  QCOMPARE(selMove.count(), 0);

  // Setting gate: disabled -> a fresh flick emits nothing.
  m_mgr->m_axisRightY = 0.0;
  m_mgr->updateRightStickSection();
  m_settings.gamepad.gamepadRightStickSections = false;
  m_mgr->m_axisRightX = 1.0;
  m_mgr->updateRightStickSection();
  QCOMPARE(focusMove.count(), 2);
}

void TestGamepadManager::button_remappedConfirmFollowsSettings() {
  m_settings.gamepad.gamepadConfirmButton = QStringLiteral("X");
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);

  // The old default no longer confirms; the remapped button does. Matching
  // is case-insensitive end to end (settings string vs incoming name).
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));
  QCOMPARE(enter.count(), 0);

  m_mgr->handleMappedButtonPress(QStringLiteral("x"));
  QCOMPARE(enter.count(), 1);
}

void TestGamepadManager::button_unboundEmitsNothing() {
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);
  QSignalSpy escape(m_mgr.get(), &GamepadManager::requestEscapeAction);
  QSignalSpy toggle(m_mgr.get(), &GamepadManager::requestToggleSidebarAction);

  m_mgr->handleMappedButtonPress(QStringLiteral("Start"));

  QCOMPARE(enter.count(), 0);
  QCOMPARE(escape.count(), 0);
  QCOMPARE(toggle.count(), 0);
}

void TestGamepadManager::button_whitespaceNameIgnored() {
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);
  QSignalSpy capture(m_mgr.get(), &GamepadManager::bindingCaptureButtonPressed);

  m_mgr->handleMappedButtonPress(QStringLiteral("   "));

  QCOMPARE(enter.count(), 0);
  QCOMPARE(capture.count(), 0);
}

void TestGamepadManager::button_droppedWhileSuspended() {
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);

  m_mgr->setSuspended(true);
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));

  QCOMPARE(enter.count(), 0);
}

void TestGamepadManager::button_droppedWhenShuttingDown() {
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);

  m_shutdown = true;
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));

  QCOMPARE(enter.count(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Binding-capture mode
// ─────────────────────────────────────────────────────────────────────────────

void TestGamepadManager::capture_buttonRoutedToCaptureSignalOnly() {
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);
  QSignalSpy capture(m_mgr.get(), &GamepadManager::bindingCaptureButtonPressed);

  m_mgr->beginBindingCapture();
  QVERIFY(m_mgr->isBindingCaptureActive());

  m_mgr->handleMappedButtonPress(QStringLiteral("  A  "));

  QCOMPARE(enter.count(), 0);
  QCOMPARE(capture.count(), 1);
  // Name arrives trimmed so the settings dialog stores a clean binding.
  QCOMPARE(capture.takeFirst().at(0).toString(), QStringLiteral("A"));
}

void TestGamepadManager::capture_beginStopsKeyboardRepeat() {
  m_kb.log.clear();

  m_mgr->beginBindingCapture();

  // Entering capture releases the key-down state and stops repeat with
  // recenter suppressed, so the capture dialog doesn't scroll the grid.
  QVERIFY(m_kb.log.contains(QStringLiteral("physDown(false)")));
  QVERIFY(m_kb.log.contains(QStringLiteral("stopRepeat(suppress)")));
}

void TestGamepadManager::capture_endClearsHeldDirection() {
  // Hold a direction, then begin capture: applyActiveDirection is gated
  // while capture is active, so the held direction survives until end.
  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Left);
  m_mgr->beginBindingCapture();

  QSignalSpy scrollStop(m_mgr.get(), &GamepadManager::requestScrollAnimationStop);
  m_kb.log.clear();

  m_mgr->endBindingCapture();
  QVERIFY(!m_mgr->isBindingCaptureActive());

  // Ending capture flushes the stale held direction back to None.
  QCOMPARE(scrollStop.count(), 1);
  QVERIFY(m_kb.log.contains(QStringLiteral("physDown(false)")));
}

// ─────────────────────────────────────────────────────────────────────────────
// applyActiveDirection
// ─────────────────────────────────────────────────────────────────────────────

void TestGamepadManager::direction_changeEmitsScrollStopMoveAndKeyboardSequence() {
  QSignalSpy scrollStop(m_mgr.get(), &GamepadManager::requestScrollAnimationStop);
  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);

  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Left);

  QCOMPARE(scrollStop.count(), 1);
  QCOMPARE(move.count(), 1);
  const auto args = move.takeFirst();
  QCOMPARE(args.at(0).toInt(), -1);
  QCOMPARE(args.at(1).toBool(), false);

  // Exact handshake: release + suppressed stop (direction change must not
  // recenter), then press to arm the repeat pipeline for the held input.
  const QStringList expected{QStringLiteral("physDown(false)"),
                             QStringLiteral("stopRepeat(suppress)"),
                             QStringLiteral("physDown(true)")};
  QCOMPARE(m_kb.log, expected);
}

void TestGamepadManager::direction_unchangedEmitsNothing() {
  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Right);

  QSignalSpy scrollStop(m_mgr.get(), &GamepadManager::requestScrollAnimationStop);
  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);
  m_kb.log.clear();

  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Right);

  QCOMPARE(scrollStop.count(), 0);
  QCOMPARE(move.count(), 0);
  QVERIFY(m_kb.log.isEmpty());
}

void TestGamepadManager::direction_toNoneReleasesKeyboardWithRecenter() {
  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Down);

  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);
  m_kb.log.clear();

  m_mgr->applyActiveDirection(GamepadHelpers::Direction::None);

  // Full release: no movement, and stopRepeat without the suppress flag so
  // the view may recenter like a keyboard key release.
  QCOMPARE(move.count(), 0);
  const QStringList expected{QStringLiteral("physDown(false)"),
                             QStringLiteral("stopRepeat(recenter)")};
  QCOMPARE(m_kb.log, expected);
}

void TestGamepadManager::direction_nullKeyboardManagerIsSafe() {
  // keyboardMgr() reads through ctx per call (Kartend-davi), so dropping the
  // sibling mid-run must degrade to signal-only dispatch, not crash.
  m_appCtx.managers.keyboardManager = nullptr;

  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);

  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Up);

  QCOMPARE(move.count(), 1);
  QVERIFY(m_kb.log.isEmpty());
}

// ─────────────────────────────────────────────────────────────────────────────
// updateDirectionFromInputs (backend-gated body)
// ─────────────────────────────────────────────────────────────────────────────

void TestGamepadManager::inputs_dpadLeftEmitsHorizontalMove() {
  if (!backendCompiledIn()) {
    QSKIP("No gamepad backend compiled in; updateDirectionFromInputs is a no-op");
  }

  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);

  m_mgr->m_left = true;
  m_mgr->updateDirectionFromInputs();

  QCOMPARE(move.count(), 1);
  const auto args = move.takeFirst();
  QCOMPARE(args.at(0).toInt(), -1);
  QCOMPARE(args.at(1).toBool(), false);
}

void TestGamepadManager::inputs_useDpadFalseSuppressesDpad() {
  if (!backendCompiledIn()) {
    QSKIP("No gamepad backend compiled in; updateDirectionFromInputs is a no-op");
  }

  m_settings.gamepad.gamepadUseDpad = false;
  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);

  m_mgr->m_left = true;
  m_mgr->updateDirectionFromInputs();

  QCOMPARE(move.count(), 0);
}

void TestGamepadManager::inputs_stickUpEmitsVerticalMove() {
  if (!backendCompiledIn()) {
    QSKIP("No gamepad backend compiled in; updateDirectionFromInputs is a no-op");
  }

  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);

  m_mgr->m_axisY = -1.0; // full deflection up, beyond AXIS_DEADZONE_ON
  m_mgr->updateDirectionFromInputs();

  QCOMPARE(move.count(), 1);
  const auto args = move.takeFirst();
  QCOMPARE(args.at(0).toInt(), -1);
  QCOMPARE(args.at(1).toBool(), true);
}

void TestGamepadManager::inputs_useStickFalseSuppressesStick() {
  if (!backendCompiledIn()) {
    QSKIP("No gamepad backend compiled in; updateDirectionFromInputs is a no-op");
  }

  m_settings.gamepad.gamepadUseLeftStick = false;
  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);

  m_mgr->m_axisY = -1.0;
  m_mgr->updateDirectionFromInputs();

  QCOMPARE(move.count(), 0);
}

void TestGamepadManager::inputs_suspendedProducesNoMove() {
  if (!backendCompiledIn()) {
    QSKIP("No gamepad backend compiled in; updateDirectionFromInputs is a no-op");
  }

  m_mgr->setSuspended(true);
  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);

  m_mgr->m_left = true;
  m_mgr->updateDirectionFromInputs();

  QCOMPARE(move.count(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// setSuspended
// ─────────────────────────────────────────────────────────────────────────────

void TestGamepadManager::suspend_clearsHeldDirectionAndReleasesKeyboard() {
  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Right);

  QSignalSpy scrollStop(m_mgr.get(), &GamepadManager::requestScrollAnimationStop);
  m_kb.log.clear();

  m_mgr->setSuspended(true);

  // Entering suspend drops the held direction so movement doesn't stick
  // under the launched program (Kartend-5rpt).
  QCOMPARE(scrollStop.count(), 1);
  const QStringList expected{QStringLiteral("physDown(false)"),
                             QStringLiteral("stopRepeat(recenter)")};
  QCOMPARE(m_kb.log, expected);
}

void TestGamepadManager::suspend_resumeRestoresButtonDispatch() {
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);

  m_mgr->setSuspended(true);
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));
  QCOMPARE(enter.count(), 0);

  m_mgr->setSuspended(false);
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));
  QCOMPARE(enter.count(), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Application-focus gate (Kartend-4nf4a)
// ─────────────────────────────────────────────────────────────────────────────

void TestGamepadManager::focus_seededFromApplicationState() {
  const GamepadManager fresh;
  QCOMPARE(fresh.m_appInactive, QGuiApplication::applicationState() != Qt::ApplicationActive);
}

void TestGamepadManager::focus_applicationSignalDrivesGate() {
  // Through the real QGuiApplication signal, not the private slot, so the
  // constructor's connection is what's under test.
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);

  emit qApp->applicationStateChanged(Qt::ApplicationInactive);
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));
  QCOMPARE(enter.count(), 0);

  emit qApp->applicationStateChanged(Qt::ApplicationActive);
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));
  QCOMPARE(enter.count(), 1);
}

void TestGamepadManager::focus_lostDropsButtonsDirectionsAndRightStick() {
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);
  QSignalSpy escape(m_mgr.get(), &GamepadManager::requestEscapeAction);
  QSignalSpy sidebar(m_mgr.get(), &GamepadManager::requestToggleSidebarAction);
  QSignalSpy move(m_mgr.get(), &GamepadManager::requestSelectionMove);
  QSignalSpy flick(m_mgr.get(), &GamepadManager::requestRightStickFlick);

  // Every state but Active blocks, not just Inactive.
  m_mgr->onApplicationStateChanged(Qt::ApplicationHidden);
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));
  m_mgr->handleMappedButtonPress(QStringLiteral("B"));
  m_mgr->handleMappedButtonPress(QStringLiteral("R1"));
  QCOMPARE(enter.count(), 0);
  QCOMPARE(escape.count(), 0);
  QCOMPARE(sidebar.count(), 0);

  if (backendCompiledIn()) {
    m_mgr->m_left = true;
    m_mgr->updateDirectionFromInputs();
    m_mgr->m_axisRightX = 1.0;
    m_mgr->updateRightStickSection();
    QCOMPARE(move.count(), 0);
    QCOMPARE(flick.count(), 0);
  }
}

void TestGamepadManager::focus_lostClearsHeldDirectionAndReleasesKeyboard() {
  m_mgr->applyActiveDirection(GamepadHelpers::Direction::Right);

  QSignalSpy scrollStop(m_mgr.get(), &GamepadManager::requestScrollAnimationStop);
  m_kb.log.clear();

  m_mgr->onApplicationStateChanged(Qt::ApplicationInactive);

  // Same drop as a launch suspension: no repeat keeps scrolling the frontend
  // underneath the window that took focus.
  QCOMPARE(scrollStop.count(), 1);
  const QStringList expected{QStringLiteral("physDown(false)"),
                             QStringLiteral("stopRepeat(recenter)")};
  QCOMPARE(m_kb.log, expected);
  QCOMPARE(m_mgr->m_activeDirection, GamepadHelpers::Direction::None);
}

void TestGamepadManager::focus_lostStopsRightStickPaneScroll() {
  if (!backendCompiledIn()) {
    QSKIP("No gamepad backend compiled in; updateRightStickSection is a no-op");
  }

  m_mgr->m_axisRightY = 1.0; // held down: starts the pane-scroll repeat
  m_mgr->updateRightStickSection();
  QVERIFY(m_mgr->m_rightStickScrollTimer);
  QVERIFY(m_mgr->m_rightStickScrollTimer->isActive());

  // updateRightStickSection is gated off while blocked, so the gate itself
  // has to stop the repeat.
  m_mgr->onApplicationStateChanged(Qt::ApplicationInactive);
  QVERIFY(!m_mgr->m_rightStickScrollTimer->isActive());
}

void TestGamepadManager::focus_heldDeflectionIsNoFreshFlickOnRefocus() {
  if (!backendCompiledIn()) {
    QSKIP("No gamepad backend compiled in; updateRightStickSection is a no-op");
  }

  QSignalSpy flick(m_mgr.get(), &GamepadManager::requestRightStickFlick);
  m_mgr->m_axisRightX = 1.0;
  m_mgr->updateRightStickSection();
  QCOMPARE(flick.count(), 1);

  // Still deflected across a focus round-trip: the same deflection, so no
  // second hop on the way back.
  m_mgr->onApplicationStateChanged(Qt::ApplicationInactive);
  m_mgr->updateRightStickSection();
  m_mgr->onApplicationStateChanged(Qt::ApplicationActive);
  m_mgr->updateRightStickSection();
  QCOMPARE(flick.count(), 1);
}

void TestGamepadManager::focus_lostDropsBindingCapture() {
  QSignalSpy capture(m_mgr.get(), &GamepadManager::bindingCaptureButtonPressed);
  m_mgr->beginBindingCapture();

  // A press made in the other window must not become the new binding.
  m_mgr->onApplicationStateChanged(Qt::ApplicationInactive);
  m_mgr->handleMappedButtonPress(QStringLiteral("X"));
  QCOMPARE(capture.count(), 0);

  m_mgr->onApplicationStateChanged(Qt::ApplicationActive);
  m_mgr->handleMappedButtonPress(QStringLiteral("X"));
  QCOMPARE(capture.count(), 1);
  QCOMPARE(capture.at(0).at(0).toString(), QStringLiteral("X"));
}

void TestGamepadManager::focus_handsModifierHudOverAtTheGate() {
  QSignalSpy hud(m_mgr.get(), &GamepadManager::modifierHeldChanged);

  // Nothing held: crossing the gate says nothing about the HUD.
  m_mgr->onApplicationStateChanged(Qt::ApplicationInactive);
  m_mgr->onApplicationStateChanged(Qt::ApplicationActive);
  QCOMPARE(hud.count(), 0);

  // Select held: the HUD hides on block (its release would arrive while
  // blocked and never reach it) and returns on refocus while still held.
  m_mgr->m_buttonBack = true;
  m_mgr->onApplicationStateChanged(Qt::ApplicationInactive);
  QCOMPARE(hud.count(), 1);
  QCOMPARE(hud.at(0).at(0).toBool(), false);
  m_mgr->onApplicationStateChanged(Qt::ApplicationActive);
  QCOMPARE(hud.count(), 2);
  QCOMPARE(hud.at(1).at(0).toBool(), true);
}

void TestGamepadManager::focus_composesWithSuspend() {
  QSignalSpy enter(m_mgr.get(), &GamepadManager::requestEnterAction);
  QSignalSpy hud(m_mgr.get(), &GamepadManager::modifierHeldChanged);
  m_mgr->m_buttonBack = true; // makes each real gate transition observable

  // Launch suspension first, then the launched program takes focus.
  m_mgr->setSuspended(true);
  m_mgr->onApplicationStateChanged(Qt::ApplicationInactive);
  QCOMPARE(hud.count(), 1); // one transition, not one per gate

  // Kartend regains focus while the tracked child still runs: stays blocked.
  m_mgr->onApplicationStateChanged(Qt::ApplicationActive);
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));
  QCOMPARE(enter.count(), 0);

  // Lifting the suspension while another window has focus: still blocked.
  m_mgr->onApplicationStateChanged(Qt::ApplicationInactive);
  m_mgr->setSuspended(false);
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));
  QCOMPARE(enter.count(), 0);
  QCOMPARE(hud.count(), 1);

  // Only with both gates clear does input reach the frontend again.
  m_mgr->onApplicationStateChanged(Qt::ApplicationActive);
  m_mgr->handleMappedButtonPress(QStringLiteral("A"));
  QCOMPARE(enter.count(), 1);
  QCOMPARE(hud.count(), 2);
}

QTEST_MAIN(TestGamepadManager)
#include "test_gamepadmanager.moc"
