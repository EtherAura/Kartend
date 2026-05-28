#include "collectionfilterpreferences_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace CollectionFilterPreferencesPersistence {

void load(QSettings &settings, CollectionFilterPreferences &filter) {
  // title-exclusion patterns are stored as a QSettings array so
  // each pattern can contain commas / brackets / backslashes without
  // delimiter escaping concerns. titleExclusionEnabled defaults to true so a
  // user adding patterns sees them apply immediately.
  const int titleExcludeCount = settings.beginReadArray(keys::kTitleExclusionPatterns);
  filter.titleExclusionPatterns.clear();
  filter.titleExclusionPatterns.reserve(titleExcludeCount);
  for (int i = 0; i < titleExcludeCount; ++i) {
    settings.setArrayIndex(i);
    const QString pattern = settings.value(keys::kPattern).toString();
    if (!pattern.isEmpty()) {
      filter.titleExclusionPatterns.append(pattern);
    }
  }
  settings.endArray();
  filter.titleExclusionEnabled = settings.value(keys::kTitleExclusionEnabled, true).toBool();
}

void save(QSettings &settings, const CollectionFilterPreferences &filter) {
  settings.beginWriteArray(keys::kTitleExclusionPatterns, filter.titleExclusionPatterns.size());
  for (int i = 0; i < filter.titleExclusionPatterns.size(); ++i) {
    settings.setArrayIndex(i);
    settings.setValue(keys::kPattern, filter.titleExclusionPatterns[i]);
  }
  settings.endArray();
  settings.setValue(keys::kTitleExclusionEnabled, filter.titleExclusionEnabled);
}

} // namespace CollectionFilterPreferencesPersistence
