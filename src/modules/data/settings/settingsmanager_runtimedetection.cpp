// Runtime-detection-section settings I/O. Split from settingsmanager.cpp along
// leaf-struct boundaries; the load helper runs inside the caller's [General]
// group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/runtime_detection_settings_persistence.h"

void SettingsManager::loadRuntimeDetectionSection(QSettings &s, GeneralSettings &settings) {
  RuntimeDetectionSettingsPersistence::load(s, settings.runtimeDetection);
}

void SettingsManager::saveRuntimeDetectionSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.runtimeDetection = settings.runtimeDetection;
  RuntimeDetectionSettingsPersistence::save(s, m_generalSettings.runtimeDetection);
}
