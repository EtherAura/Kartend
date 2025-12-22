#include "gamepadmanager.h"
#include "keyboardmanager.h"
#include "uiconstants.h"

#include <QApplication>

#ifdef KARTEND_HAS_SDL2_GAMEPAD
#include <QTimer>
#include <SDL2/SDL.h>
#endif

#ifdef KARTEND_HAS_QT_GAMEPAD
#include <QtGamepad/QGamepad>
#include <QtGamepad/QGamepadManager>
#endif

// GamepadManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(GamepadManagerSetup, KeyboardManager*, KeyboardManager, keyboardManager)
SETUP_GETTER_DEF_SAME(GamepadManagerSetup, const GeneralSettings*, GeneralSettings, generalSettings)
SETUP_GETTER_DEF_SAME(GamepadManagerSetup, const bool*, IsShuttingDown, isShuttingDown)

GamepadManager::GamepadManager(QObject *parent) : QObject(parent) {
#ifdef KARTEND_HAS_QT_GAMEPAD
  m_manager = QGamepadManager::instance();
  if (m_manager) {
    connect(m_manager, &QGamepadManager::gamepadConnected, this,
            [this](int deviceId) {
              if (!m_gamepad) {
                attachToGamepad(deviceId);
              }
            });
    connect(m_manager, &QGamepadManager::gamepadDisconnected, this,
            [this](int deviceId) {
              if (deviceId == m_deviceId) {
                detachGamepad();
              }
            });
  }
#endif

#ifdef KARTEND_HAS_SDL2_GAMEPAD
  // SDL gamecontroller polling: keeps implementation dependency-free from
  // the rest of the app and avoids an additional event filter path.
  if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) {
    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(false);
    connect(m_pollTimer, &QTimer::timeout, this, &GamepadManager::pollSdlState);
    m_pollTimer->start(UIConstants::Gamepad::POLL_INTERVAL_MS);
  }
#endif
}

GamepadManager::~GamepadManager() {
#ifdef KARTEND_HAS_QT_GAMEPAD
  detachGamepad();
#endif

#ifdef KARTEND_HAS_SDL2_GAMEPAD
  detachController();
  if (m_pollTimer) {
    m_pollTimer->stop();
  }
  SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
#endif
}

void GamepadManager::setupReferences(const GamepadManagerSetup &setup) {
  m_keyboardManager = setup.getKeyboardManager();
  m_generalSettings = setup.getGeneralSettings();
  m_isShuttingDown = setup.getIsShuttingDown();

#ifdef KARTEND_HAS_QT_GAMEPAD
  attachToFirstConnectedGamepad();
#endif

#ifdef KARTEND_HAS_SDL2_GAMEPAD
  attachToFirstConnectedController();
#endif
}

void GamepadManager::beginBindingCapture() {
  if (m_bindingCaptureActive) {
    return;
  }

  m_bindingCaptureActive = true;

  // Stop any in-flight direction/repeat so the capture doesn't trigger actions.
  applyActiveDirection(Direction::None);
  if (m_keyboardManager) {
    m_keyboardManager->setPhysicalKeyDown(false);
    m_keyboardManager->stopRepeat(true);
  }
}

void GamepadManager::endBindingCapture() {
  if (!m_bindingCaptureActive) {
    return;
  }

  m_bindingCaptureActive = false;
  applyActiveDirection(Direction::None);
}

bool GamepadManager::shuttingDown() const {
  return QApplication::closingDown() || (m_isShuttingDown && *m_isShuttingDown);
}

