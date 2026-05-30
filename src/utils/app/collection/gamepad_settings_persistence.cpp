#include "gamepad_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace GamepadSettingsPersistence {

void load(QSettings &settings, GamepadSettings &opts) {
  opts.gamepadUseDpad = settings.value(keys::kGamepadUseDpad, true).toBool();
  opts.gamepadUseLeftStick = settings.value(keys::kGamepadUseLeftStick, true).toBool();
  opts.gamepadConfirmButton = settings.value(keys::kGamepadConfirmButton, QString("A")).toString();
  opts.gamepadBackButton = settings.value(keys::kGamepadBackButton, QString("B")).toString();
  opts.gamepadToggleSidebarButton =
      settings.value(keys::kGamepadToggleSidebarButton, QString("Y")).toString();
}

void save(QSettings &settings, const GamepadSettings &opts) {
  settings.setValue(keys::kGamepadUseDpad, opts.gamepadUseDpad);
  settings.setValue(keys::kGamepadUseLeftStick, opts.gamepadUseLeftStick);
  settings.setValue(keys::kGamepadConfirmButton, opts.gamepadConfirmButton);
  settings.setValue(keys::kGamepadBackButton, opts.gamepadBackButton);
  settings.setValue(keys::kGamepadToggleSidebarButton, opts.gamepadToggleSidebarButton);
}

} // namespace GamepadSettingsPersistence
