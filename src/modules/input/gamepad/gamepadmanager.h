#ifndef GAMEPADMANAGER_H
#define GAMEPADMANAGER_H

#include "gamepadhelpers.h"
#include "setuputils.h"
#include <atomic>
#include <QObject>

QT_BEGIN_NAMESPACE
class QTimer;
#ifdef KARTEND_HAS_QT_GAMEPAD
class QGamepad;
class QGamepadManager;
#endif
QT_END_NAMESPACE

class IKeyboardManager;
#include "applicationcontext_fwd.h"
struct GeneralSettings;

struct GamepadManagerSetup {
  // Kartend-davi: keyboardManager moved out — read through `ctx` at the
  // call site (m_ctx->keyboardManager()) instead of caching a field. Stops
  // the manager from drifting if ApplicationContext::managers is rebound
  // mid-run.
  const ApplicationContext *ctx = nullptr;
  const GeneralSettings *generalSettings = nullptr;
  const bool *isShuttingDown = nullptr;

  SETUP_GETTER_DECL(const GeneralSettings *, GeneralSettings)
  SETUP_GETTER_DECL(const bool *, IsShuttingDown)
};

/**
 * @brief Translates gamepad input into the same high-level intents used by
 * KeyboardManager (selection move, enter, escape).
 *
 * When Qt Gamepad is not available at build time, this manager becomes a
 * no-op and emits nothing.
 */
class GamepadManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(GamepadManager)

  // Test access (Kartend-npoh2): the manager's input-translation layer
  // (updateDirectionFromInputs / applyActiveDirection /
  // handleMappedButtonPress) is driven by backend callbacks that need real
  // hardware; the test injects the same state/calls directly instead.
  friend class TestGamepadManager;

public:
  explicit GamepadManager(QObject *parent = nullptr);
  ~GamepadManager() override;

  void setupReferences(const GamepadManagerSetup &setup);

  void beginBindingCapture();
  void endBindingCapture();
  [[nodiscard]] bool isBindingCaptureActive() const { return m_bindingCaptureActive; }

  // Suspends gamepad input dispatch while a tracked child process is running,
  // mirroring AttractManager::setSuspended (Kartend-5rpt). Polling continues so
  // button edge-state stays synced (no phantom press on resume), but presses /
  // directions are not routed to the frontend. Entering suspend also clears any
  // held direction so movement doesn't stick under the launched program.
  void setSuspended(bool suspended);

signals:
  void requestSelectionMove(int direction, bool vertical);
  void requestEnterAction();
  void requestEscapeAction();
  void requestToggleSidebarAction();
  /// Toggle the collection tree panel (Kartend-ob1c9). Bound to
  /// GamepadSettings::gamepadToggleCollectionTreeButton (default unbound).
  void requestToggleCollectionTreeAction();
  void requestScrollAnimationStop();
  /// Select-held chord (user request 2026-08-17): while the Select/Back
  /// button is HELD, a direction moves the keyboard-focus SECTION
  /// (grid / toolbar / left / right sidebar) instead of the selection.
  /// dx/dy are -1/0/+1 in screen orientation.
  void requestFocusSectionMove(int dx, int dy);
  /// Select/Back held state — drives the on-screen modifier HUD (user
  /// request 2026-08-18: an indicator plus desaturated unfocused areas).
  void modifierHeldChanged(bool held);
  /// Right-stick FLICK, one per deflection. Kept separate from the chord
  /// signal so the interaction layer can let a visible details pane claim
  /// the vertical axis for scrolling (user request 2026-08-18).
  void requestRightStickFlick(int dx, int dy);
  /// Continuous while the right stick is deflected vertically: scroll the
  /// details pane by @p steps notches (sign = direction).
  void requestPaneScroll(int steps);

  void bindingCaptureButtonPressed(const QString &buttonName);

