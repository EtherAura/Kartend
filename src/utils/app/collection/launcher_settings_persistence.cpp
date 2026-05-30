#include "launcher_settings_persistence.h"

#include "launcherpreset.h"
#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace LauncherSettingsPersistence {

void loadScalars(QSettings &settings, LauncherSettings &opts) {
  opts.retroarchConfigPath = settings.value(keys::kRetroarchConfigPath, QString()).toString();
}

void saveScalars(QSettings &settings, const LauncherSettings &opts) {
  settings.setValue(keys::kRetroarchConfigPath, opts.retroarchConfigPath);
}

void loadPresets(QSettings &settings, LauncherSettings &opts, const PathSanitizer &sanitize) {
  opts.launcherPresets.clear();
  const int presetCount = settings.beginReadArray(keys::kGroupLaunchers);
  opts.launcherPresets.reserve(presetCount);
  for (int i = 0; i < presetCount; ++i) {
    settings.setArrayIndex(i);
    LauncherPreset preset;
    preset.id = settings.value(keys::kId).toString();
    preset.name = settings.value(keys::kName).toString();
    preset.launcherPath = sanitize(settings.value(keys::kLauncherPath).toString(),
                                   QString("Launchers[%1].launcherPath").arg(i));
    preset.corePath = sanitize(settings.value(keys::kCorePath).toString(),
                               QString("Launchers[%1].corePath").arg(i));
    preset.launchParameters = settings.value(keys::kLaunchParameters).toString();
    // Drop entries with no id — they can't be referenced and would shadow
    // valid presets if a hand-edit accidentally cleared the field.
    if (!preset.id.trimmed().isEmpty()) {
      opts.launcherPresets.append(preset);
    }
  }
  settings.endArray();
}

void savePresets(QSettings &settings, const LauncherSettings &opts) {
  // beginWriteArray clears any existing entries with the same prefix, so a
  // preset removed via the dialog doesn't linger as a stale row.
  settings.beginWriteArray(keys::kGroupLaunchers, opts.launcherPresets.size());
  for (int i = 0; i < opts.launcherPresets.size(); ++i) {
    settings.setArrayIndex(i);
    const LauncherPreset &preset = opts.launcherPresets[i];
    settings.setValue(keys::kId, preset.id);
    settings.setValue(keys::kName, preset.name);
    settings.setValue(keys::kLauncherPath, preset.launcherPath);
    settings.setValue(keys::kCorePath, preset.corePath);
    settings.setValue(keys::kLaunchParameters, preset.launchParameters);
  }
  settings.endArray();
}

} // namespace LauncherSettingsPersistence
