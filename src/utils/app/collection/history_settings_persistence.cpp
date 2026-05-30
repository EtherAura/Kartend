#include "history_settings_persistence.h"

#include "settingskeys.h"
#include "uiconstants/launch.h"

namespace keys = kartend::settings::keys;

namespace HistorySettingsPersistence {

void load(QSettings &settings, HistorySettings &opts) {
  opts.historyEnabled = settings.value(keys::kHistoryEnabled, true).toBool();
  opts.historyMaxEntries = qBound(
      UIConstants::Launch::MIN_HISTORY_MAX_ENTRIES,
      settings.value(keys::kHistoryMaxEntries, UIConstants::Launch::DEFAULT_HISTORY_MAX_ENTRIES)
          .toInt(),
      UIConstants::Launch::MAX_HISTORY_MAX_ENTRIES);
}

void save(QSettings &settings, const HistorySettings &opts) {
  settings.setValue(keys::kHistoryEnabled, opts.historyEnabled);
  settings.setValue(keys::kHistoryMaxEntries, opts.historyMaxEntries);
}

} // namespace HistorySettingsPersistence
