#ifndef UICONSTANTS_NAVIGATION_H
#define UICONSTANTS_NAVIGATION_H

namespace UIConstants {

// =============================================================================
// Navigation
// Timing for collection navigation, progress indicators, and scrollbar
// recovery.
// =============================================================================
namespace Navigation {
/// Early clear of loading progress indicator
inline constexpr int PROGRESS_CLEAR_EARLY_MS = 200;
/// Standard delay before clearing progress indicator
inline constexpr int PROGRESS_CLEAR_MS = 700;
/// Delay before centering after entering subcollection
inline constexpr int SUBCOLLECTION_SCROLL_CENTER_DELAY_MS = 600;
/// First attempt to recover scrollbar position
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_1_MS = 90;
/// Second attempt to recover scrollbar position
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_2_MS = 240;
/// Final attempt to recover scrollbar position
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_3_MS = 460;
} // namespace Navigation
} // namespace UIConstants

#endif
