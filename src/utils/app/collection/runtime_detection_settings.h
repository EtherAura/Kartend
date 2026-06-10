#ifndef KARTEND_UTILS_APP_COLLECTION_RUNTIME_DETECTION_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_RUNTIME_DETECTION_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Runtime
// detection: when enabled, launched media items are tracked via a
// non-detached QProcess so the UI can sleep behind a "Now Playing" overlay
// while the child runs and restore + raise the window when it exits.

struct RuntimeDetectionSettings {
  bool runtimeDetectionEnabled = false;
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const RuntimeDetectionSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_RUNTIME_DETECTION_SETTINGS_H
