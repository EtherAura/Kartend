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
  QString gamepadToggleSidebarButton = "R1";
  /// Toggles the collection tree panel (Kartend-ob1c9). Default empty =
  /// unbound — Y is taken by the details pane, and a surprise binding on
  /// upgrade would be worse than none.
  QString gamepadToggleCollectionTreeButton = "L1";
  /// Right-stick flicks hop the focus section (grid/toolbar/sidebars) —
  /// the default direction input for the section chord (user request
  /// 2026-08-17); Select+d-pad works as well.
  bool gamepadRightStickSections = true;
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const GamepadSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_GAMEPAD_SETTINGS_H
