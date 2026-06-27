// Collection load/save helpers, plus the per-collection diff/signal walk
// emitted from saveCollections. The hierarchy-build algorithm lives in
// collectionhierarchybuilder.* (parents-first walk, subcollection
// re-parenting); per-leaf-cluster persistence lives in
// utils/app/collection/*_persistence.{h,cpp}.
#include "settingsmanager.h"

#include <utility>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "collectiondifffingerprint.h"
#include "collectionhierarchybuilder.h"
#include "errorpresentation.h"
#include "loggingcategories.h"

#include "collection/archiveoptions_persistence.h"
#include "collection/collectionbackground_persistence.h"
#include "collection/collectionconfig.h"
#include "collection/collectionfilterpreferences_persistence.h"
#include "collection/collectionscalars_persistence.h"
#include "collection/folderbrowsingoptions_persistence.h"
#include "collection/gridlayoutpreferences_persistence.h"
#include "collection/hierarchyhelpers.h"
#include "collection/launcherprofile_persistence.h"
#include "collection/listviewoptions_persistence.h"
#include "collection/scraperoverrides_persistence.h"
#include "collection/sidebarappearance_persistence.h"
#include "collection/typehelpers.h"
#include "configvalidation.h"
#include "errorutils.h"
#include "pathutils.h"
#include "settingskeys.h"
#include "settingsutils.h"
#include "titlefilter.h"

namespace keys = kartend::settings::keys;

namespace {

// Top-level INI groups that are NOT collections — global settings buckets
// owned by saveGeneralSettings and the launcher-preset / scraper code.
// loadCollections must skip them (or each loads as a blank-named "ghost"
// collection that can't be deleted) and saveCollections must preserve them
// (or a collection save wipes them). One list so the skip side and the
// preserve side can never drift apart.
const QSet<QString> &reservedTopLevelGroups() {
  // Built from the same keys::kGroup* constants the writers use
  // (saveGeneralSettings / saveScraperSection / launcher presets) so the
  // skip+preserve set can't drift from what is actually written to the INI.
  // Kartend audit S-05.
  static const QSet<QString> groups{
      QString::fromLatin1(keys::kGroupGeneral), QString::fromLatin1(keys::kGroupScrapers),
      QString::fromLatin1(keys::kGroupScraperOptions), QString::fromLatin1(keys::kGroupLaunchers)};
  return groups;
}

// Kartend-lc58a: builds the per-collection fingerprint baseline keyed by
// (name, mediaDirectory) UUID. Mirrors the identity rule emitPerCollectionDiffs
// uses; on a duplicate UUID the last entry wins, matching the prior
// QHash<uuid, const CollectionConfig*> lookup behaviour.
QHash<QString, CollectionDiffFingerprint>
buildFingerprintIndex(const QList<CollectionConfig> &collections) {
  QHash<QString, CollectionDiffFingerprint> index;
  index.reserve(collections.size());
  for (const CollectionConfig &c : collections) {
    index.insert(CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory),
                 collectionDiffFingerprint(c));
  }
  return index;
}

} // namespace

void SettingsManager::finalizeCollections(const QHash<QString, CollectionConfig> &tempCollections,
                                          QList<CollectionConfig> &collections,
                                          const bool &needsRewrite) {
  CollectionHierarchyBuilder::build(tempCollections, collections);
  if (needsRewrite) {
    ErrorPresentation::reportSaveResult(saveCollections(collections), "collections", false);
  }
}

