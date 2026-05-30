// History-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; the load helper runs inside the caller's [General] group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/history_settings_persistence.h"
#include "uiconstants/launch.h"

void SettingsManager::loadHistorySection(QSettings &s, GeneralSettings &settings) {
  HistorySettingsPersistence::load(s, settings.history);
}

void SettingsManager::saveHistorySection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.history = settings.history;
  m_generalSettings.history.historyMaxEntries = qBound(
      UIConstants::Launch::MIN_HISTORY_MAX_ENTRIES, m_generalSettings.history.historyMaxEntries,
      UIConstants::Launch::MAX_HISTORY_MAX_ENTRIES);
  HistorySettingsPersistence::save(s, m_generalSettings.history);
}
