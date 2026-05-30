#ifndef KARTEND_UTILS_APP_COLLECTION_APPEARANCE_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_APPEARANCE_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "appearance_settings.h"

namespace AppearanceSettingsPersistence {

// Caller is inside beginGroup([General]). uiTextZoomPercent is clamped to the
// TextZoom bounds on load; save writes raw (the cache is already clamped /
// the font family already trimmed by the caller).
void load(QSettings &settings, AppearanceSettings &opts);
void save(QSettings &settings, const AppearanceSettings &opts);

} // namespace AppearanceSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_APPEARANCE_SETTINGS_PERSISTENCE_H