// Loads collections from config (no automatic default collections; leaves list
// empty if none)
void SettingsManager::loadCollections(QList<CollectionConfig> &collections) {
  collections.clear();

  QSettings settings(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  QHash<QString, CollectionConfig> tempCollections;
  bool needsRewrite = false;

  // saveCollections() validates path-like keys before write, but a
  // hand-edited or attacker-controlled config can still inject shell
  // metacharacters / traversal. Mirror the write-side check on read so the
  // failure surfaces at startup instead of at first launch / first scan.
  auto sanitizeLoadedPath = [](const QString &value, const QString &fieldName,
                               const QString &collectionName) -> QString {
    if (value.isEmpty()) {
      return value;
    }
    auto security = PathUtils::validatePathSecurity(value);
    if (security.isError()) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::InvalidFilePath,
                                            QString("Refusing to load insecure %1").arg(fieldName),
                                            "SettingsManager::loadCollections")
              .withDetails(QString("Collection: %1, Value: %2, Reason: %3")
                               .arg(collectionName, value, security.error().message)));
      return QString();
    }
    return value;
  };

  QStringList groups = settings.childGroups();
  for (const QString &group : groups) {
    // Skip the global settings groups — they are not collections. Reading
    // them as collections is what spawned blank-named "ghost" rows at the
    // root that reappeared on every restart.
    if (reservedTopLevelGroups().contains(group)) {
      continue;
    }

    // Convert "Parent > Child" back to "Parent/Child" for internal hierarchy
    // processing
    QString internalGroupName = group;
    internalGroupName.replace(" > ", "/");

    settings.beginGroup(group);
    CollectionConfig config;

    auto sanitize = [&](const QString &value, const QString &fieldId) {
      return sanitizeLoadedPath(value, fieldId, config.name);
    };
    CollectionScalarsPersistence::load(settings, config, needsRewrite, sanitize);
    LauncherProfilePersistence::load(settings, config.launcher, needsRewrite, config.name,
                                     sanitize);
    FolderBrowsingOptionsPersistence::load(settings, config.folderBrowsing);
    ArchiveOptionsPersistence::load(settings, config.archive);
    ScraperOverridesPersistence::load(settings, config.scraperOverrides);
    GridLayoutPreferencesPersistence::load(settings, config.gridLayout);
    SidebarAppearancePersistence::load(settings, config.sidebar, config.name, sanitize);
    CollectionFilterPreferencesPersistence::load(settings, config.filter);
    CollectionBackgroundPersistence::load(settings, config.background, config.name, sanitize);
    ListViewOptionsPersistence::load(settings, config.listView);

    const CollectionConfig preClamp = config;
    config.clampValues();
    if (!(config == preClamp)) {
      // An out-of-range value was clamped. Persist the corrected form (rides
      // the existing needsRewrite -> finalizeCollections rewrite path, like the
      // extensions/path normalization) so the on-disk INI matches the in-memory
      // clamped state instead of re-clamping (and re-warning) every launch.
      // Kartend audit S-08.
      needsRewrite = true;
    }

    // Kartend-qc1c: surface paths that don't exist on this host (binary
    // uninstalled, NAS unmounted, kart imported from a different machine,
    // …) as a single per-collection warning so the user learns at startup
    // instead of at first launch / first paint. Values are preserved as-is
    // so the user can fix them once the underlying resource is back.
    QStringList pathIssues;
    auto recordIssue = [&](const QString &fieldName, const QString &value,
                           PathUtils::PathStatus status) {
      if (status == PathUtils::PathStatus::OK || status == PathUtils::PathStatus::Empty) {
        return;
      }
      pathIssues.append(QStringLiteral("%1 ('%2') %3")
                            .arg(fieldName, value, PathUtils::pathStatusDescription(status)));
    };
    recordIssue(QStringLiteral("launcherPath"), config.launcher.launcherPath,
                PathUtils::checkLauncherPath(config.launcher.launcherPath));
    for (int i = 0; i < config.launcher.additionalLaunchers.size(); ++i) {
      const auto &al = config.launcher.additionalLaunchers[i];
      recordIssue(QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i), al.launcherPath,
                  PathUtils::checkLauncherPath(al.launcherPath));
    }
    recordIssue(QStringLiteral("artworkDirectory"), config.artworkDirectory,
                PathUtils::checkDirectoryPath(config.artworkDirectory));
    recordIssue(QStringLiteral("placeholderArtwork"), config.placeholderArtwork,
                PathUtils::checkFilePath(config.placeholderArtwork));
    if (!pathIssues.isEmpty()) {
      qCWarning(lcSettingsManager).nospace()
          << "Collection '" << config.name
          << "': stored paths that no longer resolve on this host (values preserved):\n  - "
          << pathIssues.join(QStringLiteral("\n  - "));
    }

    // Use the internal name (with slashes) for hierarchy processing
    tempCollections[internalGroupName] = config;
    settings.endGroup();
  }

  finalizeCollections(tempCollections, collections, needsRewrite);

  // Seed the per-domain *Changed diff baseline. Without this, the first
  // saveCollections after launch would compare against an empty baseline
  // and fire every signal for every collection at once — noisy, and the
  // listeners would refresh state that hadn't actually changed since the
  // user opened the app.
  m_lastSavedCollectionFingerprints = buildFingerprintIndex(collections);

  // Validate loaded collections and log any issues
  auto validation = ConfigValidation::validateAllCollections(collections);
  ConfigValidation::logValidationResult(validation, "loadCollections");
  // Capture UUID collisions (the data-corruption subset) so the GUI startup
  // path can surface them in a modal warning rather than only logging them
  // (Kartend audit cj462). Reset every load; missing paths and other
  // non-fatal errors are intentionally not surfaced through this channel.
  m_collectionUuidCollisions = validation.collisions;

  // refresh the title-exclusion registry whenever the on-disk
  // collection list is reloaded so QueryManager / scroll consumers see the
  // patterns from the very first item fetched after launch.
  TitleFilter::rebuildFromCollections(collections);
}

