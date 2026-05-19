#include "eventhelpers.h"

#include "gridutils.h"

namespace EventHelpers {

auto isActivityEvent(QEvent::Type type) -> bool {
  switch (type) {
  case QEvent::MouseMove:
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonRelease:
  case QEvent::KeyPress:
  case QEvent::KeyRelease:
  case QEvent::Wheel:
    return true;
  default:
    return false;
  }
}

auto shouldArmFirstClickDelay(qint64 lastActivityMs, qint64 nowMs, qint64 thresholdMs) -> bool {
  if (lastActivityMs <= 0 || thresholdMs <= 0) {
    return false;
  }
  const qint64 delta = nowMs - lastActivityMs;
  return delta >= thresholdMs;
}

auto computeLogicalCenteredScrollY(int selectedIndex, int gridWidth, int itemHeight,
                                   int verticalSpacing, int margins, int headerOffset,
                                   int viewportHeight) -> int {
  if (viewportHeight <= 0) {
    return 0;
  }
  int itemY =
      GridUtils::computeItemY(selectedIndex, gridWidth, itemHeight, verticalSpacing, margins);
  itemY += headerOffset;
  return itemY - (viewportHeight - itemHeight) / 2;
}

} // namespace EventHelpers
