// Direction + input handling cluster split out from gamepadmanager.cpp.
#include "gamepadmanager.h"
#include "ikeyboardmanager.h"
#include "uiconstants/gamepad.h"

#include <QApplication>
#include <QTimer>

void GamepadManager::setSuspended(bool suspended) {
  if (m_suspended == suspended) {
    return;
  }
  m_suspended = suspended;
  if (suspended) {
    // Drop any held direction + keyboard repeat so movement doesn't stick
    // under the launched program. Reset directly rather than through
    // applyActiveDirection(None): that path early-returns during binding
    // capture, which would leave the held direction and repeat running (and
    // m_activeDirection stale on resume). Gating the dispatch chokepoints
    // keeps further presses/directions from routing until resume.
    if (m_activeDirection != Direction::None) {
      emit requestScrollAnimationStop();
      m_activeDirection = Direction::None;
      if (auto *kb = keyboardMgr()) {
        kb->setPhysicalKeyDown(false);
        kb->stopRepeat(false);
      }
    }
  }
}

void GamepadManager::updateDirectionFromInputs() {
#if defined(KARTEND_HAS_QT_GAMEPAD) || defined(KARTEND_HAS_SDL2_GAMEPAD)
  if (shuttingDown() || m_suspended) {
    return;
  }

  if (m_bindingCaptureActive) {
    return;
  }

  const bool useDpad = !m_generalSettings || m_generalSettings->gamepad.gamepadUseDpad;
  const bool useStick = !m_generalSettings || m_generalSettings->gamepad.gamepadUseLeftStick;

  const auto stick = GamepadHelpers::axisToDirections(
      m_axisX, m_axisY, m_activeDirection, UIConstants::Gamepad::AXIS_DEADZONE_ON,
      UIConstants::Gamepad::AXIS_DEADZONE_OFF, useStick);

  const Direction newDirection =
      GamepadHelpers::combineToDirection(m_left, m_right, m_up, m_down, useDpad, stick);

  applyActiveDirection(newDirection);
#endif
}

void GamepadManager::updateRightStickSection() {
#if defined(KARTEND_HAS_QT_GAMEPAD) || defined(KARTEND_HAS_SDL2_GAMEPAD)
  if (shuttingDown() || m_suspended || m_bindingCaptureActive) {
    return;
  }
  const bool enabled =
      !m_generalSettings || m_generalSettings->gamepad.gamepadRightStickSections;
  const auto stick = GamepadHelpers::axisToDirections(
      m_axisRightX, m_axisRightY, m_rightStickDirection,
      UIConstants::Gamepad::AXIS_DEADZONE_ON, UIConstants::Gamepad::AXIS_DEADZONE_OFF, enabled);
  const Direction dir =
      GamepadHelpers::combineToDirection(false, false, false, false, /*useDpad=*/false, stick);

  // Vertical deflection scrolls (continuously, via the repeat timer); the
  // interaction layer decides whether a visible details pane claims it.
  const bool verticalHeld = dir == Direction::Up || dir == Direction::Down;
  if (verticalHeld) {
    if (!m_rightStickScrollTimer) {
      m_rightStickScrollTimer = new QTimer(this);
      m_rightStickScrollTimer->setInterval(UIConstants::Gamepad::PANE_SCROLL_REPEAT_MS);
      connect(m_rightStickScrollTimer, &QTimer::timeout, this, [this]() {
        if (shuttingDown() || m_suspended) {
          return;
        }
        emit requestPaneScroll(m_rightStickDirection == Direction::Up ? -1 : 1);
      });
    }
    if (!m_rightStickScrollTimer->isActive()) {
      m_rightStickScrollTimer->start();
    }
  } else if (m_rightStickScrollTimer && m_rightStickScrollTimer->isActive()) {
    m_rightStickScrollTimer->stop();
  }

  if (dir == m_rightStickDirection) {
    return; // held deflection: one flick per deflection, no auto-repeat
  }
  m_rightStickDirection = dir;
  if (dir == Direction::None) {
    return; // recentre arms the next flick
  }
  const int dx = dir == Direction::Left ? -1 : dir == Direction::Right ? 1 : 0;
  const int dy = dir == Direction::Up ? -1 : dir == Direction::Down ? 1 : 0;
  emit requestRightStickFlick(dx, dy);
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

  auto *kb = keyboardMgr();
  if (newDirection == Direction::None) {
    if (kb) {
      kb->setPhysicalKeyDown(false);
      kb->stopRepeat(false);
    }
    return;
  }

  // Changing direction should not trigger a recenter like a full release does.
  if (kb) {
    kb->setPhysicalKeyDown(false);
    kb->stopRepeat(true);
  }

  // Select-held chord (user request 2026-08-17): the direction press moves
  // the focus SECTION, not the selection — and must not start the keyboard
  // repeat pipeline (section hops don't auto-repeat).
  if (m_buttonBack) {
    const int dx = newDirection == Direction::Left ? -1
                   : newDirection == Direction::Right ? 1
                                                      : 0;
    const int dy = newDirection == Direction::Up ? -1 : newDirection == Direction::Down ? 1 : 0;
    if (dx != 0 || dy != 0) {
      emit requestFocusSectionMove(dx, dy);
    }
    return;
  }

  // Start/restart keyboard repeat pipeline for held input.
  if (kb) {
    kb->setPhysicalKeyDown(true);
  }

  const auto movement = GamepadHelpers::movementFor(newDirection);
  if (movement.delta != 0) {
    emit requestSelectionMove(movement.delta, movement.vertical);
  }
}

void GamepadManager::handleMappedButtonPress(const QString &buttonName) {
  if (shuttingDown() || m_suspended) {
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
      m_generalSettings ? m_generalSettings->gamepad.gamepadConfirmButton : QStringLiteral("A");
  const QString back =
      m_generalSettings ? m_generalSettings->gamepad.gamepadBackButton : QStringLiteral("B");
  const QString toggleSidebar = m_generalSettings
                                    ? m_generalSettings->gamepad.gamepadToggleSidebarButton
                                    : QStringLiteral("R1");
  const QString toggleTree = m_generalSettings
                                 ? m_generalSettings->gamepad.gamepadToggleCollectionTreeButton
                                 : QStringLiteral("L1");

  switch (
      GamepadHelpers::resolveButtonAction(normalized, confirm, back, toggleSidebar, toggleTree)) {
  case GamepadHelpers::ButtonAction::Confirm:
    emit requestEnterAction();
    return;
  case GamepadHelpers::ButtonAction::Back:
    emit requestEscapeAction();
    return;
  case GamepadHelpers::ButtonAction::ToggleSidebar:
    emit requestToggleSidebarAction();
    return;
  case GamepadHelpers::ButtonAction::ToggleCollectionTree:
    emit requestToggleCollectionTreeAction();
    return;
  case GamepadHelpers::ButtonAction::None:
    return;
  }
}
