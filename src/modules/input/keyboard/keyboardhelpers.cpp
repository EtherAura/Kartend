#include "keyboardhelpers.h"

#include <algorithm>
#include <cmath>

#include <Qt>

namespace KeyboardHelpers {

auto isConfirmKey(int eventKey, int configuredConfirmKey) -> bool {
  if (eventKey == configuredConfirmKey) {
    return true;
  }
  if (configuredConfirmKey == static_cast<int>(Qt::Key_Return) &&
      eventKey == static_cast<int>(Qt::Key_Enter)) {
    return true;
  }
  if (configuredConfirmKey == static_cast<int>(Qt::Key_Enter) &&
      eventKey == static_cast<int>(Qt::Key_Return)) {
    return true;
  }
  return false;
}

auto scaleRepeatInterval(int baseInterval, double velocityMultiplier, int minIntervalMs) -> int {
  if (velocityMultiplier <= 0.0 || velocityMultiplier == 1.0) {
    return baseInterval;
  }
  const int scaled = static_cast<int>(std::lround(baseInterval / velocityMultiplier));
  return std::max(minIntervalMs, scaled);
}

auto computeArrowDirection(int eventKey, int navLeftKey, int navRightKey, int navUpKey,
                           int navDownKey, ViewType viewType, int gridWidth) -> ArrowDirection {
  ArrowDirection out;
  if (gridWidth <= 0) {
    return out;
  }
  const bool singleStep = (viewType == ViewType::List) || (viewType == ViewType::CoverFlow);
  const bool isHorizontalView = (viewType == ViewType::Horizontal);

  if (isHorizontalView) {
    if (eventKey == navLeftKey) {
      out = {-gridWidth, true, true};
    } else if (eventKey == navRightKey) {
      out = {gridWidth, true, true};
    } else if (eventKey == navUpKey) {
      out = {-1, false, true};
    } else if (eventKey == navDownKey) {
      out = {1, false, true};
    }
    return out;
  }

  if (eventKey == navLeftKey) {
    out = {-1, false, true};
  } else if (eventKey == navRightKey) {
    out = {1, false, true};
  } else if (eventKey == navUpKey) {
    out = {singleStep ? -1 : -gridWidth, true, true};
  } else if (eventKey == navDownKey) {
    out = {singleStep ? 1 : gridWidth, true, true};
  }
  return out;
}

} // namespace KeyboardHelpers
