#ifndef KARTEND_UTILS_APP_COLLECTION_INPUT_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_INPUT_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Carries the
// application-wide input-timing + navigation-semantics preferences:
// keyboard repeat, click-hold cadence, mouse-wheel rows, scroll animation,
// and the artwork-cycle mouse modifier. Field names are unchanged from the
// old flat GeneralSettings members so persistence keys round-trip.

#include <QtCore/Qt>

struct InputSettings {
  bool rememberSelection = false;
  bool wrapNavigation = false;
  // When enabled, moving the pointer over a visible item updates the current
  // selection without requiring a click. A global interaction preference
  // because it changes input semantics application-wide.
  bool selectItemOnHover = false;
  int keyboardRepeatIntervalMs = 260;     // Grid view keyboard repeat interval in ms
  int keyboardRepeatDelayMs = 260;        // Initial delay before keyboard repeat starts
  int clickHoldDelayMs = 500;             // Click-hold activation delay in ms
  int clickHoldRepeatIntervalMs = 320;    // Grid view interval between click-hold repeat steps
  int listKeyboardRepeatIntervalMs = 50;  // List view keyboard repeat interval in ms
  int listClickHoldRepeatIntervalMs = 80; // List view interval between click-hold repeat steps
  int mouseWheelRows = 1;                 // Rows to scroll per wheel step
  int scrollAnimationDurationMs = 1500;   // Scroll animation duration in ms
  // Global scroll-velocity multiplier applied to both mouse-wheel steps and
  // held-arrow key-repeat cadence. 1.0 = unchanged; >1 faster; <1 slower.
  // Clamped at load/save time so the user can't pick values that would stall
  // or saturate the animation pipeline.
  double scrollVelocityMultiplier = 1.0;
  // Mouse artwork-cycle modifier. Stored as the integer value of a single
  // Qt::KeyboardModifier flag (Shift / Control / Alt / Meta). Combined with
  // middle-click to cycle the grid through the item's artwork types. Loader
  // coerces unrecognized values back to Shift so a hand-edit can't disable
  // the gesture entirely.
  int artworkCycleModifier = static_cast<int>(Qt::ShiftModifier);
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const InputSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_INPUT_SETTINGS_H
