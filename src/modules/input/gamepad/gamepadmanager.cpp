#include "gamepadmanager.h"
#include "applicationcontext.h"
#include "ikeyboardmanager.h"
#include "uiconstants/gamepad.h"

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
SETUP_GETTER_DEF_COL_SAME(GamepadManagerSetup, const GeneralSettings *, GeneralSettings,
                          generalSettings)
SETUP_GETTER_DEF_COL_SAME(GamepadManagerSetup, const bool *, IsShuttingDown, isShuttingDown)

IKeyboardManager *GamepadManager::keyboardMgr() const {
  return m_ctx ? m_ctx->keyboardManager() : nullptr;
}

GamepadManager::GamepadManager(QObject *parent) : QObject(parent) {
#ifdef KARTEND_HAS_QT_GAMEPAD
  m_manager = QGamepadManager::instance();
  if (m_manager) {
    connect(m_manager, &QGamepadManager::gamepadConnected, this, [this](int deviceId) {
      if (!m_gamepad) {
        attachToGamepad(deviceId);
      }
    });
    connect(m_manager, &QGamepadManager::gamepadDisconnected, this, [this](int deviceId) {
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
  m_ctx = setup.ctx;
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
  if (auto *kb = keyboardMgr()) {
    kb->setPhysicalKeyDown(false);
    kb->stopRepeat(true);
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
  // Skip keyboard interactions during shutdown: KeyboardManager::stopRepeat
  // emits a signal whose slot reaches sibling managers (m_arrowHandler) that
  // may already be destroyed in InteractionManager's reverse-declaration
  // teardown — calling it here would re-enter freed memory.
  if (auto *kb = keyboardMgr(); kb && !shuttingDown()) {
    kb->setPhysicalKeyDown(false);
    kb->stopRepeat(true);
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
    SDL_GameControllerClose(static_cast<SDL_GameController *>(m_controller));
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

  SDL_GameController *controller = static_cast<SDL_GameController *>(m_controller);
  SDL_GameControllerUpdate();

  const Sint16 rawX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
  const Sint16 rawY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);

  m_axisX = GamepadHelpers::normalizeSdlAxis(rawX);
  m_axisY = GamepadHelpers::normalizeSdlAxis(rawY);

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
