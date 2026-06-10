#ifndef KARTEND_UTILS_APP_COLLECTION_GAMEPAD_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_GAMEPAD_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Gamepad
// bindings; button names are symbolic and backends map them as available.

#include <QString>

struct GamepadSettings {
  bool gamepadUseDpad = true;
  bool gamepadUseLeftStick = true;
  QString gamepadConfirmButton = "A";
  QString gamepadBackButton = "B";
  QString gamepadToggleSidebarButton = "Y";
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const GamepadSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_GAMEPAD_SETTINGS_H
