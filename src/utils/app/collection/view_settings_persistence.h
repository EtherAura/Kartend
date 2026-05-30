#ifndef KARTEND_UTILS_APP_COLLECTION_VIEW_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_VIEW_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "view_settings.h"

namespace ViewSettingsPersistence {

// Caller is inside beginGroup([General]). sortMode is read raw (cast from
// the stored int) here; the caller re-coerces it through
// SettingsHelpers::coerceSortMode (data layer, out of reach from this TU).
// collectionTypeFilter is trimmed on load.
void load(QSettings &settings, ViewSettings &opts);
void save(QSettings &settings, const ViewSettings &opts);

} // namespace ViewSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_VIEW_SETTINGS_PERSISTENCE_H
