// Step-function migration dispatcher for the settings INI. See the header
// for the contract; this file owns the registration table and the bodies.

#include "settingsmigrations.h"

#include <functional>
#include <QLoggingCategory>
#include <vector>

namespace kartend::settings::migrations {

namespace {

Q_LOGGING_CATEGORY(lcSettingsMigrations, "kartend.settingsmigrations", QtInfoMsg)

// One registered upgrade step. Frozen at release time. @p apply mutates
// @p settings in place: read the old key shape, write the new shape,
// remove the legacy keys so subsequent loads don't see both.
struct MigrationStep {
  int from; // The version this step upgrades FROM.
  int to;   // == from + 1; steps are always single-version increments.
  std::function<void(QSettings &)> apply; // Mutates `settings` in place.
  const char *summary = "(unnamed migration step)";
};

// Registry of all known migrations. Each entry's `from` must equal the
// previous entry's `to` so the chain is dense — applyMigrations() walks
// sequentially and any gap would silently leave a user stranded.
//
// The v0 -> v1 step is registered as a no-op baseline (Kartend-h16z). Its
// purpose is twofold: it documents that pre-versioning legacy INIs are
// considered "version 0" and intentionally need no key-shape changes to
// reach the current schemaVersion, AND it keeps the dispatcher walk
// exercised in tests before any real bump lands. The body is empty so
// running it on legacy INIs is observably free.
const std::vector<MigrationStep> &registeredSteps() {
  static const std::vector<MigrationStep> steps{
      MigrationStep{
          /*from=*/0,
          /*to=*/1,
          [](QSettings & /*settings*/) {
            // Intentional no-op: every v1 key was loaded with its declared
            // default by the v0 reader. The dispatcher still walks this
            // step so tests can verify the registration table is non-empty
            // and the per-step invocation path is wired.
          },
          "v0->v1: legacy unversioned INIs (no key-shape change required)",
      },
      // Template for the first real schema bump:
      //
      // MigrationStep{
      //     /*from=*/1,
      //     /*to=*/2,
      //     [](QSettings &settings) {
      //       settings.beginGroup(QStringLiteral("General"));
      //       const QVariant legacy = settings.value(QStringLiteral("oldKey"));
      //       if (legacy.isValid()) {
      //         settings.setValue(QStringLiteral("newKey"), legacy);
      //         settings.remove(QStringLiteral("oldKey"));
      //       }
      //       settings.endGroup();
      //     },
      //     "v1->v2: rename oldKey to newKey under [General]",
      // },
  };
  return steps;
}

} // namespace

int applyMigrations(QSettings &settings, int loadedVersion, int currentVersion,
                    const QString &origin) {
  if (loadedVersion >= currentVersion) {
    // Same-version or future-version files are handled outside this
    // dispatcher (the caller logs the future-version warning).
    return loadedVersion;
  }

  const auto &steps = registeredSteps();
  int version = loadedVersion;
  for (const auto &step : steps) {
    if (step.from < version) continue; // Already past this rung.
    if (step.from != version) {        // Gap in the table — abort.
      qCWarning(lcSettingsMigrations).nospace()
          << origin << ": no migration step registered from version " << version << " to "
          << version + 1
          << "; the settings INI will load with current-build defaults for any "
             "keys that diverged. Add a step in settingsmigrations.cpp before "
             "shipping this schemaVersion bump.";
      return version;
    }
    if (step.to != step.from + 1) { // Defensive: enforce single-version increments.
      qCWarning(lcSettingsMigrations).nospace()
          << origin << ": migration step claims a non-unit jump (from " << step.from << " to "
          << step.to << "); refusing to apply it.";
      return version;
    }
    qCInfo(lcSettingsMigrations).nospace()
        << origin << ": applying settings migration " << step.from << " -> " << step.to << " ("
        << step.summary << ")";
    step.apply(settings);
    version = step.to;
    if (version >= currentVersion) {
      break;
    }
  }

  if (version < currentVersion) {
    qCWarning(lcSettingsMigrations).nospace()
        << origin << ": reached version " << version << " but the current build expects "
        << currentVersion
        << "; the upgrade chain is incomplete. Loaded fields will fall back to "
           "build defaults for any keys that diverged.";
  }
  return version;
}

} // namespace kartend::settings::migrations
