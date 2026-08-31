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

/// Colour for muted/secondary text: the palette's mid role, contrast-repaired
/// against the window background. Raw palette(mid) lands nearly on the window
/// colour on dark themes (Breeze Dark measured well under the 4.5:1 AA floor),
/// which made the dim hint labels unreadable there; the repair keeps mid's
/// muted intent on themes where it already reads and only brightens/darkens
/// where it doesn't. Split from the stylesheet wrapper so tests can probe the
/// arithmetic with an explicit palette.
[[nodiscard]] inline QColor mutedLabelColor(const QPalette &palette) {
  return ColorContrast::ensureContrast(palette.color(QPalette::Mid),
                                       palette.color(QPalette::Window));
}

/// Stylesheet for muted/secondary text; `italic` selects the dim-italic "hint"
/// label variant. Centralized so restyling the ~26 dim labels is one edit
/// instead of dozens of duplicated inline strings (Kartend-975p6).
[[nodiscard]] inline QString mutedLabelStyleSheet(bool italic = false) {
  const QString style =
      QStringLiteral("color: %1;").arg(mutedLabelColor(QGuiApplication::palette()).name());
  return italic ? style + QStringLiteral(" font-style: italic;") : style;
}
} // namespace Color
} // namespace UIConstants

#endif
