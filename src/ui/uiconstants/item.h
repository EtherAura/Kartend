#ifndef UICONSTANTS_ITEM_H
#define UICONSTANTS_ITEM_H

namespace UIConstants {

// =============================================================================
// Item Dimensions
// ItemWidget sizing, fonts, and validation bounds.
// =============================================================================
namespace Item {
/// Default item width when no collection-specific value set
inline constexpr int DEFAULT_WIDTH = 220;
/// Default item height when no collection-specific value set
inline constexpr int DEFAULT_HEIGHT = 245;
/// Minimum allowed item width in configuration
inline constexpr int MIN_WIDTH = 100;
/// Minimum allowed item height in configuration
inline constexpr int MIN_HEIGHT = 100;
/// Maximum allowed item width (4K resolution)
inline constexpr int MAX_WIDTH = 3840;
/// Maximum allowed item height (4K resolution)
inline constexpr int MAX_HEIGHT = 2160;
/// Minimum font size for item titles
inline constexpr int MIN_FONT_SIZE = 4;
/// Default font size for item titles
inline constexpr int DEFAULT_FONT_SIZE = 8;
/// Maximum font size for item titles
inline constexpr int MAX_FONT_SIZE = 72;
/// Minimum corner radius for item artwork
inline constexpr int MIN_CORNER_RADIUS = 0;
/// Default corner radius for item artwork
inline constexpr int DEFAULT_CORNER_RADIUS = 0;
/// Maximum corner radius for item artwork
inline constexpr int MAX_CORNER_RADIUS = 100;
} // namespace Item
} // namespace UIConstants

#endif
