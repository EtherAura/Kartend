#include "splash_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace SplashSettingsPersistence {

void load(QSettings &settings, SplashSettings &opts) {
  opts.bootSplashEnabled = settings.value(keys::kBootSplashEnabled, true).toBool();
  opts.resumeFocusSplashEnabled = settings.value(keys::kResumeFocusSplashEnabled, true).toBool();
  opts.bootSplashTitle = settings.value(keys::kBootSplashTitle).toString();
  opts.bootSplashSubtitle = settings.value(keys::kBootSplashSubtitle).toString();
  opts.resumeFocusSplashTitle = settings.value(keys::kResumeFocusSplashTitle).toString();
  opts.resumeFocusSplashSubtitle = settings.value(keys::kResumeFocusSplashSubtitle).toString();
}

void save(QSettings &settings, const SplashSettings &opts) {
  settings.setValue(keys::kBootSplashEnabled, opts.bootSplashEnabled);
  settings.setValue(keys::kResumeFocusSplashEnabled, opts.resumeFocusSplashEnabled);
  settings.setValue(keys::kBootSplashTitle, opts.bootSplashTitle);
  settings.setValue(keys::kBootSplashSubtitle, opts.bootSplashSubtitle);
  settings.setValue(keys::kResumeFocusSplashTitle, opts.resumeFocusSplashTitle);
  settings.setValue(keys::kResumeFocusSplashSubtitle, opts.resumeFocusSplashSubtitle);
}

} // namespace SplashSettingsPersistence
