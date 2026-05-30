#ifndef KARTEND_UTILS_APP_COLLECTION_ATTRACT_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_ATTRACT_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "attract_settings.h"

namespace AttractSettingsPersistence {

// Caller is inside beginGroup([General]). Numeric fields are clamped to the
// UIConstants::Attract bounds on load only; save writes raw (the in-memory
// cache is already clamped).
void load(QSettings &settings, AttractSettings &opts);
void save(QSettings &settings, const AttractSettings &opts);

} // namespace AttractSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_ATTRACT_SETTINGS_PERSISTENCE_H
