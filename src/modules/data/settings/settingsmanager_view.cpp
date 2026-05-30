// View-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; the load helper runs inside the caller's [General] group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/view_settings_persistence.h"
#include "settingshelpers.h"

void SettingsManager::loadViewSection(QSettings &s, GeneralSettings &settings) {
  ViewSettingsPersistence::load(s, settings.view);
  // coerceSortMode is data-layer too: re-coerce the value the persistence
  // layer cast straight from the stored int back to a known SortMode.
  settings.view.sortMode =
      SettingsHelpers::coerceSortMode(static_cast<int>(settings.view.sortMode));
}

void SettingsManager::saveViewSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.view = settings.view;
  m_generalSettings.view.collectionTypeFilter =
      m_generalSettings.view.collectionTypeFilter.trimmed();
  ViewSettingsPersistence::save(s, m_generalSettings.view);
}
