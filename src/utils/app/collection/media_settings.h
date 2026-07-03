#ifndef KARTEND_UTILS_APP_COLLECTION_MEDIA_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_MEDIA_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Media caching +
// preview tuning. All three are clamped at load/save to defensive bounds.

struct MediaSettings {
  int pixmapCacheSizeMB = 50; // Default 50MB, user configurable
  // On-disk artwork-cache eviction budget (MB), enforced by CacheManager's
  // background cache-size walk. 0 = unlimited; non-zero values are clamped to
  // [MIN_ARTWORK_DISK_CACHE_MB, MAX_ARTWORK_DISK_CACHE_MB] at load/save. The
  // default mirrors UIConstants::Cache::ARTWORK_DISK_CACHE_BUDGET_MB (leaf
  // header — no include, same literal-default convention as the fields below).
  int artworkDiskCacheBudgetMB = 2048;
  // Hard cap per video-thumbnail extraction. If the decoder hasn't produced a
  // frame in this window the request is abandoned and a null pixmap is cached
  // so the queue advances. Tunable for slow systems. Bounds 1000-30000 ms.
  int videoThumbnailExtractionTimeoutMs = 4000;
  // Global volume for sidebar / overlay preview audio, 0-100. Applied to all
  // VideoPreviewWidget instances via the static setGlobalVolume() hook.
  int previewVideoVolume = 100;
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const MediaSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_MEDIA_SETTINGS_H
