#ifndef KARTEND_UTILS_APP_COLLECTION_SIDEBARAPPEARANCE_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_SIDEBARAPPEARANCE_PERSISTENCE_H

// Kartend-1mud: per-cluster persistence helpers for SidebarAppearance.
// Matches the LauncherProfile / CollectionBackground pattern: each leaf
// cluster owns its INI load + save alongside the struct definition.

#include <functional>
#include <QSettings>
#include <QString>

#include "sidebarappearance.h"

namespace SidebarAppearancePersistence {

using PathSanitizer = std::function<QString(const QString &value, const QString &fieldId)>;

/// Path-sanitizer-aware load. Caller has entered the collection's INI
/// group via beginGroup(); the helper invokes the sanitizer once for
/// the sidebarBackgroundImage field (the only path field in the
/// cluster). The sidebarPosition fallback warning that fires when SS
/// returns an unrecognised enum is logged via lcSettingsManager.
void load(QSettings &settings, SidebarAppearance &sidebar, const QString &collectionName,
          const PathSanitizer &sanitize);

/// Save companion to load(). Caller is inside beginGroup() for the
/// collection. Writes the sidebar-prefixed keys back; non-sidebar
/// fields (horizontalAlignment, viewType, etc.) stay in
/// settingsmanagercollections.cpp because they're not part of the
/// SidebarAppearance struct.
void save(QSettings &settings, const SidebarAppearance &sidebar, const PathSanitizer &sanitize);

} // namespace SidebarAppearancePersistence

#endif // KARTEND_UTILS_APP_COLLECTION_SIDEBARAPPEARANCE_PERSISTENCE_H
