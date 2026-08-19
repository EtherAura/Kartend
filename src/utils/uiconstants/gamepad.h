#ifndef UICONSTANTS_GAMEPAD_H
#define UICONSTANTS_GAMEPAD_H

namespace UIConstants {

// =============================================================================
// Gamepad
// Analog stick deadzones for digital navigation.
// =============================================================================
namespace Gamepad {
/// Threshold to consider an axis direction engaged (press)
inline constexpr double AXIS_DEADZONE_ON = 0.60;
/// Threshold to consider an axis direction released (hysteresis)
inline constexpr double AXIS_DEADZONE_OFF = 0.45;
/// Poll interval for gamepad state in SDL2 fallback backend (when connected)
inline constexpr int POLL_INTERVAL_MS = 16;
/// Slow poll interval when no controller is connected (reduces idle CPU)
inline constexpr int POLL_INTERVAL_IDLE_MS = 1000;
/// Repeat cadence for right-stick details-pane scrolling while the stick is
/// held off-centre (user request 2026-08-18). Slower than the poll rate so
/// a held deflection reads as a smooth scroll, not a jump.
inline constexpr int PANE_SCROLL_REPEAT_MS = 45;
} // namespace Gamepad
} // namespace UIConstants

#endif
