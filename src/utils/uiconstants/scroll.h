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
/// Trailing retry cadence for cover-flow cards whose primary artwork
/// resolved empty against a still-cold DirectoryCache (Kartend-6x8tn).
/// Long enough for the off-thread dentry prewarm to land between passes,
/// short enough that the centered cards fill within ~1s of entering the
/// view on a normal (non-flattened) collection.
inline constexpr int COVER_FLOW_ARTWORK_RETRY_MS = 400;
/// Upper bound on consecutive cover-flow artwork retry passes. Keeps a
/// wedged or starved prewarm from re-arming the retry timer forever; the
/// next rebuild / range-chunk arrival restores a fresh budget.
inline constexpr int COVER_FLOW_ARTWORK_RETRY_MAX_ATTEMPTS = 10;
/// Trailing-edge debounce for re-running the hideMissingArtwork baseline
/// filter after item ranges stream in (Kartend-l66sn). Unloaded rows pass
/// the filter as "artwork unknown", so the baseline computed at startup is
/// too permissive until paths land; one re-evaluation per chunk burst hides
/// the genuinely artless items without rebuilding the view (and, when cover
/// flow is active, wiping its pixmap caches) once per arrival.
inline constexpr int HIDE_MISSING_REFILTER_DEBOUNCE_MS = 300;
/// Upper bound on consecutive baseline-refresh retries while the artwork
/// lookup cascade is still cold (each retry is one debounce interval apart
/// and schedules a prewarm). On a healthy system the cascade settles within
/// one or two retries; the cap keeps a wedged prewarm from polling forever.
/// Hitting it fails open — items stay visible, which is the same stance
/// mediaItemHasArtwork takes for an unsettled cascade.
inline constexpr int HIDE_MISSING_REFILTER_MAX_RETRIES = 20;
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
