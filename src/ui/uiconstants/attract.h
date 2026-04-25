#ifndef UICONSTANTS_ATTRACT_H
#define UICONSTANTS_ATTRACT_H

namespace UIConstants {

// =============================================================================
// Attract Mode
// Tunables for the idle-triggered autoscroll / attract-mode behaviour.
// =============================================================================
namespace Attract {
/// Default idle timeout before attract mode activates (seconds).
inline constexpr int DEFAULT_IDLE_TIMEOUT_SEC = 120;
/// Minimum configurable idle timeout (seconds).
inline constexpr int MIN_IDLE_TIMEOUT_SEC = 10;
/// Maximum configurable idle timeout (seconds).
inline constexpr int MAX_IDLE_TIMEOUT_SEC = 3600;
/// Default scroll speed in pixels per tick.
inline constexpr int DEFAULT_SCROLL_SPEED_PX = 1;
/// Minimum configurable scroll speed (pixels per tick).
inline constexpr int MIN_SCROLL_SPEED_PX = 1;
/// Maximum configurable scroll speed (pixels per tick).
inline constexpr int MAX_SCROLL_SPEED_PX = 10;
/// Interval between autoscroll ticks (ms). 16 ms ≈ 60 fps.
inline constexpr int SCROLL_TICK_INTERVAL_MS = 16;
/// Brief pause at top/bottom before reversing direction (ms).
inline constexpr int BOUNCE_PAUSE_MS = 2000;
} // namespace Attract
} // namespace UIConstants

#endif
