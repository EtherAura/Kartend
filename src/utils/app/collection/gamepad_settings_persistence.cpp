#include "gamepad_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace GamepadSettingsPersistence {

void load(QSettings &settings, GamepadSettings &opts) {
  opts.gamepadUseDpad = settings.value(keys::kGamepadUseDpad, true).toBool();
  opts.gamepadUseLeftStick = settings.value(keys::kGamepadUseLeftStick, true).toBool();
  opts.gamepadConfirmButton = settings.value(keys::kGamepadConfirmButton, QString("A")).toString();
  opts.gamepadBackButton = settings.value(keys::kGamepadBackButton, QString("B")).toString();
  // Shoulder-button defaults (user request 2026-08-17): L1/R1 mirror the
  // panels' sides — left shoulder folds the left sidebar (collection
  // tree), right shoulder the right one (details pane). Keyboard
  // equivalents stay F6/F9.
  opts.gamepadToggleSidebarButton =
      settings.value(keys::kGamepadToggleSidebarButton, QString("R1")).toString();
  opts.gamepadToggleCollectionTreeButton =
      settings.value(keys::kGamepadToggleCollectionTreeButton, QString("L1")).toString();
  opts.gamepadRightStickSections =
      settings.value(keys::kGamepadRightStickSections, true).toBool();
}

void save(QSettings &settings, const GamepadSettings &opts) {
  settings.setValue(keys::kGamepadUseDpad, opts.gamepadUseDpad);
  settings.setValue(keys::kGamepadUseLeftStick, opts.gamepadUseLeftStick);
  settings.setValue(keys::kGamepadConfirmButton, opts.gamepadConfirmButton);
  settings.setValue(keys::kGamepadBackButton, opts.gamepadBackButton);
  settings.setValue(keys::kGamepadToggleSidebarButton, opts.gamepadToggleSidebarButton);
  settings.setValue(keys::kGamepadToggleCollectionTreeButton,
                    opts.gamepadToggleCollectionTreeButton);
  settings.setValue(keys::kGamepadRightStickSections, opts.gamepadRightStickSections);
}

} // namespace GamepadSettingsPersistence
