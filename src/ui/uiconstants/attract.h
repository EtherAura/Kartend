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
inline constexpr double DEFAULT_SCROLL_SPEED_PX = 1.0;
/// Minimum configurable scroll speed (pixels per tick). Sub-pixel allowed via
/// fractional accumulator in AttractManager so very slow scroll (e.g. one page
/// in 30+ seconds) is achievable.
inline constexpr double MIN_SCROLL_SPEED_PX = 0.1;
/// Maximum configurable scroll speed (pixels per tick).
inline constexpr double MAX_SCROLL_SPEED_PX = 10.0;
/// Interval between autoscroll ticks (ms). 16 ms ≈ 60 fps.
inline constexpr int SCROLL_TICK_INTERVAL_MS = 16;
/// Brief pause at top/bottom before reversing direction (ms).
inline constexpr int BOUNCE_PAUSE_MS = 2000;
/// Default interval between selection advances during attract mode (seconds).
inline constexpr int DEFAULT_ADVANCE_INTERVAL_SEC = 5;
/// Minimum configurable selection-advance interval (seconds).
inline constexpr int MIN_ADVANCE_INTERVAL_SEC = 1;
/// Maximum configurable selection-advance interval (seconds).
inline constexpr int MAX_ADVANCE_INTERVAL_SEC = 600;
} // namespace Attract
} // namespace UIConstants

#endif
