// Gamepad-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; the load helper runs inside the caller's [General] group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/gamepad_settings_persistence.h"
#include "collection/generalsettings.h"

void SettingsManager::loadGamepadSection(QSettings &s, GeneralSettings &settings) {
  GamepadSettingsPersistence::load(s, settings.gamepad);
}

void SettingsManager::saveGamepadSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.gamepad = settings.gamepad;
  GamepadSettingsPersistence::save(s, m_generalSettings.gamepad);
}
