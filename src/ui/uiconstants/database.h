#ifndef UICONSTANTS_DATABASE_H
#define UICONSTANTS_DATABASE_H

namespace UIConstants {

// =============================================================================
// Database
// Limits and thresholds for database-backed scanning and cache validation.
// =============================================================================
namespace Database {
/// How many directory samples are stored per collection for change detection.
inline constexpr int DIR_SIGNATURE_SAMPLE_COUNT = 64;
/// Max number of directories inspected when seeding a signature without
/// scanning.
inline constexpr int DIR_SIGNATURE_SEED_MAX_DIRS = 256;
/// Minimum interval between scan progress emissions.
/// Prevents high-frequency UI updates during very fast scans.
inline constexpr int SCAN_PROGRESS_MIN_INTERVAL_MS = 33;

/// Maximum number of file entries a single directory scan task will enqueue
/// at once. Bounds peak memory when scanning very large folders.
inline constexpr int SCAN_DIR_RESULT_CHUNK_SIZE = 2048;

/// Maximum number of pending scan result chunks buffered between worker
/// threads and the consuming scan loop. Provides backpressure to keep memory
/// bounded during very fast directory scans.
inline constexpr int SCAN_READY_MAX_RESULTS = 32;

/// FTS backfill batch size when building the index lazily.
/// Kept modest to reduce lock contention with normal query operations.
inline constexpr int FTS_BACKFILL_BATCH_SIZE = 2000;

/// Time budget for each incremental FTS backfill slice.
/// Keeps the scan worker responsive and avoids long write locks.
inline constexpr int FTS_BACKFILL_TIME_BUDGET_MS = 80;

/// Delay between incremental FTS backfill slices.
/// Prevents the scan worker from running a tight 0ms timer loop that can
/// consume a full CPU core on very large databases.
inline constexpr int FTS_BACKFILL_SLICE_DELAY_MS = 20;

/// Default chunk size for on-demand range loading during virtual scroll.
/// Balances latency (smaller = faster first paint) vs throughput (larger =
/// fewer round-trips).
inline constexpr int RANGE_CHUNK_SIZE_DEFAULT = 100;

/// Larger chunk size for showAllSubcollectionItems mode with many items.
/// Reduces database round-trips when flattening 1M+ items across
/// subcollections.
inline constexpr int RANGE_CHUNK_SIZE_LARGE = 1000;

/// Item count threshold to switch to larger chunk size.
/// When total items exceed this, use RANGE_CHUNK_SIZE_LARGE for efficiency.
inline constexpr int RANGE_CHUNK_LARGE_THRESHOLD = 5000;

/// Number of chunks to prefetch ahead of scroll position.
/// Prefetching reduces perceived latency during continuous scrolling.
inline constexpr int RANGE_PREFETCH_CHUNKS = 3;

/// Item count threshold to precompute sorted order for O(1) range lookups.
/// When items exceed this, fetchItemCount creates a temp table with sorted
/// positions. Avoids expensive ORDER BY + OFFSET for every range query on large
/// collections.
inline constexpr int PRECOMPUTE_SORT_THRESHOLD = 10000;

/// SQLite busy_timeout for the main DatabaseManager connection (ms).
/// Higher value tolerates long-running scan-worker write transactions.
inline constexpr int MAIN_BUSY_TIMEOUT_MS = 30000;

/// SQLite busy_timeout for the QueryManager worker connection (ms).
/// Lower than the main connection: query slots are expected to fail fast and
/// retry rather than block the UI worker thread for seconds.
inline constexpr int WORKER_BUSY_TIMEOUT_MS = 500;

/// Number of reconnect attempts before giving up on a lost worker connection.
inline constexpr int WORKER_RECONNECT_ATTEMPTS = 3;

/// Delay between worker reconnection attempts (ms).
inline constexpr int WORKER_RECONNECT_DELAY_MS = 100;
} // namespace Database
} // namespace UIConstants

#endif
