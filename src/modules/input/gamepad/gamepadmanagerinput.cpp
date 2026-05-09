// Direction + input handling cluster split out from gamepadmanager.cpp.
#include "gamepadmanager.h"
#include "keyboardmanager.h"
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

  // Convert left stick axes to digital directions with hysteresis.
  constexpr double kOn = UIConstants::Gamepad::AXIS_DEADZONE_ON;
  constexpr double kOff = UIConstants::Gamepad::AXIS_DEADZONE_OFF;

  const double x = m_axisX;
  const double y = m_axisY;

  const bool prevLeft = (m_activeDirection == Direction::Left);
  const bool prevRight = (m_activeDirection == Direction::Right);
  const bool prevUp = (m_activeDirection == Direction::Up);
  const bool prevDown = (m_activeDirection == Direction::Down);

  const bool stickLeft = useStick && ((x <= -kOn) || (prevLeft && x <= -kOff));
  const bool stickRight = useStick && ((x >= kOn) || (prevRight && x >= kOff));
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
      m_generalSettings ? m_generalSettings->gamepadConfirmButton : QStringLiteral("A");
  const QString back =
      m_generalSettings ? m_generalSettings->gamepadBackButton : QStringLiteral("B");
  const QString toggleSidebar =
      m_generalSettings ? m_generalSettings->gamepadToggleSidebarButton : QStringLiteral("Y");

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
