#ifndef KARTEND_UTILS_APP_COLLECTION_LAUNCHERPROFILE_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_LAUNCHERPROFILE_PERSISTENCE_H

// Kartend-1mud: per-cluster persistence helpers for LauncherProfile.
// Extracted from settingsmanagercollections.cpp so each leaf cluster
// owns its INI load + save next to its struct definition. The main
// settingsmanagercollections.cpp becomes orchestration only — a loop
// that calls these helpers in turn.

#include <functional>
#include <QSettings>
#include <QString>

#include "launcherconfig.h"

namespace LauncherProfilePersistence {

/// Path-sanitizer callable used by both load and save. Caller supplies
/// a closure that captures the collection-name + needsRewrite tracking;
/// the helper invokes it once per stored path field. The (value, fieldId)
/// arguments mirror the inline sanitize* lambdas in settingsmanagercollections.cpp.
using PathSanitizer = std::function<QString(const QString &value, const QString &fieldId)>;

/// Loads the primary launcher fields + additionalLaunchers array out of
/// @p settings into @p profile. Caller has already entered the
/// collection's group with beginGroup(). Sets @p needsRewrite to true
/// when the load discards entries (e.g. empty additional-launcher slots
/// per Kartend-gjeu) so the next save normalises the INI.
void load(QSettings &settings, LauncherProfile &profile, bool &needsRewrite,
          const QString &collectionName, const PathSanitizer &sanitize);

/// Writes the primary launcher fields + additionalLaunchers array into
/// @p settings. Caller has already entered the collection's group via
/// beginGroup() (the same group the matching load() reads from).
void save(QSettings &settings, const LauncherProfile &profile, const PathSanitizer &sanitize);

} // namespace LauncherProfilePersistence

#endif // KARTEND_UTILS_APP_COLLECTION_LAUNCHERPROFILE_PERSISTENCE_H