// Persist collection configurations to disk (no lastSelected_* entries).
// Emits collectionsModified() at the end so all observers (toolbar type
// filter, hierarchy cache, sidebar summary) refresh consistently — fixes
// where right-click / kart-import / inline edits saved without
// firing a refresh, leaving the toolbar dropdown stale until restart.
ErrorUtils::Result<void>
SettingsManager::saveCollections(const QList<CollectionConfig> &collections) {
  // Perf trace (gated on KARTEND_PERF_TRACE=1) — saveCollections is called
  // from many UI events (sidebar drag commits, tab switches, kart imports,
  // toolbar filter toggles); the worry is that on a populous library the
  // synchronous QSettings write + atomicSync syscall is a per-click stall.
  // Drives the decision on Kartend-9kmb.
  const bool perfTrace = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE");
  QElapsedTimer perfWall;
  if (perfTrace) {
    perfWall.start();
  }

  QSettings settings(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  settings.setAtomicSyncRequired(true);

  // Validate path-like settings before persistence to prevent storing
  // potentially dangerous shell metacharacter injections in the config.
  // Empty paths are allowed (some fields are optional).
  auto sanitizePersistedPath = [&](const QString &value, const QString &fieldName,
                                   const QString &collectionName) -> QString {
    if (value.isEmpty()) {
      return value;
    }
    auto security = PathUtils::validatePathSecurity(value);
    if (security.isError()) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(
              ErrorUtils::ErrorCode::InvalidFilePath,
              QString("Refusing to persist insecure %1").arg(fieldName),
              "SettingsManager::saveCollections")
              .withDetails(QString("Collection: %1, Value: %2, Reason: %3")
                               .arg(collectionName, value, security.error().message)));
      return QString();
    }
    return value;
  };

  QStringList sectionNames;
  QHash<QString, int> sectionToIndex;
  QSet<QString> newGroupNames;

  for (int i = 0; i < collections.size(); ++i) {
    // synthesized playlist configs live in m_collections at
    // runtime so the rest of the UI treats them like real subcollections, but
    // they're persisted in the SQLite playlists table — never round-trip them
    // back into kartend.cfg, otherwise an INI section would shadow the DB row.
    if (collections[i].isPlaylist) {
      continue;
    }
    QString sectionName = CollectionUtils::hierarchicalNameFor(collections[i], collections);
    if (!sectionName.isEmpty()) {
      sectionNames.append(sectionName);
      sectionToIndex[sectionName] = i;

      // Convert "Parent/Child" to "Parent > Child" for INI group naming
      QString iniGroupName = sectionName;
      iniGroupName.replace("/", " > ");
      newGroupNames.insert(iniGroupName);
    }
  }
  sectionNames.sort();

  // Remove only groups that are NOT in the new collections list and NOT
  // one of the global INI groups owned by saveGeneralSettings (General,
  // Scrapers, Launchers/Launchers-array). Without the explicit whitelist,
  // every saveCollections call would clobber [Scrapers] and the Launchers
  // array — turning saveGeneralSettings's writes into no-ops on the next
  // collection mutation.
  // Top-level groups that live alongside the collection sections and
  // must survive a collection save. saveCollections walks every other
  // top-level group and removes it (so a renamed/removed collection
  // doesn't leave a stale section behind). Anything Kartend writes
  // outside the collection group hierarchy has to be in
  // reservedTopLevelGroups() or it gets silently wiped on every
  // collection mutation — which is exactly how `[ScraperOptions]` was
  // disappearing across restarts (every menu toggle / scraper edit
  // fires saveCollections).
  const QStringList existingGroups = settings.childGroups();
  for (const QString &group : existingGroups) {
    if (reservedTopLevelGroups().contains(group)) continue;
    if (newGroupNames.contains(group)) continue;
    settings.remove(group);
  }

  settings.beginGroup(keys::kGroupGeneral);
  // Kartend-4r340: the input keys (rememberSelection / wrapNavigation /
  // selectItemOnHover) are owned solely by saveGeneralSettings ->
  // InputSettingsPersistence::save. saveCollections used to ALSO write them —
  // two writers for the same keys, risking default/value drift. QSettings
  // preserves [General] keys this path doesn't touch (it never removes the
  // reserved [General] group), so they survive a collections-only save.
  // Kartend-w319x: still stamp the file-wide schema sentinel here, since
  // saveCollections can write a complete kartend.cfg before any
  // saveGeneralSettings call (first-run collection add / kart import) — without
  // it the [General] schema marker would read back as 0/legacy and re-migrate.
  settings.setValue(keys::kSchemaVersion, kSettingsSchemaVersion);
  settings.endGroup();

  for (const QString &sectionName : sectionNames) {
    int index = sectionToIndex[sectionName];
    const CollectionConfig &c = collections[index];

    // Convert "Parent/Child" to "Parent > Child" to prevent QSettings nesting
    QString iniGroupName = sectionName;
    iniGroupName.replace("/", " > ");

    settings.beginGroup(iniGroupName);
    auto sanitize = [&](const QString &value, const QString &fieldId) {
      return sanitizePersistedPath(value, fieldId, sectionName);
    };
    CollectionScalarsPersistence::save(settings, c, sectionName, sanitize);
    LauncherProfilePersistence::save(settings, c.launcher, sanitize);
    FolderBrowsingOptionsPersistence::save(settings, c.folderBrowsing);
    ArchiveOptionsPersistence::save(settings, c.archive);
    ScraperOverridesPersistence::save(settings, c.scraperOverrides);
    GridLayoutPreferencesPersistence::save(settings, c.gridLayout);
    SidebarAppearancePersistence::save(settings, c.sidebar, sanitize);
    CollectionFilterPreferencesPersistence::save(settings, c.filter);
    CollectionBackgroundPersistence::save(settings, c.background, sanitize);
    ListViewOptionsPersistence::save(settings, c.listView);
    settings.endGroup();
  }
  settings.sync();

  ErrorUtils::ErrorContext err;
  if (settings.status() != QSettings::NoError) {
    err = ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                            "Failed to persist settings",
                                            "SettingsManager::saveCollections")
              .withDetails(QString("Path: %1, Status: %2")
                               .arg(SettingsUtils::getConfigPath())
                               .arg(static_cast<int>(settings.status())));
    ErrorUtils::logError(err);
  }

  // QSettings writes the file via temp + atomic rename. The rename metadata
  // still has to hit the disk journal to survive a power loss — fsync the
  // parent directory. Mirrors saveGeneralSettings; syncDirectory tolerates
  // filesystems that don't support directory fsync (returns true on EINVAL).
  const QString configPath = SettingsUtils::getConfigPath();
  if (!PathUtils::syncDirectory(QFileInfo(configPath).path())) {
    qCWarning(lcSettingsManager) << "syncDirectory failed for" << QFileInfo(configPath).path()
                                 << "— atomic rename completed but its durability across a power "
                                    "loss is no longer guaranteed.";
  }

  // Cleartext scraper credentials share this INI; clamp it to 0600 after
  // every save site that may have touched it.
  SettingsUtils::tightenConfigPermissions();

  // keep the registry in sync with the just-persisted list. The
  // toolbar popup calls saveCollections() after edits and then triggers a
  // collection reload — refreshing here means the reload sees the new
  // patterns even before loadCollections() runs again.
  TitleFilter::rebuildFromCollections(collections);

  // notify observers regardless of how the save was initiated.
  // The settings dialog flow used to emit this from the dialog controller;
  // moving the emit here covers all paths uniformly (right-click edits, kart
  // imports, inline toolbar edits) without ad-hoc per-call-site additions.
  emit collectionsModified();

  if (perfTrace) {
    int realCount = 0;
    int playlistCount = 0;
    for (const auto &c : collections) {
      if (c.isPlaylist) {
        ++playlistCount;
      } else {
        ++realCount;
      }
    }
    qCDebug(lcPerfTrace).nospace()
        << "saveCollections: totalMs=" << perfWall.elapsed()
        << " collections=" << collections.size() << " (real=" << realCount
        << " playlists=" << playlistCount << ")"
        << " path=" << SettingsUtils::getConfigPath();
  }

  if (err.isError()) {
    return err;
  }

  // Commit the diff baseline BEFORE emitting the per-domain *Changed signals.
  // A slot may synchronously re-enter saveCollections(): the live path is
  // sidebarAppearanceChanged -> DetailsPaneManager::onSidebarAppearanceChanged
  // -> updateSidebarLayout -> saveSidebarStateForCollection -> saveCollections.
  // If the baseline were still the pre-save snapshot during that re-entry, the
  // same field would re-diff and re-emit at every level, recursing without
  // bound until the stack aborts (SIGABRT on details-sidebar tab switch).
  // Snapshot the prior baseline, commit the new one, then diff against the
  // snapshot so re-entrant saves observe no change and stop.
  //
  // Kartend-lc58a: the baseline is now a per-collection fingerprint map rather
  // than a full by-value QList<CollectionConfig>, so committing it no longer
  // deep-copies every collection's embedded QHash/QList (and no longer shares
  // copy-on-write storage with the live list, which used to force that deep copy
  // on the live list's next mutation — a sidebar drag, tab switch, filter
  // toggle). Each leaf cluster's fingerprint comes from its co-located qHash;
  // see collectiondifffingerprint.h. Move the old map out before overwriting so
  // the diff reads the exact pre-save baseline.
  const QHash<QString, CollectionDiffFingerprint> previousFingerprints =
      std::move(m_lastSavedCollectionFingerprints);
  m_lastSavedCollectionFingerprints = buildFingerprintIndex(collections);
  emitPerCollectionDiffs(previousFingerprints, collections);

  return ErrorUtils::Result<void>::success();
}

