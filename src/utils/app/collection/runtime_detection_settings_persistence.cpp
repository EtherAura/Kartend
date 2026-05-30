#include "runtime_detection_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace RuntimeDetectionSettingsPersistence {

void load(QSettings &settings, RuntimeDetectionSettings &opts) {
  opts.runtimeDetectionEnabled = settings.value(keys::kRuntimeDetectionEnabled, false).toBool();
}

void save(QSettings &settings, const RuntimeDetectionSettings &opts) {
  settings.setValue(keys::kRuntimeDetectionEnabled, opts.runtimeDetectionEnabled);
}

} // namespace RuntimeDetectionSettingsPersistence
