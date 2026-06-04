#include "textzoom.h"

#include <algorithm>
#include <atomic>

namespace {
// Atomic so off-thread readers (item factory / virtual scroll / cover-flow
// renderer) can't race the main-thread setPercent/primeFromSettings writers
// (Kartend-wmlj).
std::atomic<int> g_textZoomPercent{TextZoom::DEFAULT_PERCENT};
} // namespace

namespace TextZoom {

int percent() {
  return g_textZoomPercent;
}

void primeFromSettings(int p) {
  g_textZoomPercent = std::clamp(p, MIN_PERCENT, MAX_PERCENT);
}

int setPercent(int p) {
  const int clamped = std::clamp(p, MIN_PERCENT, MAX_PERCENT);
  g_textZoomPercent = clamped;
  return clamped;
}

int zoomedFontSize(int baseSize) {
  const int pct = g_textZoomPercent;
  if (baseSize <= 0 || pct == 100) {
    return baseSize;
  }
  return std::max(1, baseSize * pct / 100);
}

} // namespace TextZoom
