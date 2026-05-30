#include "startup_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace StartupSettingsPersistence {

void load(QSettings &settings, StartupSettings &opts, const PathSanitizer &sanitize) {
  opts.startupCollection = settings.value(keys::kStartupCollection, QString()).toString();
  opts.useHomeView = settings.value(keys::kUseHomeView, false).toBool();
  opts.homeViewLabel = settings.value(keys::kHomeViewLabel, QString()).toString();
  opts.homeViewIcon = settings.value(keys::kHomeViewIcon, QString()).toString();
  opts.firstRunComplete = settings.value(keys::kFirstRunComplete, false).toBool();
  opts.startupVideoEnabled = settings.value(keys::kStartupVideoEnabled, false).toBool();
  opts.startupVideoPath = sanitize(settings.value(keys::kStartupVideoPath, QString()).toString(),
                                   QStringLiteral("startupVideoPath"));
}

void save(QSettings &settings, const StartupSettings &opts) {
  settings.setValue(keys::kStartupCollection, opts.startupCollection);
  settings.setValue(keys::kUseHomeView, opts.useHomeView);
  settings.setValue(keys::kHomeViewLabel, opts.homeViewLabel);
  settings.setValue(keys::kHomeViewIcon, opts.homeViewIcon);
  settings.setValue(keys::kFirstRunComplete, opts.firstRunComplete);
  settings.setValue(keys::kStartupVideoEnabled, opts.startupVideoEnabled);
  settings.setValue(keys::kStartupVideoPath, opts.startupVideoPath);
}

} // namespace StartupSettingsPersistence
