#include "keybinding_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace KeybindingSettingsPersistence {

void load(QSettings &settings, KeybindingSettings &opts) {
  opts.keyNavLeft = settings.value(keys::kKeyNavLeft, static_cast<int>(Qt::Key_Left)).toInt();
  opts.keyNavRight = settings.value(keys::kKeyNavRight, static_cast<int>(Qt::Key_Right)).toInt();
  opts.keyNavUp = settings.value(keys::kKeyNavUp, static_cast<int>(Qt::Key_Up)).toInt();
  opts.keyNavDown = settings.value(keys::kKeyNavDown, static_cast<int>(Qt::Key_Down)).toInt();
  opts.keyConfirm = settings.value(keys::kKeyConfirm, static_cast<int>(Qt::Key_Return)).toInt();
  opts.keyBack = settings.value(keys::kKeyBack, static_cast<int>(Qt::Key_Escape)).toInt();
  opts.keySearch = settings.value(keys::kKeySearch, static_cast<int>(Qt::Key_Slash)).toInt();
  opts.keyAlphabeticBack =
      settings.value(keys::kKeyAlphabeticBack, static_cast<int>(Qt::Key_PageUp)).toInt();
  opts.keyAlphabeticForward =
      settings.value(keys::kKeyAlphabeticForward, static_cast<int>(Qt::Key_PageDown)).toInt();
  opts.keyJumpFirst = settings.value(keys::kKeyJumpFirst, static_cast<int>(Qt::Key_Home)).toInt();
  opts.keyJumpLast = settings.value(keys::kKeyJumpLast, static_cast<int>(Qt::Key_End)).toInt();
  opts.keyItemDetails = settings.value(keys::kKeyItemDetails, static_cast<int>(Qt::Key_I)).toInt();
  opts.keyHomeView = settings.value(keys::kKeyHomeView, 0).toInt();
  opts.keyToggleCollectionTree = settings.value(keys::kKeyToggleCollectionTree, 0).toInt();
}

void save(QSettings &settings, const KeybindingSettings &opts) {
  settings.setValue(keys::kKeyNavLeft, opts.keyNavLeft);
  settings.setValue(keys::kKeyNavRight, opts.keyNavRight);
  settings.setValue(keys::kKeyNavUp, opts.keyNavUp);
  settings.setValue(keys::kKeyNavDown, opts.keyNavDown);
  settings.setValue(keys::kKeyConfirm, opts.keyConfirm);
  settings.setValue(keys::kKeyBack, opts.keyBack);
  settings.setValue(keys::kKeySearch, opts.keySearch);
  settings.setValue(keys::kKeyAlphabeticBack, opts.keyAlphabeticBack);
  settings.setValue(keys::kKeyAlphabeticForward, opts.keyAlphabeticForward);
  settings.setValue(keys::kKeyJumpFirst, opts.keyJumpFirst);
  settings.setValue(keys::kKeyJumpLast, opts.keyJumpLast);
  settings.setValue(keys::kKeyItemDetails, opts.keyItemDetails);
  settings.setValue(keys::kKeyHomeView, opts.keyHomeView);
  settings.setValue(keys::kKeyToggleCollectionTree, opts.keyToggleCollectionTree);
}

} // namespace KeybindingSettingsPersistence
