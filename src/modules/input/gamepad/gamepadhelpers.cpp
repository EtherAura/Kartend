#include "gamepadhelpers.h"

#include <algorithm>

namespace GamepadHelpers {

AxisDirectionState axisToDirections(double axisX, double axisY, Direction previousDirection,
                                    double onThreshold, double offThreshold, bool useStick) {
  AxisDirectionState s;
  if (!useStick) {
    return s;
  }
  const bool prevLeft = previousDirection == Direction::Left;
  const bool prevRight = previousDirection == Direction::Right;
  const bool prevUp = previousDirection == Direction::Up;
  const bool prevDown = previousDirection == Direction::Down;

  s.left = (axisX <= -onThreshold) || (prevLeft && axisX <= -offThreshold);
  s.right = (axisX >= onThreshold) || (prevRight && axisX >= offThreshold);
  s.up = (axisY <= -onThreshold) || (prevUp && axisY <= -offThreshold);
  s.down = (axisY >= onThreshold) || (prevDown && axisY >= offThreshold);
  return s;
}

Direction combineToDirection(bool dpadLeft, bool dpadRight, bool dpadUp, bool dpadDown,
                              bool useDpad, const AxisDirectionState &stick) {
  const bool left = (useDpad && dpadLeft) || stick.left;
  const bool right = (useDpad && dpadRight) || stick.right;
  const bool up = (useDpad && dpadUp) || stick.up;
  const bool down = (useDpad && dpadDown) || stick.down;

  if (up) {
    return Direction::Up;
  }
  if (down) {
    return Direction::Down;
  }
  if (left) {
    return Direction::Left;
  }
  if (right) {
    return Direction::Right;
  }
  return Direction::None;
}

MovementIntent movementFor(Direction direction) {
  switch (direction) {
  case Direction::Left:
    return {-1, false};
  case Direction::Right:
    return {1, false};
  case Direction::Up:
    return {-1, true};
  case Direction::Down:
    return {1, true};
  case Direction::None:
    return {0, false};
  }
  return {0, false};
}

double normalizeSdlAxis(int rawSint16) {
  if (rawSint16 >= 0) {
    return static_cast<double>(rawSint16) / 32767.0;
  }
  return static_cast<double>(rawSint16) / 32768.0;
}

ButtonAction resolveButtonAction(const QString &buttonName, const QString &confirmBinding,
                                  const QString &backBinding,
                                  const QString &toggleSidebarBinding) {
  const QString normalized = buttonName.trimmed();
  if (normalized.isEmpty()) {
    return ButtonAction::None;
  }
  auto matches = [&normalized](const QString &configured) -> bool {
    const QString c = configured.trimmed();
    return !c.isEmpty() && c.compare(normalized, Qt::CaseInsensitive) == 0;
  };
  // Confirm wins over back wins over toggle so a user who binds two
  // actions to the same physical button doesn't get unexpected side
  // effects from the lower-priority binding.
  if (matches(confirmBinding)) {
    return ButtonAction::Confirm;
  }
  if (matches(backBinding)) {
    return ButtonAction::Back;
  }
  if (matches(toggleSidebarBinding)) {
    return ButtonAction::ToggleSidebar;
  }
  return ButtonAction::None;
}

} // namespace GamepadHelpers
