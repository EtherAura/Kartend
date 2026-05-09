#ifndef SETTINGSHELPERS_H
#define SETTINGSHELPERS_H

#include "collectiontypes.h"

// Pure helpers extracted from SettingsManager so its defensive enum coercion
// rules can be unit-tested without instantiating QSettings or the rest of the
// settings dialog graph.
//
// These exist primarily because hand-edited config files can land arbitrary
// integers in fields whose only sensible values are a small enum-like set.
// The helpers turn "anything → safe default" into a single, testable surface.
namespace SettingsHelpers {

// Returns a valid `Qt::KeyboardModifier`-style integer for the
// artworkCycleModifier setting. Accepts only Shift / Control / Alt / Meta
// (the modifiers exposed in the settings UI); anything else, including 0
// or compound bitmasks, falls back to Shift.
//
// Inputs are plain ints rather than the enum so callers don't need to
// pre-cast — this also lets the helper guard against bitmask combinations
// that happen to fall through a naive switch.
[[nodiscard]] auto coerceArtworkCycleModifier(int rawModifier) -> int;

// Coerces a hand-edited sort-mode integer back into the SortMode enum.
//
// Returns the parsed SortMode if `rawValue` is in the closed range
// [NameAscending, SizeAscending] (matching the production check). Otherwise
// returns NameAscending (the default).
[[nodiscard]] auto coerceSortMode(int rawValue) -> SortMode;

} // namespace SettingsHelpers

#endif // SETTINGSHELPERS_H
