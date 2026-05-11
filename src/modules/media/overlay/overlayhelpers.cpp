#include "overlayhelpers.h"

#include <algorithm>
#include <cmath>

namespace OverlayHelpers {

int glideDurationMs(const QPoint &fromCenter, const QPoint &toCenter, double pixelsPerSecond,
                    int minMs, int maxMs) {
  if (pixelsPerSecond <= 0.0) {
    return maxMs;
  }
  const int dx = std::abs(fromCenter.x() - toCenter.x());
  const int dy = std::abs(fromCenter.y() - toCenter.y());
  const double distance = std::sqrt(static_cast<double>(dx * dx + dy * dy));
  const int raw = static_cast<int>(std::round((distance / pixelsPerSecond) * 1000.0));
  return std::clamp(raw, minMs, maxMs);
}

QRect chooseGlideStart(const QRect &startRect, const QRect &targetRect, bool overlayVisible) {
  if (overlayVisible && startRect.isValid()) {
    return startRect;
  }
  return targetRect;
}

QRect overlayRectForPosition(const QPoint &pos, int itemWidth, int itemHeight, int inset) {
  return QRect(pos.x() - inset, pos.y() - inset, itemWidth + 2 * inset, itemHeight + 2 * inset);
}

} // namespace OverlayHelpers