void GamepadManager::updateDirectionFromInputs() {
#if defined(KARTEND_HAS_QT_GAMEPAD) || defined(KARTEND_HAS_SDL2_GAMEPAD)
  if (shuttingDown()) {
    return;
  }

  if (m_bindingCaptureActive) {
    return;
  }

  const bool useDpad = !m_generalSettings || m_generalSettings->gamepadUseDpad;
  const bool useStick =
      !m_generalSettings || m_generalSettings->gamepadUseLeftStick;

  // Convert left stick axes to digital directions with hysteresis.
  constexpr double kOn = UIConstants::Gamepad::AXIS_DEADZONE_ON;
  constexpr double kOff = UIConstants::Gamepad::AXIS_DEADZONE_OFF;

  const double x = m_axisX;
  const double y = m_axisY;

  const bool prevLeft = (m_activeDirection == Direction::Left);
  const bool prevRight = (m_activeDirection == Direction::Right);
  const bool prevUp = (m_activeDirection == Direction::Up);
  const bool prevDown = (m_activeDirection == Direction::Down);

    const bool stickLeft =
        useStick && ((x <= -kOn) || (prevLeft && x <= -kOff));
    const bool stickRight =
        useStick && ((x >= kOn) || (prevRight && x >= kOff));
      const bool stickUp = useStick && ((y <= -kOn) || (prevUp && y <= -kOff));
      const bool stickDown = useStick && ((y >= kOn) || (prevDown && y >= kOff));

    const bool dpadLeft = useDpad && m_left;
    const bool dpadRight = useDpad && m_right;
    const bool dpadUp = useDpad && m_up;
    const bool dpadDown = useDpad && m_down;

  // D-pad states are already included in m_*; incorporate stick-derived states.
  const bool combinedLeft = dpadLeft || stickLeft;
  const bool combinedRight = dpadRight || stickRight;
  const bool combinedUp = dpadUp || stickUp;
  const bool combinedDown = dpadDown || stickDown;

  Direction newDirection = Direction::None;

  // Prefer vertical when both are pressed (matches typical grid nav feel).
  if (combinedUp) {
    newDirection = Direction::Up;
  } else if (combinedDown) {
    newDirection = Direction::Down;
  } else if (combinedLeft) {
    newDirection = Direction::Left;
  } else if (combinedRight) {
    newDirection = Direction::Right;
  }

  applyActiveDirection(newDirection);
#endif
}

void GamepadManager::applyActiveDirection(Direction newDirection) {
  if (m_bindingCaptureActive) {
    return;
  }

  if (newDirection == m_activeDirection) {
    return;
  }

  // Direction changed: stop any in-flight scroll animations so we don't apply
  // stale motion across view rebuilds or after input source changes.
  emit requestScrollAnimationStop();

  m_activeDirection = newDirection;

  if (newDirection == Direction::None) {
    if (m_keyboardManager) {
      m_keyboardManager->setPhysicalKeyDown(false);
      m_keyboardManager->stopRepeat(false);
    }
    return;
  }

  // Changing direction should not trigger a recenter like a full release does.
  if (m_keyboardManager) {
    m_keyboardManager->setPhysicalKeyDown(false);
    m_keyboardManager->stopRepeat(true);
  }

  // Start/restart keyboard repeat pipeline for held input.
  if (m_keyboardManager) {
    m_keyboardManager->setPhysicalKeyDown(true);
  }

  int delta = 0;
  bool vertical = false;
  switch (newDirection) {
  case Direction::Left:
    delta = -1;
    vertical = false;
    break;
  case Direction::Right:
    delta = 1;
    vertical = false;
    break;
  case Direction::Up:
    delta = -1;
    vertical = true;
    break;
  case Direction::Down:
    delta = 1;
    vertical = true;
    break;
  case Direction::None:
    break;
  }

  if (delta != 0) {
    emit requestSelectionMove(delta, vertical);
  }
}

void GamepadManager::handleMappedButtonPress(const QString &buttonName) {
  if (shuttingDown()) {
    return;
  }

  const QString normalized = buttonName.trimmed();
  if (normalized.isEmpty()) {
    return;
  }

  if (m_bindingCaptureActive) {
    emit bindingCaptureButtonPressed(normalized);
    return;
  }

  const QString confirm =
      m_generalSettings ? m_generalSettings->gamepadConfirmButton
                        : QStringLiteral("A");
  const QString back = m_generalSettings ? m_generalSettings->gamepadBackButton
                                         : QStringLiteral("B");
  const QString toggleSidebar =
      m_generalSettings ? m_generalSettings->gamepadToggleSidebarButton
                        : QStringLiteral("Y");

  auto matches = [&normalized](const QString &configured) -> bool {
    const QString c = configured.trimmed();
    return !c.isEmpty() && (c.compare(normalized, Qt::CaseInsensitive) == 0);
  };

  // Priority: confirm/back first to avoid accidental side effects if a user
  // binds multiple actions to the same physical button.
  if (matches(confirm)) {
    emit requestEnterAction();
    return;
  }
  if (matches(back)) {
    emit requestEscapeAction();
    return;
  }
  if (matches(toggleSidebar)) {
    emit requestToggleSidebarAction();
  }
}

