#ifndef UICONSTANTS_SIDEBAR_H
#define UICONSTANTS_SIDEBAR_H

namespace UIConstants {

// =============================================================================
// Sidebar
// Metadata sidebar dimensions and timing.
// =============================================================================
namespace Sidebar {
/// Minimum sidebar width in pixels
inline constexpr int MIN_WIDTH = 150;
/// Maximum sidebar width in pixels
inline constexpr int MAX_WIDTH = 350;
/// Fixed sidebar width when visible
inline constexpr int FIXED_WIDTH = 300;
/// Margin around sidebar content
inline constexpr int MARGIN = 0;
/// Offset to account for scrollbar width
inline constexpr int SCROLLBAR_OFFSET = 80;
/// Additional margin offset for layout
inline constexpr int MARGIN_OFFSET = 40;
/// Delay before recalculating metrics after resize
inline constexpr int METRICS_RECALC_DELAY_MS = 200;
/// Delay before notifying layout change
inline constexpr int LAYOUT_NOTIFY_DELAY_MS = 100;
/// Delay before initial center scroll on show
inline constexpr int INITIAL_CENTER_SCROLL_DELAY_MS = 50;
/// Delay after selection settles before starting preview video playback
inline constexpr int VIDEO_PREVIEW_DEBOUNCE_MS = 500;
} // namespace Sidebar
} // namespace UIConstants

#endif
