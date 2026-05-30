#include "marquee_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace MarqueeSettingsPersistence {

void load(QSettings &settings, MarqueeSettings &opts) {
  opts.marqueeEnabled = settings.value(keys::kMarqueeEnabled, false).toBool();
  opts.marqueeScreenName = settings.value(keys::kMarqueeScreenName).toString();
  opts.marqueeMode = qBound(0, settings.value(keys::kMarqueeMode, 0).toInt(), 2);
}

void save(QSettings &settings, const MarqueeSettings &opts) {
  settings.setValue(keys::kMarqueeEnabled, opts.marqueeEnabled);
  settings.setValue(keys::kMarqueeScreenName, opts.marqueeScreenName);
  settings.setValue(keys::kMarqueeMode, opts.marqueeMode);
}

} // namespace MarqueeSettingsPersistence
