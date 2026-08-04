#include "appearance_settings_persistence.h"

#include "settingskeys.h"
#include "textzoom.h"

namespace keys = kartend::settings::keys;

namespace AppearanceSettingsPersistence {

void load(QSettings &settings, AppearanceSettings &opts) {
  opts.titleTintSaturation = settings.value(keys::kTitleTintSaturation, 180).toInt();
  opts.titleTintLightness = settings.value(keys::kTitleTintLightness, 60).toInt();
  opts.titleBaseColor = settings.value(keys::kTitleBaseColor, QString()).toString();

  // Kartend-bbcu6 migration. Tinting is off for new installs, but an existing
  // install has been showing tinted titles since before the toggle existed —
  // flipping them to plain text on upgrade would be an unannounced appearance
  // change to somebody's library. So: if the toggle has never been written but
  // ANY of the tint knobs have, this is an upgrade, and the tint stays on with
  // whatever values are already stored.
  //
  // Deliberately keyed on the presence of the old keys rather than on their
  // values: a user who explicitly chose the shipped defaults is
  // indistinguishable by value from one who never touched them, and only the
  // presence of a written key proves the settings file predates this option.
  if (settings.contains(keys::kTitleTintEnabled)) {
    opts.titleTintEnabled = settings.value(keys::kTitleTintEnabled).toBool();
  } else {
    opts.titleTintEnabled = settings.contains(keys::kTitleTintSaturation) ||
                            settings.contains(keys::kTitleTintLightness) ||
                            settings.contains(keys::kTitleBaseColor);
  }
  opts.globalUiFontFamily = settings.value(keys::kGlobalUiFontFamily, QString()).toString();
  opts.globalUiFontPointSize = settings.value(keys::kGlobalUiFontPointSize, 0).toInt();
  opts.uiTextZoomPercent =
      qBound(TextZoom::MIN_PERCENT,
             settings.value(keys::kUiTextZoomPercent, TextZoom::DEFAULT_PERCENT).toInt(),
             TextZoom::MAX_PERCENT);
}

void save(QSettings &settings, const AppearanceSettings &opts) {
  settings.setValue(keys::kTitleTintEnabled, opts.titleTintEnabled);
  settings.setValue(keys::kTitleTintSaturation, opts.titleTintSaturation);
  settings.setValue(keys::kTitleTintLightness, opts.titleTintLightness);
  settings.setValue(keys::kTitleBaseColor, opts.titleBaseColor);
  settings.setValue(keys::kGlobalUiFontFamily, opts.globalUiFontFamily);
  settings.setValue(keys::kGlobalUiFontPointSize, opts.globalUiFontPointSize);
  settings.setValue(keys::kUiTextZoomPercent, opts.uiTextZoomPercent);
}

} // namespace AppearanceSettingsPersistence
