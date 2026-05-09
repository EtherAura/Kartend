#include "mousehelpers.h"

namespace MouseHelpers {

auto wheelStepsFromAngle(int angleDeltaY, int angleStep) -> int {
  if (angleDeltaY == 0 || angleStep <= 0) {
    return 0;
  }
  int steps = angleDeltaY / angleStep;
  if (steps == 0) {
    steps = (angleDeltaY > 0 ? 1 : -1);
  }
  return steps;
}

auto wheelStepsFromPixel(int pixelDeltaY, int pixelStep) -> int {
  if (pixelDeltaY == 0 || pixelStep <= 0) {
    return 0;
  }
  int steps = pixelDeltaY / pixelStep;
  if (steps == 0) {
    steps = (pixelDeltaY > 0 ? 1 : -1);
  }
  return steps;
}

auto verticalScrollDirection(int selectedItemCenterY, int viewportTopY, int viewportBottomY,
                             int viewportCenterY, int rowHeight) -> int {
  if (selectedItemCenterY < viewportTopY + rowHeight) {
    return -1;
  }
  if (selectedItemCenterY > viewportBottomY - rowHeight) {
    return 1;
  }
  if (selectedItemCenterY < viewportCenterY) {
    return -1;
  }
  if (selectedItemCenterY > viewportCenterY) {
    return 1;
  }
  return 0;
}

} // namespace MouseHelpers
