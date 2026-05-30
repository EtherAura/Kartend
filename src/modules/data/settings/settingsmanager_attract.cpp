// Attract-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; the load helper runs inside the caller's [General] group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/attract_settings_persistence.h"
#include "collection/generalsettings.h"

void SettingsManager::loadAttractSection(QSettings &s, GeneralSettings &settings) {
  AttractSettingsPersistence::load(s, settings.attract);
}

void SettingsManager::saveAttractSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.attract = settings.attract;
  AttractSettingsPersistence::save(s, m_generalSettings.attract);
}
