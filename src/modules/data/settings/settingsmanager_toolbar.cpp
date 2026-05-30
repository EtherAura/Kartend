// Toolbar-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; the load helper runs inside the caller's [General] group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/toolbar_settings_persistence.h"

void SettingsManager::loadToolbarSection(QSettings &s, GeneralSettings &settings) {
  ToolbarSettingsPersistence::load(s, settings.toolbar);
}

void SettingsManager::saveToolbarSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.toolbar = settings.toolbar;
  ToolbarSettingsPersistence::save(s, m_generalSettings.toolbar);
}
