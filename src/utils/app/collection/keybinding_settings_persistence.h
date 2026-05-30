#ifndef KARTEND_UTILS_APP_COLLECTION_KEYBINDING_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_KEYBINDING_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "keybinding_settings.h"

namespace KeybindingSettingsPersistence {

// Caller is inside beginGroup([General]).
void load(QSettings &settings, KeybindingSettings &opts);
void save(QSettings &settings, const KeybindingSettings &opts);

} // namespace KeybindingSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_KEYBINDING_SETTINGS_PERSISTENCE_H
