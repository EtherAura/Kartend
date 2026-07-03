#ifndef KARTEND_UTILS_APP_COLLECTION_MEDIA_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_MEDIA_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "media_settings.h"

namespace MediaSettingsPersistence {

// Caller is inside beginGroup([General]). All fields are clamped to
// defensive bounds on load; save writes raw (the cache is already clamped).
void load(QSettings &settings, MediaSettings &opts);
void save(QSettings &settings, const MediaSettings &opts);

/// Shared clamp for the disk artwork-cache budget: 0 is the "unlimited"
/// sentinel, everything else lands in [MIN_ARTWORK_DISK_CACHE_MB,
/// MAX_ARTWORK_DISK_CACHE_MB]. Used by the load path here, the settings-save
/// path, and CacheManager's setter so all three agree on the sentinel.
[[nodiscard]] int clampArtworkDiskCacheBudgetMB(int megabytes);

} // namespace MediaSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_MEDIA_SETTINGS_PERSISTENCE_H
