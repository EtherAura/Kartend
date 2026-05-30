#ifndef KARTEND_UTILS_APP_COLLECTION_SCRAPER_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_SCRAPER_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "scraper_settings.h"

namespace ScraperSettingsPersistence {

// Persistence for the [ScraperOptions] INI group. Credentials
// (ScraperSettings::credentials) are NOT handled here — they live in the
// [Scrapers] group and are read/written inline by SettingsManager because
// the keychain round-trip is coupled to the data-layer secret-service
// wrappers. Enum fields are clamped to their valid range on load; save
// writes raw (the cache is already clamped). The caller is inside
// beginGroup([ScraperOptions]); the schemaVersion sentinel is stamped by the
// caller AFTER saveOptions, so this never touches it.
void loadOptions(QSettings &settings, ScraperOptions &opts);
void saveOptions(QSettings &settings, const ScraperOptions &opts);

} // namespace ScraperSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_SCRAPER_SETTINGS_PERSISTENCE_H
