#include "attract_settings_persistence.h"

#include "settingskeys.h"
#include "uiconstants/attract.h"

namespace keys = kartend::settings::keys;

namespace AttractSettingsPersistence {

void load(QSettings &settings, AttractSettings &opts) {
  opts.attractModeEnabled = settings.value(keys::kAttractModeEnabled, false).toBool();
  opts.attractModeIdleTimeoutSec = qBound(
      UIConstants::Attract::MIN_IDLE_TIMEOUT_SEC,
      settings
          .value(keys::kAttractModeIdleTimeoutSec, UIConstants::Attract::DEFAULT_IDLE_TIMEOUT_SEC)
          .toInt(),
      UIConstants::Attract::MAX_IDLE_TIMEOUT_SEC);
  opts.attractModeAutoScrollEnabled =
      settings.value(keys::kAttractModeAutoScrollEnabled, true).toBool();
  opts.attractModeScrollSpeed = qBound(
      UIConstants::Attract::MIN_SCROLL_SPEED_PX,
      settings.value(keys::kAttractModeScrollSpeed, UIConstants::Attract::DEFAULT_SCROLL_SPEED_PX)
          .toDouble(),
      UIConstants::Attract::MAX_SCROLL_SPEED_PX);
  opts.attractModeAdvanceSelectionEnabled =
      settings.value(keys::kAttractModeAdvanceSelectionEnabled, false).toBool();
  opts.attractModeAdvanceSelectionIntervalSec =
      qBound(UIConstants::Attract::MIN_ADVANCE_INTERVAL_SEC,
             settings
                 .value(keys::kAttractModeAdvanceSelectionIntervalSec,
                        UIConstants::Attract::DEFAULT_ADVANCE_INTERVAL_SEC)
                 .toInt(),
             UIConstants::Attract::MAX_ADVANCE_INTERVAL_SEC);
  opts.attractModeAdvanceSelectionRandom =
      settings.value(keys::kAttractModeAdvanceSelectionRandom, false).toBool();
}

void save(QSettings &settings, const AttractSettings &opts) {
  settings.setValue(keys::kAttractModeEnabled, opts.attractModeEnabled);
  settings.setValue(keys::kAttractModeIdleTimeoutSec, opts.attractModeIdleTimeoutSec);
  settings.setValue(keys::kAttractModeAutoScrollEnabled, opts.attractModeAutoScrollEnabled);
  settings.setValue(keys::kAttractModeScrollSpeed, opts.attractModeScrollSpeed);
  settings.setValue(keys::kAttractModeAdvanceSelectionEnabled,
                    opts.attractModeAdvanceSelectionEnabled);
  settings.setValue(keys::kAttractModeAdvanceSelectionIntervalSec,
                    opts.attractModeAdvanceSelectionIntervalSec);
  settings.setValue(keys::kAttractModeAdvanceSelectionRandom,
                    opts.attractModeAdvanceSelectionRandom);
}

} // namespace AttractSettingsPersistence