#ifdef KARTEND_HAS_QT_GAMEPAD

void GamepadManager::attachToFirstConnectedGamepad() {
  if (!m_manager || m_gamepad || shuttingDown()) {
    return;
  }

  const QList<int> connected = m_manager->connectedGamepads();
  if (!connected.isEmpty()) {
    attachToGamepad(connected.first());
  }
}

void GamepadManager::attachToGamepad(int deviceId) {
  if (m_gamepad || shuttingDown()) {
    return;
  }

  m_deviceId = deviceId;
  m_gamepad = new QGamepad(deviceId, this);

  connect(m_gamepad, &QGamepad::buttonUpChanged, this,
          &GamepadManager::onDpadUpChanged);
  connect(m_gamepad, &QGamepad::buttonDownChanged, this,
          &GamepadManager::onDpadDownChanged);
  connect(m_gamepad, &QGamepad::buttonLeftChanged, this,
          &GamepadManager::onDpadLeftChanged);
  connect(m_gamepad, &QGamepad::buttonRightChanged, this,
          &GamepadManager::onDpadRightChanged);

  connect(m_gamepad, &QGamepad::axisLeftXChanged, this,
          &GamepadManager::onAxisLeftXChanged);
  connect(m_gamepad, &QGamepad::axisLeftYChanged, this,
          &GamepadManager::onAxisLeftYChanged);

  connect(m_gamepad, &QGamepad::buttonAChanged, this,
          &GamepadManager::onButtonAChanged);
  connect(m_gamepad, &QGamepad::buttonBChanged, this,
          &GamepadManager::onButtonBChanged);

    // Additional buttons (for mapping/capture). These are widely supported in Qt6.
    connect(m_gamepad, &QGamepad::buttonXChanged, this,
      &GamepadManager::onButtonXChanged);
    connect(m_gamepad, &QGamepad::buttonYChanged, this,
      &GamepadManager::onButtonYChanged);
    connect(m_gamepad, &QGamepad::buttonL1Changed, this,
      &GamepadManager::onButtonL1Changed);
    connect(m_gamepad, &QGamepad::buttonR1Changed, this,
      &GamepadManager::onButtonR1Changed);
    connect(m_gamepad, &QGamepad::buttonSelectChanged, this,
      &GamepadManager::onButtonSelectChanged);
    connect(m_gamepad, &QGamepad::buttonStartChanged, this,
      &GamepadManager::onButtonStartChanged);
    connect(m_gamepad, &QGamepad::buttonGuideChanged, this,
      &GamepadManager::onButtonGuideChanged);
    connect(m_gamepad, &QGamepad::buttonL3Changed, this,
      &GamepadManager::onButtonL3Changed);
    connect(m_gamepad, &QGamepad::buttonR3Changed, this,
      &GamepadManager::onButtonR3Changed);
}

void GamepadManager::detachGamepad() {
  if (m_keyboardManager) {
    m_keyboardManager->setPhysicalKeyDown(false);
    m_keyboardManager->stopRepeat(true);
  }

  m_activeDirection = Direction::None;
  m_up = false;
  m_down = false;
  m_left = false;
  m_right = false;
  m_axisX = 0.0;
  m_axisY = 0.0;

  m_deviceId = -1;

  if (m_gamepad) {
    m_gamepad->deleteLater();
    m_gamepad = nullptr;
  }
}

void GamepadManager::onDpadUpChanged(bool pressed) {
  m_up = pressed;
  updateDirectionFromInputs();
}

void GamepadManager::onDpadDownChanged(bool pressed) {
  m_down = pressed;
  updateDirectionFromInputs();
}

void GamepadManager::onDpadLeftChanged(bool pressed) {
  m_left = pressed;
  updateDirectionFromInputs();
}

void GamepadManager::onDpadRightChanged(bool pressed) {
  m_right = pressed;
  updateDirectionFromInputs();
}

void GamepadManager::onAxisLeftXChanged(double value) {
  m_axisX = value;
  updateDirectionFromInputs();
}

