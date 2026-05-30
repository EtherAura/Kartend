#ifndef KARTEND_UTILS_APP_COLLECTION_INPUT_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_INPUT_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "input_settings.h"

namespace InputSettingsPersistence {

// Caller is inside beginGroup([General]). artworkCycleModifier is read raw
// here; the caller re-coerces it through SettingsHelpers::
// coerceArtworkCycleModifier (which lives in the data layer, out of reach
// from this TU).
void load(QSettings &settings, InputSettings &opts);
void save(QSettings &settings, const InputSettings &opts);

} // namespace InputSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_INPUT_SETTINGS_PERSISTENCE_H
