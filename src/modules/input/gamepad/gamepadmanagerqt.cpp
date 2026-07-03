// Sibling TU: Qt6::Gamepad backend (attach/detach + input slots) for
// GamepadManager.
#include "gamepadmanager.h"
#include "ikeyboardmanager.h"

#include <QApplication>

#ifdef KARTEND_HAS_QT_GAMEPAD
#include <QtGamepad/QGamepad>
#include <QtGamepad/QGamepadManager>
#endif

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

  connect(m_gamepad, &QGamepad::buttonUpChanged, this, &GamepadManager::onDpadUpChanged);
  connect(m_gamepad, &QGamepad::buttonDownChanged, this, &GamepadManager::onDpadDownChanged);
  connect(m_gamepad, &QGamepad::buttonLeftChanged, this, &GamepadManager::onDpadLeftChanged);
  connect(m_gamepad, &QGamepad::buttonRightChanged, this, &GamepadManager::onDpadRightChanged);

  connect(m_gamepad, &QGamepad::axisLeftXChanged, this, &GamepadManager::onAxisLeftXChanged);
  connect(m_gamepad, &QGamepad::axisLeftYChanged, this, &GamepadManager::onAxisLeftYChanged);

  connect(m_gamepad, &QGamepad::buttonAChanged, this, &GamepadManager::onButtonAChanged);
  connect(m_gamepad, &QGamepad::buttonBChanged, this, &GamepadManager::onButtonBChanged);

  // Additional buttons (for mapping/capture). These are widely supported in
  // Qt6.
  connect(m_gamepad, &QGamepad::buttonXChanged, this, &GamepadManager::onButtonXChanged);
  connect(m_gamepad, &QGamepad::buttonYChanged, this, &GamepadManager::onButtonYChanged);
  connect(m_gamepad, &QGamepad::buttonL1Changed, this, &GamepadManager::onButtonL1Changed);
  connect(m_gamepad, &QGamepad::buttonR1Changed, this, &GamepadManager::onButtonR1Changed);
  connect(m_gamepad, &QGamepad::buttonSelectChanged, this, &GamepadManager::onButtonSelectChanged);
  connect(m_gamepad, &QGamepad::buttonStartChanged, this, &GamepadManager::onButtonStartChanged);
  connect(m_gamepad, &QGamepad::buttonGuideChanged, this, &GamepadManager::onButtonGuideChanged);
  connect(m_gamepad, &QGamepad::buttonL3Changed, this, &GamepadManager::onButtonL3Changed);
  connect(m_gamepad, &QGamepad::buttonR3Changed, this, &GamepadManager::onButtonR3Changed);
}

void GamepadManager::detachGamepad() {
  // See detachController() for why keyboard interactions are skipped during
  // shutdown.
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
