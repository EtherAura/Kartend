#include "media_settings_persistence.h"

#include "settingskeys.h"
#include "uiconstants/cache.h"
#include "uiconstants/timing.h"

namespace keys = kartend::settings::keys;

namespace MediaSettingsPersistence {

void load(QSettings &settings, MediaSettings &opts) {
  opts.pixmapCacheSizeMB =
      settings.value(keys::kPixmapCacheSizeMB, UIConstants::Cache::DEFAULT_PIXMAP_CACHE_MB).toInt();
  opts.pixmapCacheSizeMB = qBound(UIConstants::Cache::MIN_PIXMAP_CACHE_MB, opts.pixmapCacheSizeMB,
                                  UIConstants::Cache::MAX_PIXMAP_CACHE_MB);
  opts.videoThumbnailExtractionTimeoutMs =
      settings
          .value(keys::kVideoThumbnailExtractionTimeoutMs,
                 UIConstants::Timing::DEFAULT_VIDEO_THUMBNAIL_TIMEOUT_MS)
          .toInt();
  opts.videoThumbnailExtractionTimeoutMs = qBound(
      UIConstants::Timing::MIN_VIDEO_THUMBNAIL_TIMEOUT_MS, opts.videoThumbnailExtractionTimeoutMs,
      UIConstants::Timing::MAX_VIDEO_THUMBNAIL_TIMEOUT_MS);
  opts.previewVideoVolume = qBound(0, settings.value(keys::kPreviewVideoVolume, 100).toInt(), 100);
}

void save(QSettings &settings, const MediaSettings &opts) {
  settings.setValue(keys::kPixmapCacheSizeMB, opts.pixmapCacheSizeMB);
  settings.setValue(keys::kVideoThumbnailExtractionTimeoutMs,
                    opts.videoThumbnailExtractionTimeoutMs);
  settings.setValue(keys::kPreviewVideoVolume, opts.previewVideoVolume);
}

} // namespace MediaSettingsPersistence
