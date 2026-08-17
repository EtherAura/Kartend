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
      MigrationStep{
          /*from=*/1,
          /*to=*/2,
          [](QSettings &settings) {
            // User decision 2026-08-17: the navigation sidebar (collection
            // tree) docks LEFT and FULL-HEIGHT; the details pane docks RIGHT
            // and BELOW-TOOLBAR — applied to ALL existing collections, not
            // just future defaults (the save path writes every key
            // explicitly, so a default change alone would never reach an
            // existing INI). Values are the raw INI vocabulary of the two
            // persistence modules (collectiontreesettings_persistence /
            // sidebarappearance_persistence).
            //
            // A collection section is recognised by its `name` key — the one
            // key every collection writes and no reserved top-level group
            // ([General], [ScraperOptions], ...) carries. Matching on the
            // reserved-list would couple this step to a list that grows;
            // matching on `name` stays true by construction.
            const QStringList groups = settings.childGroups();
            for (const QString &group : groups) {
              settings.beginGroup(group);
              if (settings.contains(QStringLiteral("name"))) {
                settings.setValue(QStringLiteral("collectionTreePosition"),
                                  QStringLiteral("left"));
                settings.setValue(QStringLiteral("collectionTreeJustification"),
                                  QStringLiteral("full-height"));
                settings.setValue(QStringLiteral("sidebarPosition"), QStringLiteral("right"));
                settings.setValue(QStringLiteral("sidebarJustification"),
                                  QStringLiteral("below-toolbar"));
              }
              settings.endGroup();
            }
          },
          "v1->v2: navigation sidebar left+full-height, details pane "
          "right+below-toolbar, stamped onto every collection section",
      },
      MigrationStep{
          /*from=*/2,
          /*to=*/3,
          [](QSettings &settings) {
            // User decision 2026-08-17 (evening): tinted icons are the tree's
            // new default — colour logos clash with per-collection theming.
            // Stamp 'tinted' onto every collection section still on the old
            // 'normal' default (or with no style key); a DELIBERATE
            // monochrome choice is preserved. Same name-key section
            // recognition as v1->v2.
            const QStringList groups = settings.childGroups();
            for (const QString &group : groups) {
              settings.beginGroup(group);
              if (settings.contains(QStringLiteral("name"))) {
                const QString style =
                    settings.value(QStringLiteral("collectionTreeIconStyle")).toString();
                if (style.isEmpty() || style == QStringLiteral("normal")) {
                  settings.setValue(QStringLiteral("collectionTreeIconStyle"),
                                    QStringLiteral("tinted"));
                }
              }
              settings.endGroup();
            }
          },
          "v2->v3: tree icon style 'tinted' stamped over the old 'normal' "
          "default on every collection section",
      },
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
