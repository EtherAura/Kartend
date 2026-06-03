#ifndef UICONSTANTS_COLOR_H
#define UICONSTANTS_COLOR_H

#include <QString>

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
[[nodiscard]] inline QString errorLabelStyleSheet(bool bold = false) {
  const QString hex = QString::fromLatin1(VALIDATION_ERROR_FG);
  const QString style = QStringLiteral("color: %1;").arg(hex);
  return bold ? style + QStringLiteral(" font-weight: bold;") : style;
}
} // namespace Color
} // namespace UIConstants

#endif
