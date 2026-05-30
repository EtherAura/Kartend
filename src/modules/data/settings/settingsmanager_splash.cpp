// Splash-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; the load helper runs inside the caller's [General] group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/splash_settings_persistence.h"

void SettingsManager::loadSplashSection(QSettings &s, GeneralSettings &settings) {
  SplashSettingsPersistence::load(s, settings.splash);
}

void SettingsManager::saveSplashSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.splash = settings.splash;
  SplashSettingsPersistence::save(s, m_generalSettings.splash);
}
