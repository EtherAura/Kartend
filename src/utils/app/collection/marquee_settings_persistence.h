#ifndef KARTEND_UTILS_APP_COLLECTION_MARQUEE_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_MARQUEE_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "marquee_settings.h"

namespace MarqueeSettingsPersistence {

// Caller is inside beginGroup([General]). marqueeMode is clamped to its known
// range on load so a hand-edited INI can't propagate a bogus value.
void load(QSettings &settings, MarqueeSettings &opts);
void save(QSettings &settings, const MarqueeSettings &opts);

} // namespace MarqueeSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_MARQUEE_SETTINGS_PERSISTENCE_H
