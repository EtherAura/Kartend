#include "appearance_settings_persistence.h"

#include "settingskeys.h"
#include "textzoom.h"

namespace keys = kartend::settings::keys;

namespace AppearanceSettingsPersistence {

void load(QSettings &settings, AppearanceSettings &opts) {
  opts.titleTintSaturation = settings.value(keys::kTitleTintSaturation, 180).toInt();
  opts.titleTintLightness = settings.value(keys::kTitleTintLightness, 60).toInt();
  opts.titleBaseColor = settings.value(keys::kTitleBaseColor, QString()).toString();
  opts.globalUiFontFamily = settings.value(keys::kGlobalUiFontFamily, QString()).toString();
  opts.globalUiFontPointSize = settings.value(keys::kGlobalUiFontPointSize, 0).toInt();
  opts.uiTextZoomPercent =
      qBound(TextZoom::MIN_PERCENT,
             settings.value(keys::kUiTextZoomPercent, TextZoom::DEFAULT_PERCENT).toInt(),
             TextZoom::MAX_PERCENT);
}

void save(QSettings &settings, const AppearanceSettings &opts) {
  settings.setValue(keys::kTitleTintSaturation, opts.titleTintSaturation);
  settings.setValue(keys::kTitleTintLightness, opts.titleTintLightness);
  settings.setValue(keys::kTitleBaseColor, opts.titleBaseColor);
  settings.setValue(keys::kGlobalUiFontFamily, opts.globalUiFontFamily);
  settings.setValue(keys::kGlobalUiFontPointSize, opts.globalUiFontPointSize);
  settings.setValue(keys::kUiTextZoomPercent, opts.uiTextZoomPercent);
}

} // namespace AppearanceSettingsPersistence
