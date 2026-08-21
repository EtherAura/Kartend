#ifndef COLORCONTRAST_H
#define COLORCONTRAST_H

#include <QColor>

/// WCAG 2.x contrast arithmetic plus a minimal-adjustment repair helper for
/// text colours derived with absolute HSL components (Kartend-q40q0) — the
/// class of bug Kartend-bbcu6 fixed for item titles: taking a palette
/// colour's HUE but forcing fixed saturation/lightness pins the contrast
/// against one background polarity, so a pair tuned on a light background
/// lands near background luminance on a dark one (measured 1.63:1 there).
/// ensureContrast() keeps the derivation's hue/saturation intent and moves
/// only the lightness, only as far as the target ratio requires.
namespace ColorContrast {

/// WCAG 2.x relative luminance of an sRGB colour in [0, 1]. Alpha ignored —
/// callers compare opaque text colours against opaque backgrounds.
[[nodiscard]] double relativeLuminance(const QColor &color);

/// WCAG contrast ratio in [1, 21]; symmetric in its arguments.
[[nodiscard]] double contrastRatio(const QColor &a, const QColor &b);

/// WCAG AA minimum contrast for normal-size text. (Large/bold text only
/// needs 3:1, but every caller here paints normal-size labels.)
inline constexpr double kAaNormalText = 4.5;

/// Returns @p fg moved the minimal HSL-lightness distance needed to reach
/// @p minRatio against @p bg; hue, saturation and alpha are preserved, and a
/// colour that already meets the ratio comes back unchanged. Lightness walks
/// toward whichever pole (white or black) can achieve the higher ratio
/// against @p bg, so a mid-luminance background gets the reachable direction
/// rather than a coin flip. If even that pole cannot reach @p minRatio the
/// pole is returned — the maximum achievable at that hue.
[[nodiscard]] QColor ensureContrast(const QColor &fg, const QColor &bg,
                                    double minRatio = kAaNormalText);

/// The colour breadcrumb links are drawn in: the highlight's hue at half
/// saturation and a fixed lightness, contrast-repaired against @p window.
///
/// Shared because the collection tree's root label has to MATCH it (user
/// request 2026-08-19). Two copies of this derivation would drift the first
/// time either was tuned.
[[nodiscard]] QColor breadcrumbLinkColor(const QColor &highlight, const QColor &window);

} // namespace ColorContrast

#endif // COLORCONTRAST_H
