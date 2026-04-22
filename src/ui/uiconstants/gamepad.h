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
} // namespace Gamepad
} // namespace UIConstants

#endif