void GamepadManager::onAxisLeftYChanged(double value) {
  m_axisY = value;
  updateDirectionFromInputs();
}

void GamepadManager::onButtonAChanged(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }

  handleMappedButtonPress(QStringLiteral("A"));
}

void GamepadManager::onButtonBChanged(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }

  handleMappedButtonPress(QStringLiteral("B"));
}

void GamepadManager::onButtonXChanged(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }
  handleMappedButtonPress(QStringLiteral("X"));
}

void GamepadManager::onButtonYChanged(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }
  handleMappedButtonPress(QStringLiteral("Y"));
}

void GamepadManager::onButtonL1Changed(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }
  handleMappedButtonPress(QStringLiteral("L1"));
}

void GamepadManager::onButtonR1Changed(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }
  handleMappedButtonPress(QStringLiteral("R1"));
}

void GamepadManager::onButtonSelectChanged(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }
  handleMappedButtonPress(QStringLiteral("Back"));
}

void GamepadManager::onButtonStartChanged(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }
  handleMappedButtonPress(QStringLiteral("Start"));
}

void GamepadManager::onButtonGuideChanged(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }
  handleMappedButtonPress(QStringLiteral("Guide"));
}

void GamepadManager::onButtonL3Changed(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }
  handleMappedButtonPress(QStringLiteral("L3"));
}

void GamepadManager::onButtonR3Changed(bool pressed) {
  if (!pressed || shuttingDown()) {
    return;
  }
  handleMappedButtonPress(QStringLiteral("R3"));
}

#endif

#ifdef KARTEND_HAS_SDL2_GAMEPAD

void GamepadManager::attachToFirstConnectedController() {
  if (m_controller || shuttingDown()) {
    return;
  }

  const int numJoysticks = SDL_NumJoysticks();
  for (int i = 0; i < numJoysticks; ++i) {
    if (SDL_IsGameController(i) == SDL_TRUE) {
      SDL_GameController *controller = SDL_GameControllerOpen(i);
      if (controller) {
        m_controller = controller;
        break;
      }
    }
  }
}

void GamepadManager::detachController() {
  if (m_keyboardManager) {
    m_keyboardManager->setPhysicalKeyDown(false);
    m_keyboardManager->stopRepeat(true);
  }

  m_activeDirection = Direction::None;
  m_up = false;
  m_down = false;
  m_left = false;
  m_right = false;
  m_axisX = 0.0;
  m_axisY = 0.0;
  m_buttonA = false;
  m_buttonB = false;
  m_buttonX = false;
  m_buttonY = false;
  m_buttonBack = false;
  m_buttonStart = false;
  m_buttonGuide = false;
  m_buttonL1 = false;
  m_buttonR1 = false;
  m_buttonL3 = false;
  m_buttonR3 = false;

  if (m_controller) {
    SDL_GameControllerClose(static_cast<SDL_GameController*>(m_controller));
    m_controller = nullptr;
  }
}

