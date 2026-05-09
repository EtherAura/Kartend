#ifndef UICONSTANTS_ARTWORK_H
#define UICONSTANTS_ARTWORK_H

namespace UIConstants {

// =============================================================================
// Artwork Loading
// Batch sizes, timing, and throttling for async artwork loading.
// =============================================================================
namespace Artwork {
/// Target size for artwork scaling (pixels)
inline constexpr int BOX_SIZE = 400;
/// Max megabytes a single QImageReader decode may allocate. Qt 6's default is
/// 128 MB; we pin explicitly so future Qt changes can't relax the cap, and
/// size it for legitimate hi-res scans (~6000×6000 RGBA ≈ 144 MB).
inline constexpr int MAX_DECODE_MB = 256;
/// Batch size for high-priority (visible) artwork loading
inline constexpr int BATCH_HIGH = 10;
/// Batch size for low-priority (prefetch) artwork loading
inline constexpr int BATCH_LOW = 5;
/// Batch size for immediate viewport artwork
inline constexpr int IMMEDIATE_BATCH = 15;
/// Idle time before starting silent background loading
inline constexpr int SILENT_LOAD_IDLE_TIME_MS = 500;
/// Batch size for silent loading during scroll
inline constexpr int SILENT_LOAD_BATCH_SIZE = 30;
/// Default batch size for silent loading
inline constexpr int SILENT_LOAD_BATCH_SIZE_DEFAULT = 20;
/// Interval between silent load batches (higher = less CPU, slower precache)
inline constexpr int SILENT_LOAD_INTERVAL_MS = 200;
/// Delay before starting silent loading after scroll
inline constexpr int DEFER_SILENT_LOADING_DELAY_MS = 100;
/// Divisor for throttling silent load batch size
inline constexpr int SILENT_LOAD_THROTTLE_DIVISOR = 8;
/// Batch size for persistent silent load when idle
inline constexpr int PERSISTENT_SILENT_BATCH_IDLE = 8;
/// Batch size for persistent silent load when active (higher = faster but more
/// CPU)
inline constexpr int PERSISTENT_SILENT_BATCH_ACTIVE = 4;
/// Interval for persistent silent load operations (higher = less CPU, slower
/// precache)
inline constexpr int PERSISTENT_SILENT_LOAD_INTERVAL_MS = 300;
/// Minimum cooldown after a batch completes before starting another (prevents
/// CPU saturation)
inline constexpr int SILENT_LOAD_COOLDOWN_MS = 500;
/// Delay before starting silent load after items loaded
inline constexpr int START_SILENT_LOAD_AFTER_ITEMS_MS = 150;
/// Delay before reapplying filter after artwork load
inline constexpr int FILTER_REAPPLY_DELAY_MS = 150;
} // namespace Artwork
} // namespace UIConstants

#endif
