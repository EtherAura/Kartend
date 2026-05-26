#include "textzoom.h"

#include <algorithm>

namespace {
int g_textZoomPercent = TextZoom::DEFAULT_PERCENT;
} // namespace

namespace TextZoom {

int percent() {
  return g_textZoomPercent;
}

void primeFromSettings(int p) {
  g_textZoomPercent = std::clamp(p, MIN_PERCENT, MAX_PERCENT);
}

int setPercent(int p) {
  g_textZoomPercent = std::clamp(p, MIN_PERCENT, MAX_PERCENT);
  return g_textZoomPercent;
}

int zoomedFontSize(int baseSize) {
  if (baseSize <= 0 || g_textZoomPercent == 100) {
    return baseSize;
  }
  return std::max(1, baseSize * g_textZoomPercent / 100);
}

} // namespace TextZoom