void SettingsManager::emitPerCollectionDiffs(
    const QHash<QString, CollectionDiffFingerprint> &previous,
    const QList<CollectionConfig> &collections) {
  // Identity is (name, mediaDirectory) UUID so a reorder of unchanged
  // collections doesn't fire spurious diffs; an added collection has no
  // matching baseline entry and skips the diff (collectionsModified covers the
  // add/remove lifecycle separately).
  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &newCfg = collections[i];
    const QString uuid = CollectionUtils::computeCollectionUuid(newCfg.name, newCfg.mediaDirectory);
    const auto it = previous.constFind(uuid);
    if (it == previous.constEnd()) {
      continue;
    }
    const CollectionDiffFingerprint &oldFp = it.value();
    const CollectionDiffFingerprint newFp = collectionDiffFingerprint(newCfg);

    // Compare per-cluster fingerprints, not the values themselves. Equal
    // clusters always fingerprint equal, so a real change is never missed; the
    // emit still carries the live NEW value (not the hash) to listeners.
    if (oldFp.gridLayout != newFp.gridLayout) emit gridLayoutChanged(i, newCfg.gridLayout);
    if (oldFp.sidebar != newFp.sidebar) emit sidebarAppearanceChanged(i, newCfg.sidebar);
    if (oldFp.background != newFp.background)
      emit collectionBackgroundChanged(i, newCfg.background);
    if (oldFp.listView != newFp.listView) emit listViewOptionsChanged(i, newCfg.listView);
    if (oldFp.archive != newFp.archive) emit archiveOptionsChanged(i, newCfg.archive);
    if (oldFp.folderBrowsing != newFp.folderBrowsing)
      emit folderBrowsingOptionsChanged(i, newCfg.folderBrowsing);
    if (oldFp.filter != newFp.filter) emit collectionFilterPreferencesChanged(i, newCfg.filter);
    if (oldFp.scraperOverrides != newFp.scraperOverrides)
      emit scraperOverridesChanged(i, newCfg.scraperOverrides);
    if (oldFp.launcher != newFp.launcher) emit launcherProfileChanged(i, newCfg.launcher);
  }
}
