#ifndef UICONSTANTS_CONCURRENCY_H
#define UICONSTANTS_CONCURRENCY_H

namespace UIConstants {

// =============================================================================
// Concurrency
// Thread pool sizing for background work.
// =============================================================================
namespace Concurrency {
/// Minimum worker threads for background pools
inline constexpr int WORKER_POOL_MIN_THREADS = 1;
/// Maximum worker threads for background pools (caps CPU contention)
inline constexpr int WORKER_POOL_MAX_THREADS = 4;
/// Ideal thread count divisor (e.g., 2 means use ~half of available threads)
inline constexpr int WORKER_POOL_DIVISOR = 2;
} // namespace Concurrency
} // namespace UIConstants

#endif
