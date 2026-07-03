// Media-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; the load helper runs inside the caller's [General] group.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/media_settings_persistence.h"
#include "uiconstants/cache.h"
#include "uiconstants/timing.h"

void SettingsManager::loadMediaSection(QSettings &s, GeneralSettings &settings) {
  MediaSettingsPersistence::load(s, settings.media);
}

void SettingsManager::saveMediaSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.media = settings.media;
  m_generalSettings.media.pixmapCacheSizeMB =
      qBound(UIConstants::Cache::MIN_PIXMAP_CACHE_MB, m_generalSettings.media.pixmapCacheSizeMB,
             UIConstants::Cache::MAX_PIXMAP_CACHE_MB);
  m_generalSettings.media.artworkDiskCacheBudgetMB =
      MediaSettingsPersistence::clampArtworkDiskCacheBudgetMB(
          m_generalSettings.media.artworkDiskCacheBudgetMB);
  m_generalSettings.media.videoThumbnailExtractionTimeoutMs =
      qBound(UIConstants::Timing::MIN_VIDEO_THUMBNAIL_TIMEOUT_MS,
             m_generalSettings.media.videoThumbnailExtractionTimeoutMs,
             UIConstants::Timing::MAX_VIDEO_THUMBNAIL_TIMEOUT_MS);
  m_generalSettings.media.previewVideoVolume =
      qBound(0, m_generalSettings.media.previewVideoVolume, 100);
  MediaSettingsPersistence::save(s, m_generalSettings.media);
}
