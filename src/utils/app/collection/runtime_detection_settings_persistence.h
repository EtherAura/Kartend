#ifndef KARTEND_UTILS_APP_COLLECTION_RUNTIME_DETECTION_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_RUNTIME_DETECTION_SETTINGS_PERSISTENCE_H

#include <QSettings>

#include "runtime_detection_settings.h"

namespace RuntimeDetectionSettingsPersistence {

// Caller is inside beginGroup([General]).
void load(QSettings &settings, RuntimeDetectionSettings &opts);
void save(QSettings &settings, const RuntimeDetectionSettings &opts);

} // namespace RuntimeDetectionSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_RUNTIME_DETECTION_SETTINGS_PERSISTENCE_H
