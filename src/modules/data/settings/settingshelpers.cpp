#include "settingshelpers.h"

#include <Qt>

namespace SettingsHelpers {

auto coerceArtworkCycleModifier(int rawModifier) -> int {
  switch (rawModifier) {
  case static_cast<int>(Qt::ShiftModifier):
  case static_cast<int>(Qt::ControlModifier):
  case static_cast<int>(Qt::AltModifier):
  case static_cast<int>(Qt::MetaModifier):
    return rawModifier;
  default:
    return static_cast<int>(Qt::ShiftModifier);
  }
}

auto coerceSortMode(int rawValue) -> SortMode {
  if (rawValue >= static_cast<int>(SortMode::NameAscending) &&
      rawValue <= static_cast<int>(SortMode::SizeAscending)) {
    return static_cast<SortMode>(rawValue);
  }
  return SortMode::NameAscending;
}

} // namespace SettingsHelpers
