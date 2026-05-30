// Launchers-section settings I/O. Split from settingsmanager.cpp along
// leaf-struct boundaries. retroarchConfigPath rides in [General] (the *Scalars
// helpers run inside the caller's open group); the launcherPresets array lives
// at the top level, so the *Presets helpers must run with no group open. The
// whole-struct copy + retroarchConfigPath trim happen in saveLaunchersScalars,
// which the shell always runs before saveLaunchersPresets.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/launcher_settings_persistence.h"

void SettingsManager::loadLaunchersScalars(QSettings &s, GeneralSettings &settings) {
  LauncherSettingsPersistence::loadScalars(s, settings.launchers);
}

void SettingsManager::loadLaunchersPresets(QSettings &s, GeneralSettings &settings) {
  // Stored as a QSettings array so size is implicit. loadPresets sanitizes each
  // path + drops id-less rows.
  LauncherSettingsPersistence::loadPresets(s, settings.launchers,
                                           &SettingsManager::sanitizeLoadedPath);
}

void SettingsManager::saveLaunchersScalars(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.launchers = settings.launchers;
  m_generalSettings.launchers.retroarchConfigPath =
      m_generalSettings.launchers.retroarchConfigPath.trimmed();
  LauncherSettingsPersistence::saveScalars(s, m_generalSettings.launchers);
}

void SettingsManager::saveLaunchersPresets(QSettings &s) {
  // savePresets clears the array prefix first so a preset removed via the
  // dialog doesn't linger as a stale row.
  LauncherSettingsPersistence::savePresets(s, m_generalSettings.launchers);
}
