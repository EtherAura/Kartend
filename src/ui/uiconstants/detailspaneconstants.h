#ifndef UICONSTANTS_DETAILSPANECONSTANTS_H
#define UICONSTANTS_DETAILSPANECONSTANTS_H

namespace UIConstants {

// =============================================================================
// DetailsPane (introduced the rename; promoted the
// namespace to UIConstants::DetailsPane.)
// Details-pane dimensions and timing.
// =============================================================================
namespace DetailsPane {
/// Minimum details-pane width in pixels (Left/Right dock).
inline constexpr int MIN_WIDTH = 150;
/// Default details-pane width when first shown (Left/Right dock).
inline constexpr int FIXED_WIDTH = 300;
/// minimum details-pane height in pixels (Top/Bottom dock).
inline constexpr int MIN_HEIGHT = 120;
/// default details-pane height in pixels (Top/Bottom dock).
inline constexpr int FIXED_HEIGHT = 280;
/// Margin around details-pane content.
inline constexpr int MARGIN = 0;
/// Offset to account for scrollbar width.
inline constexpr int SCROLLBAR_OFFSET = 80;
/// Additional margin offset for layout.
inline constexpr int MARGIN_OFFSET = 40;
/// Delay before recalculating metrics after resize.
inline constexpr int METRICS_RECALC_DELAY_MS = 200;
/// Delay before notifying layout change.
inline constexpr int LAYOUT_NOTIFY_DELAY_MS = 100;
/// Delay before initial center scroll on show.
inline constexpr int INITIAL_CENTER_SCROLL_DELAY_MS = 50;
/// Delay after selection settles before starting preview video playback.
/// Was 500ms pre-Kartend-9q8d when the debounce had to be long enough to
/// outwait scroll animations (otherwise playVideo would block the GUI
/// thread mid-glide and stutter the scroll). Now Kartend-9q8d round 6's
/// scroll-idle predicate guards playVideo independently, so this debounce
/// only needs to confirm the user actually settled on an item — 200ms is
/// snappier without re-introducing the mid-scroll-block risk.
inline constexpr int VIDEO_PREVIEW_DEBOUNCE_MS = 200;
/// Debounce window for the sidebar metadata refresh (4 DB queries +
/// filesystem probes). Coalesces wheel/arrow storms (~10ms cadence) into a
/// single refresh once the selection settles. Tuned short enough that a
/// single click feels instant but long enough that a 30-tick wheel sweep
/// fires once instead of 30 times.
inline constexpr int METADATA_DEBOUNCE_MS = 60;
/// width of the resize grip on the inner edge (px). Has to be
/// wide enough to be a forgiving target but narrow enough that it doesn't
/// eat clicks aimed at controls near the inner edge (gallery edit button,
/// metadata buttons). 8 strikes the balance — also paired with an event
/// filter on inner widgets so children don't swallow grip-zone clicks.
inline constexpr int RESIZE_GRIP_PX = 8;
} // namespace DetailsPane
} // namespace UIConstants

#endif
