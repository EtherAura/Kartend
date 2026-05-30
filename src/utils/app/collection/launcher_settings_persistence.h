#ifndef KARTEND_UTILS_APP_COLLECTION_LAUNCHER_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_LAUNCHER_SETTINGS_PERSISTENCE_H

#include <functional>
#include <QSettings>
#include <QString>

#include "launcher_settings.h"

namespace LauncherSettingsPersistence {

using PathSanitizer = std::function<QString(const QString &value, const QString &fieldId)>;

// retroarchConfigPath lives in [General]; the caller is inside that group when
// calling these.
void loadScalars(QSettings &settings, LauncherSettings &opts);
void saveScalars(QSettings &settings, const LauncherSettings &opts);

// launcherPresets live in the top-level [Launchers] QSettings array — the
// caller must NOT be inside any group. loadPresets sanitizes each preset's
// launcherPath / corePath and drops id-less entries; savePresets writes raw
// (the array prefix is cleared by beginWriteArray so removed presets don't
// linger).
void loadPresets(QSettings &settings, LauncherSettings &opts, const PathSanitizer &sanitize);
void savePresets(QSettings &settings, const LauncherSettings &opts);

} // namespace LauncherSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_LAUNCHER_SETTINGS_PERSISTENCE_H
