/**
 * @file test_gamepadmanager.cpp
 * @brief Unit tests for GamepadManager's input-translation layer (Kartend-npoh2).
 *
 * test_gamepadhelpers covers the pure input math (axis hysteresis, direction
 * combine, button-action resolution). These tests cover the manager logic
 * around it: settings-driven button→action dispatch, binding-capture mode,
 * suspend gating, the keyboard-repeat handshake on direction changes, and
 * the dpad/stick→signal pipeline. The backend callbacks (Qt6 Gamepad signals
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
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#if defined(KARTEND_HAS_SDL2_GAMEPAD)
#include <QTimer>
#endif

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
  QSignalSpy toggle(m_mgr.get(), &GamepadManager::requestToggleSidebarAction);

  m_mgr->handleMappedButtonPress(QStringLiteral("Y"));

  QCOMPARE(toggle.count(), 1);
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

QTEST_MAIN(TestGamepadManager)
#include "test_gamepadmanager.moc"
