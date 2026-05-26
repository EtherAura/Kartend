#ifndef KARTEND_SETTINGSMIGRATIONS_H
#define KARTEND_SETTINGSMIGRATIONS_H

// Step-function migration dispatcher for the settings INI.
//
// Mirrors the pattern in src/utils/db/dbmigrations.{h,cpp}: every schema
// bump registers a focused step that knows how to upgrade exactly the
// previous version's on-disk shape to the new one. The loader composes
// the steps automatically when it detects a stored schemaVersion lower
// than the build's current schemaVersion.
//
// Adding a new step:
//   1. Bump `kSettingsSchemaVersion` in settingsmanager.cpp.
//   2. Append a new step to the kMigrationSteps table in
//      settingsmigrations.cpp with `from` = the version that's being
//      upgraded FROM. Implement the inline lambda or free function it
//      points at.
//   3. Add a test fixture under tests/modules/settings/fixtures/ that
//      stores a representative INI at the previous schemaVersion and
//      assert post-migration shape via SettingsManager::loadGeneralSettings.
//   4. NEVER edit an existing step after release — it's frozen the
//      moment a user's INI passes through it.
//
// The dispatcher is a no-op when the stored version already matches the
// current version. It also tolerates a missing schemaVersion key (legacy
// pre-versioning install) — the absent key is treated as version 0 and
// every step runs in turn.

#include <QSettings>
#include <QString>

namespace kartend::settings::migrations {

// Applies the registered N->N+1 migration steps in order so a settings
// INI written at @p loadedVersion is in-place upgraded to
// @p currentVersion. The @p settings handle should not be inside a
// beginGroup() — steps explicitly target the groups they want to mutate.
// `origin` is included in structured logging so a migration warning can
// be traced back to the entry point.
//
// Returns the version actually reached. In the happy path that equals
// @p currentVersion. A return value < currentVersion means a step refused
// (logged via lcSettingsMigrations) — the caller should treat the load as
// best-effort and surface the gap to the user.
int applyMigrations(QSettings &settings, int loadedVersion, int currentVersion,
                    const QString &origin);

} // namespace kartend::settings::migrations

#endif // KARTEND_SETTINGSMIGRATIONS_H
