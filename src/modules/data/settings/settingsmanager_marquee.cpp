// Marquee-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; the load helper runs inside the caller's [General] group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/marquee_settings_persistence.h"

void SettingsManager::loadMarqueeSection(QSettings &s, GeneralSettings &settings) {
  MarqueeSettingsPersistence::load(s, settings.marquee);
}

void SettingsManager::saveMarqueeSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.marquee = settings.marquee;
  MarqueeSettingsPersistence::save(s, m_generalSettings.marquee);
}
