#ifndef KARTEND_UTILS_APP_COLLECTION_GAMEPAD_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_GAMEPAD_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "gamepad_settings.h"

namespace GamepadSettingsPersistence {

// Caller is inside beginGroup([General]).
void load(QSettings &settings, GamepadSettings &opts);
void save(QSettings &settings, const GamepadSettings &opts);

} // namespace GamepadSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_GAMEPAD_SETTINGS_PERSISTENCE_H
