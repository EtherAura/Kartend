// WCAG contrast arithmetic + lightness repair. See colorcontrast.h for the
// rationale (Kartend-q40q0). Pure colour math, no widgets — unit-tested by
// tests/utils/view/test_colorcontrast.cpp.
#include "colorcontrast.h"

#include <algorithm>
#include <cmath>

namespace ColorContrast {

namespace {

/// sRGB channel linearisation from the WCAG 2.x definition.
double linearised(double v) {
  return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

} // namespace

double relativeLuminance(const QColor &color) {
  return 0.2126 * linearised(color.redF()) + 0.7152 * linearised(color.greenF()) +
         0.0722 * linearised(color.blueF());
}

double contrastRatio(const QColor &a, const QColor &b) {
  // Named locals, not std::minmax over the two calls — minmax(const T&,
  // const T&) returns a pair of references that would dangle once the
  // temporaries die at the end of the expression.
  const double la = relativeLuminance(a);
  const double lb = relativeLuminance(b);
  const double hi = std::max(la, lb);
  const double lo = std::min(la, lb);
  return (hi + 0.05) / (lo + 0.05);
}

QColor breadcrumbLinkColor(const QColor &highlight, const QColor &window) {
  // Half saturation at a fixed lightness keeps the hue recognisable without
  // the highlight's full punch, then contrast repair guarantees it stays
  // legible on either polarity.
  int h = 0;
  int s = 0;
  int l = 0;
  highlight.getHsl(&h, &s, &l);
  return ensureContrast(QColor::fromHsl(h < 0 ? 0 : h, s / 2, 170), window);
}

QColor ensureContrast(const QColor &fg, const QColor &bg, double minRatio) {
  if (contrastRatio(fg, bg) >= minRatio) return fg;
  int hue = 0;
  int saturation = 0;
  int lightness = 0;
  int alpha = 0;
  fg.getHsl(&hue, &saturation, &lightness, &alpha);
  // Aim at whichever pole achieves the higher ratio against this background:
  // white yields (1.05)/(bgLum+0.05), black (bgLum+0.05)/(0.05). Comparing
  // the two picks the direction that can actually reach the target instead
  // of thresholding on "is the background dark".
  const double bgLum = relativeLuminance(bg);
  const bool lighten = (1.05 / (bgLum + 0.05)) >= ((bgLum + 0.05) / 0.05);
  // Relative luminance rises monotonically with HSL lightness at fixed
  // hue/saturation, so the first step that meets the ratio is the minimal
  // move. 255 iterations worst-case on colours computed once per theme
  // change — a scan beats a bisection on clarity here.
  const int step = lighten ? 1 : -1;
  QColor candidate = fg;
  for (int l = lightness + step; l >= 0 && l <= 255; l += step) {
    candidate = QColor::fromHsl(hue, saturation, l, alpha);
    if (contrastRatio(candidate, bg) >= minRatio) return candidate;
  }
  // Pole reached without meeting the target — the maximum achievable in the
  // reachable direction (only possible for minRatio near the 21:1 ceiling).
  return candidate;
}

} // namespace ColorContrast
