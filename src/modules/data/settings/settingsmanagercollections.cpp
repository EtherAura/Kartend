// Collection load/save helpers, plus the per-collection diff/signal walk
// emitted from saveCollections. The hierarchy-build algorithm lives in
// collectionhierarchybuilder.* (parents-first walk, subcollection
// re-parenting); per-leaf-cluster persistence lives in
// utils/app/collection/*_persistence.{h,cpp}.
#include "settingsmanager.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "collectionhierarchybuilder.h"
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
  static const QSet<QString> groups{QStringLiteral("General"), QStringLiteral("Scrapers"),
                                    QStringLiteral("ScraperOptions"), QStringLiteral("Launchers")};
  return groups;
}

} // namespace

void SettingsManager::finalizeCollections(const QHash<QString, CollectionConfig> &tempCollections,
                                          QList<CollectionConfig> &collections,
                                          const bool &needsRewrite) {
  CollectionHierarchyBuilder::build(tempCollections, collections);
  if (needsRewrite) {
    (void)saveCollections(collections);
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

    config.clampValues();

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
  m_lastSavedCollections = collections;

  // Validate loaded collections and log any issues
  auto validation = ConfigValidation::validateAllCollections(collections);
  ConfigValidation::logValidationResult(validation, "loadCollections");

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
  settings.setValue(keys::kRememberSelection, m_generalSettings.input.rememberSelection);
  settings.setValue(keys::kWrapNavigation, m_generalSettings.input.wrapNavigation);
  settings.setValue(keys::kSelectItemOnHover, m_generalSettings.input.selectItemOnHover);
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
  const QList<CollectionConfig> previouslySaved = m_lastSavedCollections;
  m_lastSavedCollections = collections;
  emitPerCollectionDiffs(previouslySaved, collections);

  return ErrorUtils::Result<void>::success();
}

void SettingsManager::emitPerCollectionDiffs(const QList<CollectionConfig> &previous,
                                             const QList<CollectionConfig> &collections) {
  // Identity is (name, mediaDirectory) UUID so a reorder of unchanged
  // collections doesn't fire spurious diffs; an added collection has no
  // matching old entry and skips the diff (collectionsModified covers the
  // add/remove lifecycle separately).
  QHash<QString, const CollectionConfig *> oldByUuid;
  oldByUuid.reserve(previous.size());
  for (const CollectionConfig &oldCfg : previous) {
    const QString uuid = CollectionUtils::computeCollectionUuid(oldCfg.name, oldCfg.mediaDirectory);
    oldByUuid.insert(uuid, &oldCfg);
  }

  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &newCfg = collections[i];
    const QString uuid = CollectionUtils::computeCollectionUuid(newCfg.name, newCfg.mediaDirectory);
    const auto it = oldByUuid.constFind(uuid);
    if (it == oldByUuid.constEnd()) {
      continue;
    }
    const CollectionConfig &oldCfg = *(it.value());

    if (oldCfg.gridLayout != newCfg.gridLayout) emit gridLayoutChanged(i, newCfg.gridLayout);
    if (oldCfg.sidebar != newCfg.sidebar) emit sidebarAppearanceChanged(i, newCfg.sidebar);
    if (oldCfg.background != newCfg.background)
      emit collectionBackgroundChanged(i, newCfg.background);
    if (oldCfg.listView != newCfg.listView) emit listViewOptionsChanged(i, newCfg.listView);
    if (oldCfg.archive != newCfg.archive) emit archiveOptionsChanged(i, newCfg.archive);
    if (oldCfg.folderBrowsing != newCfg.folderBrowsing)
      emit folderBrowsingOptionsChanged(i, newCfg.folderBrowsing);
    if (oldCfg.filter != newCfg.filter) emit collectionFilterPreferencesChanged(i, newCfg.filter);
    if (oldCfg.scraperOverrides != newCfg.scraperOverrides)
      emit scraperOverridesChanged(i, newCfg.scraperOverrides);
    if (oldCfg.launcher != newCfg.launcher) emit launcherProfileChanged(i, newCfg.launcher);
  }
}
