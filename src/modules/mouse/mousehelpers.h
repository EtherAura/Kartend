#ifndef MOUSEHELPERS_H
#define MOUSEHELPERS_H

// Pure helpers extracted from MouseManager so wheel-step accumulation and
// viewport-based vertical scroll direction can be unit-tested without
// instantiating MouseManager (which needs ScrollManager/QScrollArea/UI graph).
//
// See bd for the broader extraction effort.
namespace MouseHelpers {

// Converts a vertical mouse wheel angleDelta (Qt: 1/8 of a degree, typical
// notched mouse = 120 per click) into discrete scroll steps.
//
// - Returns 0 when angleDeltaY == 0.
// - Returns at least +/-1 when the delta is non-zero but smaller than one
//   step so a sub-step trackpad gesture still moves selection by one.
[[nodiscard]] auto wheelStepsFromAngle(int angleDeltaY, int angleStep) -> int;

// Converts a vertical pixelDelta (used by trackpads / hi-res wheels) into
// discrete scroll steps. Same sub-step guarantee as wheelStepsFromAngle.
[[nodiscard]] auto wheelStepsFromPixel(int pixelDeltaY, int pixelStep) -> int;

// Decides whether to scroll up (-1), down (+1), or stay (0) so the selected
// item is recentered in the viewport.
//
// All inputs are in the same coordinate space (typically grid container Y):
// - selectedItemCenterY: vertical center of the selected tile
// - viewportTopY / viewportBottomY: bounds of the visible viewport
// - viewportCenterY: midpoint of the viewport
// - rowHeight: height of one row (used as the "near edge" margin)
//
// Rules (in order):
// 1. If item is within rowHeight of the top edge -> -1 (scroll up).
// 2. If item is within rowHeight of the bottom edge -> +1 (scroll down).
// 3. If item is above viewport center -> -1.
// 4. If item is below viewport center -> +1.
// 5. Exactly at center -> 0.
[[nodiscard]] auto verticalScrollDirection(int selectedItemCenterY, int viewportTopY,
                                           int viewportBottomY, int viewportCenterY, int rowHeight)
    -> int;

} // namespace MouseHelpers

#endif // MOUSEHELPERS_H
