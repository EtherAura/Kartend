#ifndef UICONSTANTS_CACHE_H
#define UICONSTANTS_CACHE_H

namespace UIConstants {

// =============================================================================
// Cache
// Memory cache sizing, disk persistence timing, and lazy loading.
// =============================================================================
namespace Cache {
/// Maximum pixmap cache size in KB (50 MB)
inline constexpr int PIXMAP_CACHE_KB = 1024 * 50;
/// Interval for lazy loading additional cache entries
inline constexpr int LAZY_LOAD_INTERVAL_MS = 500;
/// Batch size for lazy load operations
inline constexpr int LAZY_LOAD_BATCH_SIZE = 10;
/// Maximum entries in memory cache (LRU eviction)
inline constexpr int MAX_MEMORY_CACHE_SIZE = 256;
/// Minimum pixmap dimension to cache
inline constexpr int MIN_PIXMAP_SIZE = 200;
/// Delay after scroll stops before triggering silent load
inline constexpr int SCROLL_SILENT_LOAD_TRIGGER_DELAY_MS = 1500;
/// Time without scroll to consider inactivity
inline constexpr int SCROLL_INACTIVITY_MS = 500;
/// Delay before persisting cache changes to disk
inline constexpr int SAVE_DEFER_MS = 2000;
/// Interval between automatic cache saves (5 minutes)
inline constexpr int SAVE_INTERVAL_MS = 300000;
/// Check cache save condition every N operations
inline constexpr int CHECK_INTERVAL = 50;
/// Trigger save when cache grows by this factor
inline constexpr double SAVE_GROWTH_FACTOR = 1.2;
/// Quick save delay for urgent persistence
inline constexpr int QUICK_SAVE_DELAY_MS = 200;
/// Debounce interval for flush operations
inline constexpr int FLUSH_DEBOUNCE_MS = 1000;
} // namespace Cache
} // namespace UIConstants

#endif
