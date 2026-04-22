#ifndef UICONSTANTS_KEYBOARD_H
#define UICONSTANTS_KEYBOARD_H

namespace UIConstants {

// =============================================================================
// Keyboard Navigation
// Timing for arrow key navigation, repeat rates, and centering behavior.
// =============================================================================
namespace Keyboard {
/// Base interval between arrow key repeat steps
inline constexpr int BASE_INTERVAL_MS = 260;
/// Extra delay added for vertical navigation (row changes)
inline constexpr int VERTICAL_EXTRA_MS = 120;
/// Time to wait for animation to complete before next step
inline constexpr int ANIMATION_SETTLE_MS = 200;
/// Initial delay before key repeat begins
inline constexpr int REPEAT_START_DELAY_MS = 260;
/// Timer interval for view updates during navigation
inline constexpr int VIEW_UPDATE_INTERVAL_MS = 30;
/// Delay to clear arrow center suppression after selection restore
inline constexpr int ARROW_CENTER_CLEAR_AFTER_RESTORE_MS = 300;
/// Extended suppression after selection restore completes
inline constexpr int ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS = 500;
/// Delay before checking if arrow center can be cleared
inline constexpr int ARROW_CENTER_CLEAR_CHECK_DELAY_MS = 440;
/// Delay to clear arrow center suppression after explicit set
inline constexpr int ARROW_CENTER_CLEAR_AFTER_SET_MS = 240;
} // namespace Keyboard
} // namespace UIConstants

#endif
