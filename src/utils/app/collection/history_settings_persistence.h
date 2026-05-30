#ifndef KARTEND_UTILS_APP_COLLECTION_HISTORY_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_HISTORY_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "history_settings.h"

namespace HistorySettingsPersistence {

// Caller is inside beginGroup([General]). historyMaxEntries is clamped to the
// Launch bounds on load; save writes raw (the cache is already clamped).
void load(QSettings &settings, HistorySettings &opts);
void save(QSettings &settings, const HistorySettings &opts);

} // namespace HistorySettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_HISTORY_SETTINGS_PERSISTENCE_H
