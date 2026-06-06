#ifndef UICONSTANTS_ANIMATION_H
#define UICONSTANTS_ANIMATION_H

namespace UIConstants {

// =============================================================================
// Animation
// Durations and keyframe positions for scroll and pulse animations.
// =============================================================================
namespace Animation {
/// Duration for animated scroll to center selected item
inline constexpr int CENTER_SCROLL_DURATION_MS = 1500;
/// Minimum duration for smooth scroll animations
inline constexpr int SMOOTH_SCROLL_MIN_DURATION_MS = 1500;
/// Maximum duration for smooth scroll animations
inline constexpr int SMOOTH_SCROLL_MAX_DURATION_MS = 1500;
/// Duration for mouse wheel triggered smooth scroll
inline constexpr int SMOOTH_SCROLL_WHEEL_DURATION_MS = 1500;
/// Duration for horizontal glide animation between items
inline constexpr int HSCROLL_ANIM_DURATION_MS = 140;
/// Full cycle duration for selection pulse animation
inline constexpr int PULSE_DURATION_MS = 3000;
/// Minimum opacity during pulse animation cycle
inline constexpr double PULSE_OPACITY_LOW = 0.25;
/// Maximum opacity during pulse animation cycle
inline constexpr double PULSE_OPACITY_HIGH = 1.0;
/// Delay before pulse animation starts after selection
inline constexpr int PULSE_INACTIVITY_DELAY_MS = 100;
/// Keyframe position (0-1) for pulse peak opacity
inline constexpr double PULSE_KEYFRAME_MID_POS = 0.5;
} // namespace Animation
} // namespace UIConstants

#endif
