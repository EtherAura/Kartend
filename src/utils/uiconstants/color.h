#ifndef UICONSTANTS_COLOR_H
#define UICONSTANTS_COLOR_H

#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QString>

#include "colorcontrast.h"

namespace UIConstants {

// =============================================================================
// Colors and Theming
// Color manipulation constants for theme-aware rendering.
// =============================================================================
namespace Color {
/// Saturation level for title tint (0-255)
inline constexpr int TITLE_TINT_SATURATION = 180;
/// Lightness level for title tint - lower = darker (0-255)
inline constexpr int TITLE_TINT_LIGHTNESS = 75;
/// Maximum channel value (255 for 8-bit color)
inline constexpr int CHANNEL_MAX = 255;
/// Base for hexadecimal color parsing
inline constexpr int HEX_BASE = 16;

/// Foreground hex for validation/error text in dialogs. Qt's QPalette has no
/// error role, so this single literal is the one place to make the error red
/// theme-aware later instead of duplicating it across dialogs (Kartend-c43nl).
inline constexpr const char *VALIDATION_ERROR_FG = "#d05050";

/// Stylesheet for an error/validation text label. `bold` adds font-weight:bold
/// to match the warning labels; the plain form is the inline validation line.
/// The literal is repaired against the live window background at call time:
/// #d05050 measures 3.72:1 / 3.22:1 against Breeze Light / Dark — under the
/// 4.5:1 normal-text AA floor on both — and Kartend-c43nl centralised it here
/// precisely so theme-awareness could land in one place (Kartend-q40q0). The
/// repair preserves the red hue and only moves lightness, so on any sane
/// theme this stays recognisably "the error colour".
[[nodiscard]] inline QString errorLabelStyleSheet(bool bold = false) {
  const QColor fg =
      ColorContrast::ensureContrast(QColor(QString::fromLatin1(VALIDATION_ERROR_FG)),
                                    QGuiApplication::palette().color(QPalette::Window));
  const QString style = QStringLiteral("color: %1;").arg(fg.name());
  return bold ? style + QStringLiteral(" font-weight: bold;") : style;
}

/// Stylesheet for muted/secondary text. Uses the palette's mid role so it tracks
/// the active theme. Centralized so restyling the ~26 dim palette(mid) labels is
/// one edit instead of dozens of duplicated inline strings (Kartend-975p6).
inline constexpr const char *MUTED_TEXT = "color: palette(mid);";
/// Muted text with italic — the dim-italic "hint" label style used across dialogs.
inline constexpr const char *MUTED_ITALIC_TEXT = "color: palette(mid); font-style: italic;";
} // namespace Color
} // namespace UIConstants

#endif
