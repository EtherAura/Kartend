#ifndef KARTEND_UTILS_APP_COLLECTION_TOOLBAR_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_TOOLBAR_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "toolbar_settings.h"

namespace ToolbarSettingsPersistence {

// Caller is inside beginGroup([General]).
void load(QSettings &settings, ToolbarSettings &opts);
void save(QSettings &settings, const ToolbarSettings &opts);

} // namespace ToolbarSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_TOOLBAR_SETTINGS_PERSISTENCE_H
