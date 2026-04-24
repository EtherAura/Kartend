#ifndef UICONSTANTS_MOUSE_H
#define UICONSTANTS_MOUSE_H

namespace UIConstants {

// =============================================================================
// Mouse and Click Handling
// Timing for click-hold scrolling, wheel events, and double-click detection.
// =============================================================================
namespace Mouse {
/// Delay before click-hold scroll mode activates
inline constexpr int CLICK_HOLD_START_MS = 500;
/// Interval between horizontal scroll steps during click-hold
inline constexpr int CLICK_HOLD_HORIZONTAL_INTERVAL_MS = 320;
/// Duration to suppress arrow centering after wheel scroll
inline constexpr int WHEEL_SUPPRESS_ARROW_CENTER_MS = 420;
/// Duration to suppress position updates after double-click
inline constexpr int DOUBLE_CLICK_POS_SUPPRESS_MS = 300;
/// Time without scroll events to consider scrolling stopped
inline constexpr int CONTINUOUS_SCROLL_IDLE_MS = 300;
/// Timer to detect when user scroll interaction ends
inline constexpr int USER_SCROLL_IDLE_TIMER_MS = 240;
/// Delay before clearing user scroll active flag
inline constexpr int USER_SCROLL_ACTIVE_CLEAR_DELAY_MS = 50;
/// Brief delay before recentering after repeat stops
inline constexpr int STOP_REPEAT_RECENTER_DELAY_MS = 10;
/// Delay before hover selection commits; prevents incidental mouse passes from
/// changing selection immediately.
inline constexpr int HOVER_SELECT_DWELL_MS = 140;
/// Angle delta per wheel click (Qt standard: 120 = 1 step)
inline constexpr int WHEEL_ANGLE_STEP = 120;
/// Pixel scroll amount per wheel step
inline constexpr int WHEEL_PIXEL_STEP = 120;
} // namespace Mouse
} // namespace UIConstants

#endif