void GamepadManager::pollSdlState() {
  if (shuttingDown()) {
    return;
  }

  if (!m_controller) {
    attachToFirstConnectedController();
    if (!m_controller) {
      // No controller connected - use slow polling to reduce idle CPU usage
      if (m_pollTimer && m_pollTimer->interval() != UIConstants::Gamepad::POLL_INTERVAL_IDLE_MS) {
        m_pollTimer->setInterval(UIConstants::Gamepad::POLL_INTERVAL_IDLE_MS);
      }
      return;
    }
    // Controller just connected - switch to fast polling
    if (m_pollTimer && m_pollTimer->interval() != UIConstants::Gamepad::POLL_INTERVAL_MS) {
      m_pollTimer->setInterval(UIConstants::Gamepad::POLL_INTERVAL_MS);
    }
  }

    SDL_GameController *controller = static_cast<SDL_GameController*>(m_controller);
    SDL_GameControllerUpdate();

  const Sint16 rawX =
      SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
  const Sint16 rawY =
      SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);

  // Normalize to [-1, 1].
  auto normalize = [](Sint16 v) -> double {
    if (v >= 0) {
      return static_cast<double>(v) / 32767.0;
    }
    return static_cast<double>(v) / 32768.0;
  };

  m_axisX = normalize(rawX);
  m_axisY = normalize(rawY);

    m_up = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP);
    m_down = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    m_left = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    m_right = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

  const bool aNow = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A);
  const bool bNow = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B);
  const bool xNow = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X);
  const bool yNow = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y);
  const bool backNow = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);
  const bool startNow = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START);
  const bool guideNow = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_GUIDE);
  const bool l1Now = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
  const bool r1Now = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
  const bool l3Now = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK);
  const bool r3Now = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK);

  const bool pressA = aNow && !m_buttonA;
  const bool pressB = bNow && !m_buttonB;
  const bool pressX = xNow && !m_buttonX;
  const bool pressY = yNow && !m_buttonY;
  const bool pressBack = backNow && !m_buttonBack;
  const bool pressStart = startNow && !m_buttonStart;
  const bool pressGuide = guideNow && !m_buttonGuide;
  const bool pressL1 = l1Now && !m_buttonL1;
  const bool pressR1 = r1Now && !m_buttonR1;
  const bool pressL3 = l3Now && !m_buttonL3;
  const bool pressR3 = r3Now && !m_buttonR3;

  if (m_bindingCaptureActive) {
    if (pressA) {
      emit bindingCaptureButtonPressed(QStringLiteral("A"));
    }
    if (pressB) {
      emit bindingCaptureButtonPressed(QStringLiteral("B"));
    }
    if (pressX) {
      emit bindingCaptureButtonPressed(QStringLiteral("X"));
    }
    if (pressY) {
      emit bindingCaptureButtonPressed(QStringLiteral("Y"));
    }
    if (pressBack) {
      emit bindingCaptureButtonPressed(QStringLiteral("Back"));
    }
    if (pressStart) {
      emit bindingCaptureButtonPressed(QStringLiteral("Start"));
    }
    if (pressGuide) {
      emit bindingCaptureButtonPressed(QStringLiteral("Guide"));
    }
    if (pressL1) {
      emit bindingCaptureButtonPressed(QStringLiteral("L1"));
    }
    if (pressR1) {
      emit bindingCaptureButtonPressed(QStringLiteral("R1"));
    }
    if (pressL3) {
      emit bindingCaptureButtonPressed(QStringLiteral("L3"));
    }
    if (pressR3) {
      emit bindingCaptureButtonPressed(QStringLiteral("R3"));
    }

    m_buttonA = aNow;
    m_buttonB = bNow;
    m_buttonX = xNow;
    m_buttonY = yNow;
    m_buttonBack = backNow;
    m_buttonStart = startNow;
    m_buttonGuide = guideNow;
    m_buttonL1 = l1Now;
    m_buttonR1 = r1Now;
    m_buttonL3 = l3Now;
    m_buttonR3 = r3Now;
    return;
  }

  if (pressA) {
    handleMappedButtonPress(QStringLiteral("A"));
  }
  if (pressB) {
    handleMappedButtonPress(QStringLiteral("B"));
  }
  if (pressX) {
    handleMappedButtonPress(QStringLiteral("X"));
  }
  if (pressY) {
    handleMappedButtonPress(QStringLiteral("Y"));
  }
  if (pressBack) {
    handleMappedButtonPress(QStringLiteral("Back"));
  }
  if (pressStart) {
    handleMappedButtonPress(QStringLiteral("Start"));
  }
  if (pressGuide) {
    handleMappedButtonPress(QStringLiteral("Guide"));
  }
  if (pressL1) {
    handleMappedButtonPress(QStringLiteral("L1"));
  }
  if (pressR1) {
    handleMappedButtonPress(QStringLiteral("R1"));
  }
  if (pressL3) {
    handleMappedButtonPress(QStringLiteral("L3"));
  }
  if (pressR3) {
    handleMappedButtonPress(QStringLiteral("R3"));
  }

  m_buttonA = aNow;
  m_buttonB = bNow;
  m_buttonX = xNow;
  m_buttonY = yNow;
  m_buttonBack = backNow;
  m_buttonStart = startNow;
  m_buttonGuide = guideNow;
  m_buttonL1 = l1Now;
  m_buttonR1 = r1Now;
  m_buttonL3 = l3Now;
  m_buttonR3 = r3Now;

  updateDirectionFromInputs();
}

#endif
