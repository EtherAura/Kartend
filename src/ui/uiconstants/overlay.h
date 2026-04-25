#ifndef UICONSTANTS_OVERLAY_H
#define UICONSTANTS_OVERLAY_H

namespace UIConstants {

// =============================================================================
// Overlay
// Timing for transient overlays (search loading indicator, etc.).
// =============================================================================
namespace Overlay {
/// Fade in/out duration for the search loading overlay (ms).
inline constexpr int SEARCH_LOADING_FADE_DURATION_MS = 150;
/// Pulse animation interval for the search loading overlay (ms).
inline constexpr int SEARCH_LOADING_PULSE_INTERVAL_MS = 800;
/// Fade in/out duration for the startup/focus splash overlay (ms).
inline constexpr int SPLASH_FADE_DURATION_MS = 180;
/// Time the startup/focus splash remains visible before auto-hiding (ms).
inline constexpr int SPLASH_VISIBLE_DURATION_MS = 1300;
} // namespace Overlay
} // namespace UIConstants

#endif
