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
} // namespace Overlay
} // namespace UIConstants

#endif
