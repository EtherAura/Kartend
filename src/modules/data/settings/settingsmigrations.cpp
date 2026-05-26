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
// When the file is empty (only v0 -> v1 will ever be added once a real
// schema bump lands), applyMigrations() short-circuits to the no-op fast
// path on every load.
const std::vector<MigrationStep> &registeredSteps() {
  // First real migration step will be appended here when the schema
  // version is bumped to 2. Documented as a placeholder so the dispatcher
  // wiring stays exercised in tests even before a real step exists.
  static const std::vector<MigrationStep> steps{
      // Example shape for the first real migration (left commented out
      // until needed so the test suite doesn't double-fire). The block
      // below is the canonical template — copy-paste, rename, replace
      // the body when bumping the schema:
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
