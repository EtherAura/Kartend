// Direction + input handling cluster split out from gamepadmanager.cpp.
#include "gamepadmanager.h"
#include "ikeyboardmanager.h"
#include "uiconstants.h"

#include <QApplication>

void GamepadManager::updateDirectionFromInputs() {
#if defined(KARTEND_HAS_QT_GAMEPAD) || defined(KARTEND_HAS_SDL2_GAMEPAD)
  if (shuttingDown()) {
    return;
  }

  if (m_bindingCaptureActive) {
    return;
  }

  const bool useDpad = !m_generalSettings || m_generalSettings->gamepadUseDpad;
  const bool useStick = !m_generalSettings || m_generalSettings->gamepadUseLeftStick;

  const auto stick = GamepadHelpers::axisToDirections(
      m_axisX, m_axisY, m_activeDirection, UIConstants::Gamepad::AXIS_DEADZONE_ON,
      UIConstants::Gamepad::AXIS_DEADZONE_OFF, useStick);

  const Direction newDirection =
      GamepadHelpers::combineToDirection(m_left, m_right, m_up, m_down, useDpad, stick);

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

  const auto movement = GamepadHelpers::movementFor(newDirection);
  if (movement.delta != 0) {
    emit requestSelectionMove(movement.delta, movement.vertical);
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
      m_generalSettings ? m_generalSettings->gamepadConfirmButton : QStringLiteral("A");
  const QString back =
      m_generalSettings ? m_generalSettings->gamepadBackButton : QStringLiteral("B");
  const QString toggleSidebar =
      m_generalSettings ? m_generalSettings->gamepadToggleSidebarButton : QStringLiteral("Y");

  switch (GamepadHelpers::resolveButtonAction(normalized, confirm, back, toggleSidebar)) {
  case GamepadHelpers::ButtonAction::Confirm:
    emit requestEnterAction();
    return;
  case GamepadHelpers::ButtonAction::Back:
    emit requestEscapeAction();
    return;
  case GamepadHelpers::ButtonAction::ToggleSidebar:
    emit requestToggleSidebarAction();
    return;
  case GamepadHelpers::ButtonAction::None:
    return;
  }
}
