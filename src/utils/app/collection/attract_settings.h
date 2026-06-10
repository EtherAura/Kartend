#ifndef KARTEND_UTILS_APP_COLLECTION_ATTRACT_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_ATTRACT_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Attract-mode /
// autoscroll preferences. Numeric fields are clamped at load/save to the
// UIConstants::Attract bounds.

struct AttractSettings {
  bool attractModeEnabled = false;
  int attractModeIdleTimeoutSec = 120;      // Seconds of idle before activation
  bool attractModeAutoScrollEnabled = true; // Sub-toggle: viewport autoscroll
  double attractModeScrollSpeed = 1.0;      // Pixels per tick (sub-pixel via accumulator)
  bool attractModeAdvanceSelectionEnabled = false;
  int attractModeAdvanceSelectionIntervalSec = 5; // Seconds between selection advances
  bool attractModeAdvanceSelectionRandom = false; // Pick random vs. sequential next
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const AttractSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_ATTRACT_SETTINGS_H
