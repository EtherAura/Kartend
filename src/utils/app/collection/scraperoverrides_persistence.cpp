#include "scraperoverrides_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace ScraperOverridesPersistence {

void load(QSettings &settings, ScraperOverrides &overrides) {
  overrides.screenscraperSystemId = settings.value(keys::kScreenscraperSystemId, -1).toInt();
  overrides.screenscraperHashArchive =
      settings.value(keys::kScreenscraperHashArchive, true).toBool();
  overrides.scraperProviderId = settings.value(keys::kScraperProviderId).toString().trimmed();
  // datFilePaths is persisted as a QSettings array so individual
  // entries can contain commas / brackets without delimiter
  // escaping. Legacy configs (pre-multi-DAT) shipped a single
  // `datFilePath=` key — when the array is absent and the legacy
  // key has a value, upgrade it to a one-entry list so the user
  // doesn't lose their existing DAT on first launch after upgrade.
  const int datPathCount = settings.beginReadArray(keys::kDatFilePaths);
  overrides.datFilePaths.clear();
  overrides.datFilePaths.reserve(datPathCount);
  for (int i = 0; i < datPathCount; ++i) {
    settings.setArrayIndex(i);
    const QString path = settings.value(keys::kPath).toString();
    if (!path.isEmpty()) overrides.datFilePaths.append(path);
  }
  settings.endArray();
  if (overrides.datFilePaths.isEmpty()) {
    const QString legacy = settings.value(keys::kDatFilePath).toString();
    if (!legacy.isEmpty()) overrides.datFilePaths.append(legacy);
  }
}

void save(QSettings &settings, const ScraperOverrides &overrides) {
  settings.setValue(keys::kScreenscraperSystemId, overrides.screenscraperSystemId);
  settings.setValue(keys::kScreenscraperHashArchive, overrides.screenscraperHashArchive);
  settings.setValue(keys::kScraperProviderId, overrides.scraperProviderId);
  // Drop any stale single-key `datFilePath=` from pre-upgrade configs
  // — keeping both formats around would confuse hand-editors and
  // round-tripping.
  settings.remove(keys::kDatFilePath);
  settings.beginWriteArray(keys::kDatFilePaths, overrides.datFilePaths.size());
  for (int i = 0; i < overrides.datFilePaths.size(); ++i) {
    settings.setArrayIndex(i);
    settings.setValue(keys::kPath, overrides.datFilePaths[i]);
  }
  settings.endArray();
}

} // namespace ScraperOverridesPersistence
