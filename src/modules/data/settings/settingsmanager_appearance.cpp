// Appearance-section settings I/O. Split from settingsmanager.cpp along
// leaf-struct boundaries; the load helper runs inside the caller's [General]
// group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/appearance_settings_persistence.h"
#include "collection/generalsettings.h"
#include "textzoom.h"

void SettingsManager::loadAppearanceSection(QSettings &s, GeneralSettings &settings) {
  AppearanceSettingsPersistence::load(s, settings.appearance);
}

void SettingsManager::saveAppearanceSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.appearance = settings.appearance;
  m_generalSettings.appearance.globalUiFontFamily =
      m_generalSettings.appearance.globalUiFontFamily.trimmed();
  m_generalSettings.appearance.uiTextZoomPercent = qBound(
      TextZoom::MIN_PERCENT, m_generalSettings.appearance.uiTextZoomPercent, TextZoom::MAX_PERCENT);
  AppearanceSettingsPersistence::save(s, m_generalSettings.appearance);
}
