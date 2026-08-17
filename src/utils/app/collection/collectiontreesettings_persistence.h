#ifndef KARTEND_UTILS_APP_COLLECTION_COLLECTIONTREESETTINGS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_COLLECTIONTREESETTINGS_PERSISTENCE_H

// Kartend-ob1c9: per-cluster persistence for CollectionTreeSettings,
// matching the SidebarAppearance / CollectionBackground pattern — each leaf
// cluster owns its INI load + save alongside the struct definition. No path
// fields, so no PathSanitizer parameter.

#include <QSettings>
#include <QString>

#include "collectiontreesettings.h"

namespace CollectionTreeSettingsPersistence {

/// Caller has entered the collection's INI group via beginGroup(). The
/// position value reuses the details pane's left/right vocabulary; anything
/// that isn't "left" or "right" (including the pane-only "top"/"bottom")
/// clamps to Left with a warning through lcSettingsManager.
void load(QSettings &settings, CollectionTreeSettings &tree, const QString &collectionName);

/// Save companion to load(). Caller is inside beginGroup() for the
/// collection.
void save(QSettings &settings, const CollectionTreeSettings &tree);

} // namespace CollectionTreeSettingsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_COLLECTIONTREESETTINGS_PERSISTENCE_H
