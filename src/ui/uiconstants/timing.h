#ifndef UICONSTANTS_TIMING_H
#define UICONSTANTS_TIMING_H

namespace UIConstants {

// =============================================================================
// Timing - General Delays
// All timing values are in milliseconds unless otherwise noted.
// =============================================================================
namespace Timing {
/// Minimal delay for immediate-next-event-loop deferral
inline constexpr int SHORT_DELAY_MS = 50;
/// Standard debounce interval for rapid events
inline constexpr int MEDIUM_DELAY_MS = 100;
/// Delay for operations that need visual settling
inline constexpr int LONG_DELAY_MS = 200;
/// Initial delay before first collection load
inline constexpr int STARTUP_DELAY_MS = 300;
/// Debounce for layout recalculation after resize/config change
inline constexpr int LAYOUT_UPDATE_DELAY_MS = 100;
/// Debounce for viewport-based artwork loading
inline constexpr int VIEWPORT_UPDATE_DELAY_MS = 100;
/// Throttle interval for scroll event processing (normal scrolling)
inline constexpr int SCROLL_THROTTLE_DELAY_MS = 100;
/// How long tooltips remain visible
inline constexpr int TOOLTIP_DISPLAY_MS = 2000;
/// Delay before showing tooltip after hover
inline constexpr int TOOLTIP_DELAY_MS = 50;
/// Legacy alias for LAYOUT_UPDATE_DELAY_MS
inline constexpr int LAYOUT_DELAY_MS = 100;
/// Time to wait for Qt layout system to stabilize
inline constexpr int VIEWPORT_DELAY_MS = 300;
/// Base interval for settings dialog retry operations
inline constexpr int SETTINGS_RETRY_BASE_MS = 50;
/// Time without interaction before triggering idle behavior
inline constexpr int USER_IDLE_THRESHOLD_MS = 2000;
/// Delay after resize before re-centering selection
inline constexpr int RESIZE_RECENTER_DELAY_MS = 150;
/// Default per-item video thumbnail extraction timeout (ms).
inline constexpr int DEFAULT_VIDEO_THUMBNAIL_TIMEOUT_MS = 4000;
/// Minimum configurable video thumbnail extraction timeout (ms). Floors
/// slow-system tuning while preventing the queue from stalling on a
/// misconfigured value.
inline constexpr int MIN_VIDEO_THUMBNAIL_TIMEOUT_MS = 1000;
/// Maximum configurable video thumbnail extraction timeout (ms).
inline constexpr int MAX_VIDEO_THUMBNAIL_TIMEOUT_MS = 30000;
} // namespace Timing
} // namespace UIConstants

#endif
