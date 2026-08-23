#ifndef KARTEND_UTILS_APP_COLLECTION_SYSTEMICON_SETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_SYSTEMICON_SETTINGS_PERSISTENCE_H

// Kartend-1kkk2: per-cluster persistence for SystemIconSettings, matching the
// CollectionTreeSettings / SidebarAppearance pattern — each leaf cluster owns
// its INI load + save alongside the struct definition.
//
// No path fields, so no PathSanitizer parameter: the system name and pack are
// stored as IDENTITIES, and only become path components at resolve time,
// where RetroArchIcons::iconPath validates them.

#include <QSettings>
#include <QString>

#include "systemicon_settings.h"

namespace SystemIconSettingsPersistence {

/// Caller has entered the collection's INI group via beginGroup(). An
/// unknown subject falls back to Controller with a warning through
/// lcSettingsManager; the icon size clamps silently, like every other size.
void load(QSettings &settings, SystemIconSettings &icon, const QString &collectionName);

/// Save companion to load(). Caller is inside beginGroup() for the
/// collection.
void save(QSettings &settings, const SystemIconSettings &icon);

} // namespace SystemIconSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_SYSTEMICON_SETTINGS_PERSISTENCE_H
