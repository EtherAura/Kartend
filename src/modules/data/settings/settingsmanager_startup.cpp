// Startup-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; the load helper runs inside the caller's [General] group and
// feeds SettingsManager::sanitizeLoadedPath to the persistence layer so a
// hand-edited startup video path can't smuggle shell metacharacters in.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/startup_settings_persistence.h"

void SettingsManager::loadStartupSection(QSettings &s, GeneralSettings &settings) {
  StartupSettingsPersistence::load(s, settings.startup, &SettingsManager::sanitizeLoadedPath);
}

void SettingsManager::saveStartupSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.startup = settings.startup;
  m_generalSettings.startup.startupVideoPath = m_generalSettings.startup.startupVideoPath.trimmed();
  m_generalSettings.startup.homeViewLabel = m_generalSettings.startup.homeViewLabel.trimmed();
  m_generalSettings.startup.homeViewIcon = m_generalSettings.startup.homeViewIcon.trimmed();
  StartupSettingsPersistence::save(s, m_generalSettings.startup);
}
