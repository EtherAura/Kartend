#ifndef ATTRACTHELPERS_H
#define ATTRACTHELPERS_H

// Pure helpers extracted from AttractManager. The manager owns QTimers,
// QScrollBar, and ApplicationContext — these are the arithmetic in between
// (the advance-selection picks, the sub-pixel scroll accumulator, the
// bounce/clamp at the scrollbar boundaries), pulled out so they can be
// exercised without a UI graph.
namespace AttractHelpers {

// Linear-advance index for the selection-advance tick.
// Returns -1 when `total <= 0` (no items). When `current < 0` (no selection
// yet), returns 0 to start at the head. Otherwise wraps: (current + 1) %
// total, so cycling continues indefinitely.
[[nodiscard]] int linearAdvanceIndex(int current, int total);

// Selection-advance random pick range: returns a [start, end) interval the
// caller should pick a random index from. The pick is biased toward the
// current scroll direction so the new selection moves visually with the
// autoscroll; at a boundary in the scroll direction, falls back to the
// opposite half.
//   direction >= 0  → forward: (current, total)
//   direction < 0   → reverse: [0, current)
// When both halves are empty (single-item / no items / pathological state),
// returns a degenerate range where start == end so the caller can detect
// "no valid pick".
struct AdvanceRange {
  int rangeStart = 0;
  int rangeEnd = 0; // exclusive
  [[nodiscard]] bool isEmpty() const { return rangeStart >= rangeEnd; }
  [[nodiscard]] int size() const { return rangeEnd - rangeStart; }
};
[[nodiscard]] AdvanceRange randomAdvanceRange(int current, int total, int direction);

// Sub-pixel scroll accumulator step. The scrollbar only accepts ints, so
// the manager accumulates fractional pixels each tick and only advances by
// the integer part each time the accumulator crosses 1. Returns the integer
// step to apply this tick and the residual that carries forward.
//   delta == 0 → caller does nothing, but the residual is updated so very
//   slow speeds (e.g. 0.5 px/tick) eventually fire a 1-px step.
struct ScrollStep {
  int delta = 0;
  double newAccumulator = 0.0;
};
[[nodiscard]] ScrollStep accumulateScrollDelta(double accumulator, double speedPxPerTick);

// Boundary-aware scroll position. Given the bar's current value and the
// step the accumulator decided on, compute the next position and report
// whether the move hit a boundary (so the caller can start the bounce
// pause).
//   hitMax / hitMin → caller clamps to max/min and starts bounce; otherwise
//   `next` is just current + signed step.
struct NextScroll {
  int next = 0;
  bool hitMax = false;
  bool hitMin = false;
};
[[nodiscard]] NextScroll nextScrollPosition(int current, int delta, int direction, int minValue,
                                            int maxValue);

} // namespace AttractHelpers

#endif // ATTRACTHELPERS_H
