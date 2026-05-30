#ifndef KARTEND_UTILS_APP_COLLECTION_MEDIA_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_MEDIA_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "media_settings.h"

namespace MediaSettingsPersistence {

// Caller is inside beginGroup([General]). All three fields are clamped to
// defensive bounds on load; save writes raw (the cache is already clamped).
void load(QSettings &settings, MediaSettings &opts);
void save(QSettings &settings, const MediaSettings &opts);

} // namespace MediaSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_MEDIA_SETTINGS_PERSISTENCE_H
