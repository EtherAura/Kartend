#ifndef KEYBOARDHELPERS_H
#define KEYBOARDHELPERS_H

#include "collectiontypes.h"

// Pure helpers extracted from KeyboardManager so confirm-key equivalence,
// hold-repeat interval scaling, and arrow-direction routing can be
// unit-tested without instantiating KeyboardManager (which depends on
// ScrollManager + the rest of the manager graph).
//
// The existing test_keyboardmanager covers KeyboardManager's static
// selection helpers (calculateNewSelection / hasRowChanged /
// deriveDirectionForKey). These helpers extend that surface to the
// dispatch / repeat-cadence / view-aware-arrow logic.
namespace KeyboardHelpers {

// Returns true if `eventKey` should activate "confirm".
//
// `configuredConfirmKey` is the user-rebindable key. Return / Enter are
// treated as interchangeable so a setup that bound `Return` still fires on
// the keypad's `Enter`, and vice versa. Anything else requires an exact match.
[[nodiscard]] auto isConfirmKey(int eventKey, int configuredConfirmKey) -> bool;

// Scales a base hold-repeat interval by the global scrollVelocityMultiplier.
//
// - `velocityMultiplier <= 0` is treated as "feature off": returns
//   `baseInterval` unchanged. (Production code guards against zero-division
//   here; the helper preserves that invariant.)
// - A multiplier of exactly 1.0 is also a fast no-op.
// - Otherwise returns `round(baseInterval / velocityMultiplier)` clamped to a
//   minimum of `minIntervalMs` so the event loop can't be saturated by a
//   pathological multiplier.
[[nodiscard]] auto scaleRepeatInterval(int baseInterval, double velocityMultiplier,
                                       int minIntervalMs) -> int;

// Decoded arrow-key navigation intent. `valid` is false when `eventKey`
// doesn't match any of the four configured nav keys.
struct ArrowDirection {
  int direction = 0;
  bool vertical = false;
  bool valid = false;
};

// Resolves an arrow key press into a (direction, vertical) navigation request,
// honouring the active view type:
//
//   Grid       — Left/Right step ±1 horizontal, Up/Down step ±gridWidth vertical
//   List       — Left/Right step ±1 horizontal, Up/Down step ±1 vertical
//   CoverFlow  — same as List (single-step on every axis)
//   Horizontal — long axis is X; Up/Down step ±1 (linear), Left/Right step
//                ±gridWidth and travel through the column-wrap path so the
//                vertical-selection helper handles cross-column wrap.
//
// `gridWidth` must be > 0; if not, the helper returns invalid.
[[nodiscard]] auto computeArrowDirection(int eventKey, int navLeftKey, int navRightKey,
                                         int navUpKey, int navDownKey, ViewType viewType,
                                         int gridWidth) -> ArrowDirection;

} // namespace KeyboardHelpers

#endif // KEYBOARDHELPERS_H
