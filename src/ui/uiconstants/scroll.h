#ifndef UICONSTANTS_SCROLL_H
#define UICONSTANTS_SCROLL_H

namespace UIConstants {

// =============================================================================
// Scroll
// Tunables for virtual scrolling, viewport sizing, and artwork prewarm.
// =============================================================================
namespace Scroll {
/// Item count threshold above which artwork prewarm is gated to a single
/// debounced batch (instead of fanning out for every visible row change).
inline constexpr int ARTWORK_PREWARM_LARGE_COLLECTION_THRESHOLD = 100;
/// Minimum gap between consecutive artwork prewarm batches (ms).
inline constexpr qint64 ARTWORK_PREWARM_DEBOUNCE_MS = 200;
/// Floor for the effective viewport width used in grid math (px). Prevents
/// degenerate layouts when the widget is briefly given a near-zero width
/// during construction or splitter resizes.
inline constexpr int MIN_EFFECTIVE_VIEWPORT_WIDTH = 200;
/// Debounce window for per-item cover-flow resolution (preview video
/// lookup + per-item artwork gallery). Mirrors the sidebar metadata
/// debounce: a wheel sweep across the carousel coalesces into one
/// resolution at the trailing edge instead of running DB + filesystem
/// probes for every intermediate selection.
inline constexpr int COVER_FLOW_RESOLVE_DEBOUNCE_MS = 60;
/// Default user-configurable scroll velocity multiplier. 1.0 = unmodified.
inline constexpr double DEFAULT_VELOCITY_MULTIPLIER = 1.0;
/// Minimum configurable scroll velocity multiplier. Bounded so the
/// multiplier can't stall scrolling outright.
inline constexpr double MIN_VELOCITY_MULTIPLIER = 0.25;
/// Maximum configurable scroll velocity multiplier. Bounded so the
/// multiplier can't run away on a misconfigured value.
inline constexpr double MAX_VELOCITY_MULTIPLIER = 5.0;
} // namespace Scroll
} // namespace UIConstants

#endif
