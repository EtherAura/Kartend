#ifndef UICONSTANTS_SELECTION_H
#define UICONSTANTS_SELECTION_H

namespace UIConstants {

// =============================================================================
// Selection
// Timing for selection restore process and double-click handling.
// =============================================================================
namespace Selection {
/// Number of steps in progressive selection restore
inline constexpr int RESTORE_STEPS = 6;
/// Delay between each restore step
inline constexpr int RESTORE_STEP_DELAY_MS = 120;
/// Maximum total time for selection restore process
inline constexpr int RESTORE_MAX_DELAY_MS = 900;
/// First early verification check during restore
inline constexpr int RESTORE_EARLY_VERIFY_1_MS = 140;
/// Second early verification check during restore
inline constexpr int RESTORE_EARLY_VERIFY_2_MS = 320;
/// Suppress double-click immediately after subcollection enter
inline constexpr int DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS = 700;
/// Delay before clearing double-click suppression
inline constexpr int DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS = 800;
/// Debounce delay before pushing the selected item's metadata into the
/// sidebar. Prevents thrash during rapid keyboard / mouse navigation.
inline constexpr int METADATA_SIDEBAR_UPDATE_DELAY_MS = 120;
/// Selection-overlay glide animation: travel speed in pixels-per-second.
/// Used to derive a duration proportional to the distance the overlay must
/// move so short hops feel snappy and long jumps stay readable.
inline constexpr double OVERLAY_GLIDE_PIXELS_PER_SECOND = 1500.0;
/// Hard cap on selection-overlay glide animation duration (ms).
/// Prevents very long jumps from feeling sluggish.
inline constexpr int OVERLAY_GLIDE_MAX_DURATION_MS = 300;
} // namespace Selection
} // namespace UIConstants

#endif
