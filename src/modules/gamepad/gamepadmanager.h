#ifndef GAMEPADMANAGER_H
#define GAMEPADMANAGER_H

#include "setuputils.h"
#include <QObject>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class KeyboardManager;
struct ApplicationContext;
struct GeneralSettings;

struct GamepadManagerSetup {
  const ApplicationContext *ctx = nullptr;
  KeyboardManager *keyboardManager = nullptr;
  const GeneralSettings *generalSettings = nullptr;
  const bool *isShuttingDown = nullptr;

  SETUP_GETTER_DECL(KeyboardManager *, KeyboardManager)
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

public:
  explicit GamepadManager(QObject *parent = nullptr);
  ~GamepadManager() override;

  void setupReferences(const GamepadManagerSetup &setup);

  void beginBindingCapture();
  void endBindingCapture();
  [[nodiscard]] bool isBindingCaptureActive() const {
    return m_bindingCaptureActive;
  }

signals:
  void requestSelectionMove(int direction, bool vertical);
  void requestEnterAction();
  void requestEscapeAction();
  void requestToggleSidebarAction();
  void requestScrollAnimationStop();

  void bindingCaptureButtonPressed(const QString &buttonName);

private:
  enum class Direction { None, Left, Right, Up, Down };

  void updateDirectionFromInputs();
  void applyActiveDirection(Direction newDirection);
  void handleMappedButtonPress(const QString &buttonName);

  [[nodiscard]] bool shuttingDown() const;

  KeyboardManager *m_keyboardManager = nullptr;
  const GeneralSettings *m_generalSettings = nullptr;
  const bool *m_isShuttingDown = nullptr;

  // Current digital input state (D-pad OR derived from left stick).
  bool m_up = false;
  bool m_down = false;
  bool m_left = false;
  bool m_right = false;

  // Left stick axes.
  double m_axisX = 0.0;
  double m_axisY = 0.0;

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

  Direction m_activeDirection = Direction::None;

#ifdef KARTEND_HAS_QT_GAMEPAD
  class QGamepad;
  class QGamepadManager;

  void attachToFirstConnectedGamepad();
  void attachToGamepad(int deviceId);
  void detachGamepad();

  void onDpadUpChanged(bool pressed);
  void onDpadDownChanged(bool pressed);
  void onDpadLeftChanged(bool pressed);
  void onDpadRightChanged(bool pressed);

  void onAxisLeftXChanged(double value);
  void onAxisLeftYChanged(double value);

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
