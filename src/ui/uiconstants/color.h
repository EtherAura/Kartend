#ifndef UICONSTANTS_COLOR_H
#define UICONSTANTS_COLOR_H

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
} // namespace Color
} // namespace UIConstants

#endif
