#ifndef KARTEND_UTILS_APP_COLLECTION_STARTUP_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_STARTUP_SETTINGS_PERSISTENCE_H

#include <functional>
#include <QSettings>
#include <QString>

#include "startup_settings.h"

namespace StartupSettingsPersistence {

using PathSanitizer = std::function<QString(const QString &value, const QString &fieldId)>;

// Caller is inside beginGroup([General]). startupVideoPath is run through the
// caller-supplied sanitizer on load so a hand-edited config can't sneak shell
// metacharacters into a path. save writes raw (the cache is already trimmed
// and the current save path never re-sanitized on write).
void load(QSettings &settings, StartupSettings &opts, const PathSanitizer &sanitize);
void save(QSettings &settings, const StartupSettings &opts);

} // namespace StartupSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_STARTUP_SETTINGS_PERSISTENCE_H
