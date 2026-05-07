#ifndef UICONSTANTS_DETAILSPANE_H
#define UICONSTANTS_DETAILSPANE_H

namespace UIConstants {

// =============================================================================
// DetailsPane (Kartend-u2gx introduced the rename; Kartend-vyeq promoted the
// namespace to UIConstants::DetailsPane.)
// Details-pane dimensions and timing.
// =============================================================================
namespace DetailsPane {
/// Minimum details-pane width in pixels (Left/Right dock).
inline constexpr int MIN_WIDTH = 150;
/// Default details-pane width when first shown (Left/Right dock).
inline constexpr int FIXED_WIDTH = 300;
/// Kartend-u2gx: minimum details-pane height in pixels (Top/Bottom dock).
inline constexpr int MIN_HEIGHT = 120;
/// Kartend-u2gx: default details-pane height in pixels (Top/Bottom dock).
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
inline constexpr int VIDEO_PREVIEW_DEBOUNCE_MS = 500;
/// Kartend-u2gx: width of the resize grip on the inner edge (px). Has to be
/// wide enough to be a forgiving target but narrow enough that it doesn't
/// eat clicks aimed at controls near the inner edge (gallery edit button,
/// metadata buttons). 8 strikes the balance — also paired with an event
/// filter on inner widgets so children don't swallow grip-zone clicks.
inline constexpr int RESIZE_GRIP_PX = 8;
} // namespace DetailsPane
} // namespace UIConstants

#endif
