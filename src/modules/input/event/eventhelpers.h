#ifndef EVENTHELPERS_H
#define EVENTHELPERS_H

#include <QEvent>
#include <QtGlobal>

// Pure helpers extracted from EventManager so its event-classification and
// wheel-centering math can be unit-tested without instantiating the full UI
// graph.
namespace EventHelpers {

// Returns true if `type` is one of the input event categories that count as
// user activity for attract-mode / idle bookkeeping
// (mouse press/release/move, key press/release, wheel).
[[nodiscard]] auto isActivityEvent(QEvent::Type type) -> bool;

// Returns true when the gap between `lastActivityMs` and `nowMs` has reached
// the configured idle threshold, i.e. the next click should be treated as
// the "first click after idle" (a row-change first-click candidate).
//
// - lastActivityMs <= 0 is treated as "no prior activity recorded": no arm.
// - thresholdMs <= 0 is treated as "feature off": no arm.
// - Negative deltas (clock skew) collapse to false.
[[nodiscard]] auto shouldArmFirstClickDelay(qint64 lastActivityMs, qint64 nowMs, qint64 thresholdMs)
    -> bool;

// Computes the logical (pre-scroll-scale-bounds) target scroll Y so the row
// containing `selectedIndex` is vertically centered in the viewport.
//
// Inputs map to the values WheelEventHandler::computeTargetScroll already
// derives from collection config / ScrollManager metrics:
//   gridWidth        - itemsPerRow
//   itemHeight       - row height in pixels
//   verticalSpacing  - inter-row spacing
//   margins          - top/bottom grid margin
//   headerOffset     - list-view header offset in pixels (0 for grid views)
//   viewportHeight   - visible viewport height in pixels
//
// Returns 0 for non-positive viewportHeight (no viewport => target 0).
// Negative results are passed through; the caller is expected to clamp into
// [0, scrollMax] (qBound style) since clamping needs the per-axis maximum
// which is a Qt-side value.
[[nodiscard]] auto computeLogicalCenteredScrollY(int selectedIndex, int gridWidth, int itemHeight,
                                                 int verticalSpacing, int margins, int headerOffset,
                                                 int viewportHeight) -> int;

} // namespace EventHelpers

#endif // EVENTHELPERS_H
