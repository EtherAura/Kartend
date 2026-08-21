#ifndef UICONSTANTS_WIDGET_H
#define UICONSTANTS_WIDGET_H

namespace UIConstants {

// =============================================================================
// Widget Styling
// ItemWidget dimensions, borders, and pool sizing.
// =============================================================================
namespace Widget {
/// Margin around widget content
inline constexpr int MARGIN = 10;
/// Spacing between artwork and title
inline constexpr int SPACING = 8;
/// Internal padding for widget content
inline constexpr int PADDING = 20;

/// EXTRA inset between the artwork and the tile backdrop behind it, on top of
/// PADDING. Requested 2026-08-20 — cover art sat nearly flush against the
/// rounded backdrop on three sides, so the backdrop read as a hairline rather
/// than a frame. Applied to BOTH axes, so the art box shrinks symmetrically
/// and stays square; the backdrop and cell geometry are untouched, which
/// keeps column positions and the painted-extent alignment maths unchanged.
inline constexpr int ARTWORK_BACKDROP_INSET = 10;
/// Margin around icons within widgets
inline constexpr int ICON_MARGIN = 5;
/// Large widget size preset
inline constexpr int LARGE_SIZE = 220;
/// Base height for widget calculation
inline constexpr int HEIGHT_BASE = 200;
/// Extra height added to base
inline constexpr int HEIGHT_EXTRA = 25;
/// Minimum icon size in widgets
inline constexpr int MIN_ICON_SIZE = 60;
/// Default icon size when not specified
inline constexpr int DEFAULT_ICON_SIZE = 60;
/// Size of subcollection indicator triangle
inline constexpr int TRIANGLE_SIZE = 12;
/// Offset from corner for triangle indicator
inline constexpr int TRIANGLE_OFFSET = 10;
/// Border width for selected items
inline constexpr int BORDER_WIDTH_SELECTION = 4;
/// Border radius for rounded corners
inline constexpr int BORDER_RADIUS = 5;
/// Factor for darkening highlight color
inline constexpr int HIGHLIGHT_DARKEN_FACTOR = 120;

/// Widget pool sizing constants
namespace Pool {
/// Minimum widgets to keep in pool
inline constexpr int MIN_SIZE = 20;
/// Maximum widgets to keep in pool
inline constexpr int MAX_SIZE = 100;
/// Multiplier for buffer calculation
inline constexpr int BUFFER_MULTIPLIER = 2;
/// Idle time before triggering pool prewarm (ms)
inline constexpr int PREWARM_IDLE_MS = 500;
} // namespace Pool
} // namespace Widget
} // namespace UIConstants

#endif
