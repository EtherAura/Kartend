#ifndef KARTEND_UTILS_APP_COLLECTION_SPLASH_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_SPLASH_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "splash_settings.h"

namespace SplashSettingsPersistence {

// Caller is inside beginGroup([General]).
void load(QSettings &settings, SplashSettings &opts);
void save(QSettings &settings, const SplashSettings &opts);

} // namespace SplashSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_SPLASH_SETTINGS_PERSISTENCE_H
