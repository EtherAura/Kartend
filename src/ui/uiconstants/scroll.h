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
} // namespace Scroll
} // namespace UIConstants

#endif
