// Keybindings-section settings I/O. Split from settingsmanager.cpp along
// leaf-struct boundaries; the load helper runs inside the caller's [General]
// group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/keybinding_settings_persistence.h"

void SettingsManager::loadKeybindingsSection(QSettings &s, GeneralSettings &settings) {
  KeybindingSettingsPersistence::load(s, settings.keybindings);
}

void SettingsManager::saveKeybindingsSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.keybindings = settings.keybindings;
  KeybindingSettingsPersistence::save(s, m_generalSettings.keybindings);
}