private:
  using Direction = GamepadHelpers::Direction;

  void updateDirectionFromInputs();
  /// Edge-detects the right stick into requestFocusSectionMove — shared by
  /// both backends, gated by GamepadSettings::gamepadRightStickSections.
  void updateRightStickSection();
  void applyActiveDirection(Direction newDirection);
  void handleMappedButtonPress(const QString &buttonName);

  [[nodiscard]] bool shuttingDown() const;

  // Kartend-davi: read through the app context rather than caching the
  // sibling pointer. Stops the manager from drifting out of sync if
  // ApplicationContext::managers.keyboardManager is rebound mid-run.
  [[nodiscard]] IKeyboardManager *keyboardMgr() const;

  const ApplicationContext *m_ctx = nullptr;
  const GeneralSettings *m_generalSettings = nullptr;
  const bool *m_isShuttingDown = nullptr;

  // Current digital input state (D-pad OR derived from left stick).
  // Threading invariant: today both Qt6Gamepad and SDL2 paths write these on
  // the main thread (Qt signals delivered to main; SDL polled by a main-thread
  // QTimer), and updateDirectionFromInputs() reads them only from the same
  // path. std::atomic with seq_cst ops guards against a future move of SDL
  // polling onto a dedicated thread, where plain `bool` would be a data race.
  std::atomic<bool> m_up{false};
  std::atomic<bool> m_down{false};
  std::atomic<bool> m_left{false};
  std::atomic<bool> m_right{false};

  // Left stick axes.
  double m_axisX = 0.0;
  double m_axisY = 0.0;

  // Right stick axes + its own hysteresis state (user request 2026-08-17:
  // a right-stick FLICK hops the focus section — no modifier needed; one
  // hop per deflection, recentre to hop again).
  double m_axisRightX = 0.0;
  double m_axisRightY = 0.0;
  Direction m_rightStickDirection = Direction::None;
  /// Drives requestPaneScroll while the right stick is held off-centre:
  /// the Qt backend only reports axis CHANGES, so a held deflection would
  /// otherwise scroll exactly once.
  QTimer *m_rightStickScrollTimer = nullptr;

  // Digital buttons (used for edge detection in polling backends).
  bool m_buttonA = false;
  bool m_buttonB = false;
  bool m_buttonX = false;
  bool m_buttonY = false;
  bool m_buttonBack = false;
  bool m_buttonStart = false;
  bool m_buttonGuide = false;
  bool m_buttonL1 = false;
  bool m_buttonR1 = false;
  bool m_buttonL3 = false;
  bool m_buttonR3 = false;

  bool m_bindingCaptureActive = false;
  bool m_suspended = false;

  Direction m_activeDirection = Direction::None;

#ifdef KARTEND_HAS_QT_GAMEPAD
  // QGamepad / QGamepadManager are forward-declared at namespace scope above:
  // declaring them here would introduce nested GamepadManager::QGamepad types
  // that shadow the real Qt classes in every member-function body.
  void attachToFirstConnectedGamepad();
  void attachToGamepad(int deviceId);
  void detachGamepad();

  void onDpadUpChanged(bool pressed);
  void onDpadDownChanged(bool pressed);
  void onDpadLeftChanged(bool pressed);
  void onDpadRightChanged(bool pressed);

  void onAxisLeftXChanged(double value);
  void onAxisLeftYChanged(double value);
  void onAxisRightXChanged(double value);
  void onAxisRightYChanged(double value);

  void onButtonAChanged(bool pressed);
  void onButtonBChanged(bool pressed);
  void onButtonXChanged(bool pressed);
  void onButtonYChanged(bool pressed);
  void onButtonL1Changed(bool pressed);
  void onButtonR1Changed(bool pressed);
  void onButtonSelectChanged(bool pressed);
  void onButtonStartChanged(bool pressed);
  void onButtonGuideChanged(bool pressed);
  void onButtonL3Changed(bool pressed);
  void onButtonR3Changed(bool pressed);

  QGamepadManager *m_manager = nullptr;
  QGamepad *m_gamepad = nullptr;
  int m_deviceId = -1;
#endif

#ifdef KARTEND_HAS_SDL2_GAMEPAD
  void attachToFirstConnectedController();
  void detachController();
  void pollSdlState();

  QTimer *m_pollTimer = nullptr;
  void *m_controller = nullptr;
#endif
};

#endif // GAMEPADMANAGER_H
