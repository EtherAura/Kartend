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
};

#endif // KARTEND_UTILS_APP_COLLECTION_GAMEPAD_SETTINGS_H
