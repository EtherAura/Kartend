// Collection load/save + finalize helpers extracted from settingsmanager.cpp:
//   - finalizeCollections (~28 LOC)
//   - loadCollections (~124 LOC)
//   - saveCollections (~147 LOC)
// The anonymous namespace helpers (findParentCollectionIndex,
// processSubcollection) used by finalizeCollections move with them.
#include "settingsmanager.h"

#include <QDir>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "loggingcategories.h"

#include "collectionutils.h"
#include "configvalidation.h"
#include "errorutils.h"
#include "extensionutils.h"
#include "pathutils.h"
#include "settingskeys.h"
#include "settingsutils.h"
#include "titlefilter.h"
#include "uiconstants/detailspane.h"
#include "uiconstants/grid.h"
#include "uiconstants/item.h"
#include "uiconstants/listview.h"

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

auto findParentCollectionIndex(const QStringList &parts, const QString &immediateParentName,
                               const QList<CollectionConfig> &collections) -> int {
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].name == immediateParentName) {
      if (parts.size() == 2 && !collections[i].isSubcollection) {
        return i;
      }
      if (parts.size() > 2) {
        QStringList parentPath = parts.mid(0, parts.size() - 1);
        QString expectedParentPath = parentPath.join('/');
        QString actualParentPath =
            CollectionUtils::hierarchicalNameFor(collections[i], collections);
        if (actualParentPath == expectedParentPath) {
          return i;
        }
      }
    }
  }
  return -1;
}

auto processSubcollection(const QString &sectionName, CollectionConfig &collection,
                          QList<CollectionConfig> &collections) -> void {
  QStringList parts = sectionName.split('/', Qt::KeepEmptyParts);
  if (parts.size() < 2) {
    return;
  }

  const QString &immediateParentName = parts[parts.size() - 2];
  int parentIndex = findParentCollectionIndex(parts, immediateParentName, collections);

  if (parentIndex >= 0) {
    collection.parentCollectionIndex = parentIndex;
    collection.isSubcollection = true;
    collections.append(collection);
  }
}

} // namespace

void SettingsManager::finalizeCollections(const QHash<QString, CollectionConfig> &tempCollections,
                                          QList<CollectionConfig> &collections,
                                          const bool &needsRewrite) {
  QStringList sectionNames = tempCollections.keys();
  sectionNames.sort();

  // Add parent collections first
  for (const QString &sectionName : sectionNames) {
    CollectionConfig collection = tempCollections[sectionName];
    if (!sectionName.contains('/')) {
      collection.isSubcollection = false;
      collection.parentCollectionIndex = -1;
      collections.append(collection);
    }
  }

  // Add subcollections
  for (const QString &sectionName : sectionNames) {
    if (sectionName.contains('/')) {
      CollectionConfig collection = tempCollections[sectionName];
      processSubcollection(sectionName, collection, collections);
    }
  }

  if (needsRewrite) {
    saveCollections(collections);
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

    // Strict round-trip preservation: capture every flat child key in this
    // collection section into config.preservedKeys, minus a small blocklist
    // of legacy keys that the codebase intentionally strips on save. On
    // re-save, preservedKeys is written first and the known-field
    // setValue() calls below overwrite the duplicates, so the net effect is
    // that future-version keys this build doesn't recognise survive a
    // load-modify-save cycle instead of being silently dropped.
    {
      static const QStringList kPreservationBlocklist = {
          keys::kVideoDirectory,
          keys::kManualDirectory,
      };
      for (const QString &k : settings.childKeys()) {
        if (k.contains(QLatin1Char('/'))) continue;
        if (kPreservationBlocklist.contains(k)) continue;
        config.preservedKeys.insert(k, settings.value(k));
      }
    }

    config.name = settings.value(keys::kName).toString();
    // free-form category label. Empty means "untagged" — the
    // sidebar/toolbar filter resolves an empty type by walking up the parent
    // chain via CollectionUtils::effectiveCollectionType.
    config.type = settings.value(keys::kType).toString().trimmed();
    config.launcher.launcherPath = sanitizeLoadedPath(
        settings.value(keys::kLauncherPath).toString(), "launcherPath", config.name);
    config.launcher.corePath =
        sanitizeLoadedPath(settings.value(keys::kCorePath).toString(), "corePath", config.name);
    config.launcher.launchParameters = settings.value(keys::kLaunchParameters).toString();
    config.launcher.launcherName = settings.value(keys::kLauncherName).toString();
    // additional launchers are stored as a QSettings array under
    // "additionalLaunchers". When the key is absent (legacy configs), the
    // collection just has the primary launcher and the array stays empty.
    const int additionalCount = settings.beginReadArray(keys::kAdditionalLaunchers);
    config.launcher.additionalLaunchers.reserve(additionalCount);
    for (int i = 0; i < additionalCount; ++i) {
      settings.setArrayIndex(i);
      LauncherConfig launcher;
      launcher.name = settings.value(keys::kName).toString();
      const QString launcherFieldId = QString("additionalLaunchers[%1].launcherPath").arg(i);
      const QString coreFieldId = QString("additionalLaunchers[%1].corePath").arg(i);
      launcher.launcherPath = sanitizeLoadedPath(settings.value(keys::kLauncherPath).toString(),
                                                 launcherFieldId, config.name);
      launcher.corePath =
          sanitizeLoadedPath(settings.value(keys::kCorePath).toString(), coreFieldId, config.name);
      launcher.launchParameters = settings.value(keys::kLaunchParameters).toString();
      // optional reference to a global preset.
      launcher.presetId = settings.value(keys::kPresetId).toString();
      config.launcher.additionalLaunchers.append(launcher);
    }
    settings.endArray();
    // alias parents — names of additional collections this
    // collection should appear under. Stored as a QSettings string array so
    // names can contain commas/semicolons without escaping concerns.
    const int additionalParentsCount = settings.beginReadArray(keys::kAdditionalParents);
    config.additionalParentNames.reserve(additionalParentsCount);
    for (int i = 0; i < additionalParentsCount; ++i) {
      settings.setArrayIndex(i);
      const QString parentName = settings.value(keys::kName).toString();
      if (!parentName.isEmpty()) {
        config.additionalParentNames.append(parentName);
      }
    }
    settings.endArray();
    config.launcher.defaultLauncherIndex = settings.value(keys::kDefaultLauncherIndex, 0).toInt();
    config.mediaDirectory = sanitizeLoadedPath(settings.value(keys::kMediaDirectory).toString(),
                                               "mediaDirectory", config.name);
    config.artworkDirectory = sanitizeLoadedPath(settings.value(keys::kArtworkDirectory).toString(),
                                                 "artworkDirectory", config.name);
    // videoDirectory / manualDirectory were collapsed into the single-
    // root artwork layout (`{artwork}/video/`, `{artwork}/manual/`).
    // Don't read them — the struct fields stay default-empty and the
    // consumer code paths fall back to the artwork subdirs. The
    // saved-side `settings.remove(...)` calls below scrub any stale
    // values out of the INI on the next save.
    config.videoDirectory.clear();
    config.manualDirectory.clear();
    config.placeholderArtwork = sanitizeLoadedPath(
        settings.value(keys::kPlaceholderArtwork).toString(), "placeholderArtwork", config.name);
    config.folderBrowsing.includeContentSubfolders =
        settings.value(keys::kIncludeContentSubfolders, false).toBool();
    config.folderBrowsing.includeArtworkSubfolders =
        settings.value(keys::kIncludeArtworkSubfolders, false).toBool();
    config.folderBrowsing.showAllSubfolderItems =
        settings.value(keys::kShowAllSubfolderItems, false).toBool();
    config.folderBrowsing.hideSubfolderTitles =
        settings.value(keys::kHideSubfolderTitles, false).toBool();
    config.folderBrowsing.showHiddenFolders =
        settings.value(keys::kShowHiddenFolders, false).toBool();
    config.archive.extractArchives = settings.value(keys::kExtractArchives, false).toBool();
    config.archive.extractedExtension = settings.value(keys::kExtractedExtension).toString();
    config.expandMode = settings.value(keys::kExpandMode, false).toBool();
    config.watchFilesystem = settings.value(keys::kWatchFilesystem, false).toBool();
    config.collectionIcon = settings.value(keys::kCollectionIcon).toString();

    QString extStr = settings.value(keys::kExtensions).toString();
    QStringList rawList = extStr.split(',', Qt::SkipEmptyParts);
    for (QString &extension : rawList) {
      extension = extension.trimmed();
    }
    QStringList normalized = ExtensionUtils::normalizeStoredExtensions(rawList);
    if (normalized != rawList) {
      needsRewrite = true;
    }
    config.extensions = normalized;

    // User-defined custom artwork type ids. Stored as
    // comma-separated values; each token is trimmed. Empty tokens and
    // duplicates are dropped at load time so a sloppy edit can't wedge the
    // sidebar gallery (the type id doubles as the artwork_type DB key).
    QString customArtTypesStr = settings.value(keys::kCustomArtworkTypes).toString();
    QStringList customArtTypes = customArtTypesStr.split(',', Qt::SkipEmptyParts);
    QStringList cleanedCustomTypes;
    cleanedCustomTypes.reserve(customArtTypes.size());
    for (QString &type : customArtTypes) {
      type = type.trimmed();
      if (!type.isEmpty() && !cleanedCustomTypes.contains(type)) {
        cleanedCustomTypes.append(type);
      }
    }
    config.customArtworkTypes = cleanedCustomTypes;

    // ScreenScraper systemeid override; -1 = autodetect at scrape time.
    config.scraperOverrides.screenscraperSystemId =
        settings.value(keys::kScreenscraperSystemId, -1).toInt();
    // Hash inner ROM in archives. Default true — better SS match accuracy.
    config.scraperOverrides.screenscraperHashArchive =
        settings.value(keys::kScreenscraperHashArchive, true).toBool();
    // Per-collection scraper override; empty = automatic (resolve by type).
    config.scraperOverrides.scraperProviderId =
        settings.value(keys::kScraperProviderId).toString().trimmed();
    // datFilePaths is persisted as a QSettings array so individual
    // entries can contain commas / brackets without delimiter
    // escaping. Legacy configs (pre-multi-DAT) shipped a single
    // `datFilePath=` key — when the array is absent and the legacy
    // key has a value, upgrade it to a one-entry list so the user
    // doesn't lose their existing DAT on first launch after upgrade.
    const int datPathCount = settings.beginReadArray(keys::kDatFilePaths);
    config.scraperOverrides.datFilePaths.clear();
    config.scraperOverrides.datFilePaths.reserve(datPathCount);
    for (int i = 0; i < datPathCount; ++i) {
      settings.setArrayIndex(i);
      const QString path = settings.value(keys::kPath).toString();
      if (!path.isEmpty()) config.scraperOverrides.datFilePaths.append(path);
    }
    settings.endArray();
    if (config.scraperOverrides.datFilePaths.isEmpty()) {
      const QString legacy = settings.value(keys::kDatFilePath).toString();
      if (!legacy.isEmpty()) config.scraperOverrides.datFilePaths.append(legacy);
    }

    config.gridLayout.gridWidth = settings.value(keys::kGridWidth, 4).toInt();
    config.gridLayout.horizontalGridHeight = settings.value(keys::kHorizontalGridHeight, 0).toInt();
    config.gridLayout.gridWidthSidebarHidden =
        settings.value(keys::kGridWidthSidebarHidden, 0).toInt();
    config.gridLayout.horizontalGridHeightSidebarHidden =
        settings.value(keys::kHorizontalGridHeightSidebarHidden, 0).toInt();
    // alt items-per-column when a Top/Bottom-docked details pane
    // hides in Expand mode. 0 means "inherit gridWidth" — preserves existing
    // behavior for collections that haven't opted in.
    config.gridLayout.gridHeightSidebarHidden =
        settings.value(keys::kGridHeightSidebarHidden, 0).toInt();
    config.sidebar.sidebarVisible = settings.value(keys::kSidebarVisible, false).toBool();
    config.showAllSubcollectionItems =
        settings.value(keys::kShowAllSubcollectionItems, false).toBool();
    config.horizontalAlignment = CollectionUtils::stringToAlignment(
        settings.value(keys::kHorizontalAlignment, "center").toString());
    config.sidebar.sidebarMode =
        (settings.value(keys::kSidebarMode, "overlay").toString() == "fixed")
            ? DetailsPaneMode::Expand
            : DetailsPaneMode::Overlay;
    // sidebar enhancements.
    config.sidebar.sidebarPosition = CollectionUtils::stringToDetailsPanePosition(
        settings.value(keys::kSidebarPosition, "right").toString());
    config.sidebar.sidebarBackgroundType = CollectionUtils::stringToDetailsPaneBackgroundType(
        settings.value(keys::kSidebarBackgroundType, "color").toString());
    config.sidebar.sidebarBackgroundColor =
        settings.value(keys::kSidebarBackgroundColor).toString();
    config.sidebar.sidebarBackgroundImage =
        sanitizeLoadedPath(settings.value(keys::kSidebarBackgroundImage).toString(),
                           "sidebarBackgroundImage", config.name);
    config.sidebar.sidebarPattern = CollectionUtils::stringToDetailsPanePattern(
        settings.value(keys::kSidebarPattern, "crosshatch").toString());
    config.sidebar.sidebarPatternIntensity =
        settings.value(keys::kSidebarPatternIntensity, 50).toInt();
    config.sidebar.sidebarPatternColor = settings.value(keys::kSidebarPatternColor).toString();
    config.sidebar.sidebarTextColor = settings.value(keys::kSidebarTextColor).toString();
    config.sidebar.sidebarAccentColor = settings.value(keys::kSidebarAccentColor).toString();
    config.sidebar.sidebarHeaderBgColor = settings.value(keys::kSidebarHeaderBgColor).toString();
    config.sidebar.sidebarSectionBgColor = settings.value(keys::kSidebarSectionBgColor).toString();
    config.sidebar.sidebarHeaderBgOpacity =
        settings.value(keys::kSidebarHeaderBgOpacity, 200).toInt();
    config.sidebar.sidebarSectionBgOpacity =
        settings.value(keys::kSidebarSectionBgOpacity, 170).toInt();
    config.sidebar.sidebarWidth =
        settings.value(keys::kSidebarWidth, UIConstants::DetailsPane::FIXED_WIDTH).toInt();
    // pane height for Top/Bottom dock. Same persistence treatment
    // as sidebarWidth (no migration of older configs needed — the default is
    // applied when the key is absent).
    config.sidebar.sidebarHeight =
        settings.value(keys::kSidebarHeight, UIConstants::DetailsPane::FIXED_HEIGHT).toInt();
    config.sidebar.sidebarWidthLocked = settings.value(keys::kSidebarWidthLocked, true).toBool();
    config.sidebar.sidebarActiveTab = CollectionUtils::stringToDetailsPaneTab(
        settings.value(keys::kSidebarActiveTab, "item").toString());
    config.viewType =
        CollectionUtils::stringToViewType(settings.value(keys::kViewType, "grid").toString());
    config.hideMissingArtwork = settings.value(keys::kHideMissingArtwork, false).toBool();
    config.gridLayout.hideHorizontalScrollbar =
        settings.value(keys::kHideHorizontalScrollbar, false).toBool();
    config.gridLayout.hideVerticalScrollbar =
        settings.value(keys::kHideVerticalScrollbar, false).toBool();
    config.hideTitles = settings.value(keys::kHideTitles, false).toBool();
    config.hideSubcollectionTitles = settings.value(keys::kHideSubcollectionTitles, false).toBool();
    // title-exclusion patterns are stored as a QSettings array so
    // each pattern can contain commas / brackets / backslashes without
    // delimiter escaping concerns. titleExclusionEnabled defaults to true so a
    // user adding patterns sees them apply immediately.
    const int titleExcludeCount = settings.beginReadArray(keys::kTitleExclusionPatterns);
    config.filter.titleExclusionPatterns.reserve(titleExcludeCount);
    for (int i = 0; i < titleExcludeCount; ++i) {
      settings.setArrayIndex(i);
      const QString pattern = settings.value(keys::kPattern).toString();
      if (!pattern.isEmpty()) {
        config.filter.titleExclusionPatterns.append(pattern);
      }
    }
    settings.endArray();
    config.filter.titleExclusionEnabled =
        settings.value(keys::kTitleExclusionEnabled, true).toBool();
    config.gridLayout.horizontalSpacing =
        settings.value(keys::kHorizontalSpacing, UIConstants::Grid::SPACING).toInt();
    config.gridLayout.verticalSpacing = settings.value(keys::kVerticalSpacing, 20).toInt();
    config.gridLayout.itemWidth =
        settings.value(keys::kItemWidth, UIConstants::Item::DEFAULT_WIDTH).toInt();
    config.gridLayout.itemHeight =
        settings.value(keys::kItemHeight, UIConstants::Item::DEFAULT_HEIGHT).toInt();
    config.gridLayout.fontSize =
        settings.value(keys::kFontSize, UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
    config.gridLayout.cornerRadius =
        settings.value(keys::kCornerRadius, UIConstants::Item::DEFAULT_CORNER_RADIUS).toInt();

    // Background settings
    QString bgType = settings.value(keys::kBackgroundType, "color").toString().toLower();
    if (bgType == "image") {
      config.background.backgroundType = BackgroundType::Image;
    } else if (bgType == "video") {
      config.background.backgroundType = BackgroundType::Video;
    } else {
      config.background.backgroundType = BackgroundType::Color;
    }
    config.background.backgroundColor = settings.value(keys::kBackgroundColor).toString();
    config.background.backgroundImage = sanitizeLoadedPath(
        settings.value(keys::kBackgroundImage).toString(), "backgroundImage", config.name);
    config.background.backgroundVideo = sanitizeLoadedPath(
        settings.value(keys::kBackgroundVideo).toString(), "backgroundVideo", config.name);
    config.background.primaryColor = settings.value(keys::kPrimaryColor).toString();
    config.background.tileColor = settings.value(keys::kTileColor).toString();
    config.background.selectionColor = settings.value(keys::kSelectionColor).toString();

    // header logo
    config.background.headerLogoImage = sanitizeLoadedPath(
        settings.value(keys::kHeaderLogoImage).toString(), "headerLogoImage", config.name);
    config.background.headerLogoPosition = CollectionUtils::stringToHeaderLogoPosition(
        settings.value(keys::kHeaderLogoPosition, "topcenter").toString());

    // vignette
    config.background.vignetteEnabled = settings.value(keys::kVignetteEnabled, false).toBool();
    config.background.vignetteIntensity = settings.value(keys::kVignetteIntensity, 60).toInt();

    // wallpaper parallax
    config.background.wallpaperParallax = settings.value(keys::kWallpaperParallax, false).toBool();
    config.background.parallaxStrength = settings.value(keys::kParallaxStrength, 30).toInt();

    // toolbar backdrop blur
    config.background.toolbarBackdropBlur =
        settings.value(keys::kToolbarBackdropBlur, false).toBool();
    config.background.backdropBlurRadius = settings.value(keys::kBackdropBlurRadius, 12).toInt();

    // List mode settings
    config.listView.listFontSize =
        settings.value(keys::kListFontSize, UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
    config.listView.listRowHeight =
        settings.value(keys::kListRowHeight, UIConstants::ListView::DEFAULT_ROW_HEIGHT).toInt();
    config.listView.listRowColor = settings.value(keys::kListRowColor).toString();
    config.listView.listAltRowColor = settings.value(keys::kListAltRowColor).toString();

    // Text appearance settings (per-collection)
    config.customFontFamily = settings.value(keys::kCustomFontFamily).toString();

    // sidebar font override.
    config.sidebar.sidebarFontFamily = settings.value(keys::kSidebarFontFamily).toString();
    config.sidebar.sidebarFontPointSize = settings.value(keys::kSidebarFontPointSize, 0).toInt();

    // Validate and clamp numeric values to acceptable ranges
    config.clampValues();

    // Use the internal name (with slashes) for hierarchy processing
    tempCollections[internalGroupName] = config;
    settings.endGroup();
  }

  finalizeCollections(tempCollections, collections, needsRewrite);

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
  settings.setValue(keys::kRememberSelection, m_generalSettings.rememberSelection);
  settings.setValue(keys::kWrapNavigation, m_generalSettings.wrapNavigation);
  settings.setValue(keys::kSelectItemOnHover, m_generalSettings.selectItemOnHover);
  settings.endGroup();

  for (const QString &sectionName : sectionNames) {
    int index = sectionToIndex[sectionName];
    const CollectionConfig &c = collections[index];

    // Convert "Parent/Child" to "Parent > Child" to prevent QSettings nesting
    QString iniGroupName = sectionName;
    iniGroupName.replace("/", " > ");

    settings.beginGroup(iniGroupName);
    // Strict round-trip preservation: replay every flat key captured at
    // load time before any known-field setValue() calls below. The
    // ordering matters: known keys must overwrite duplicates so the
    // user's current struct values win over the stashed copy. Future-
    // version keys (or hand-edited keys this build doesn't otherwise
    // touch) survive an older-build save.
    for (auto it = c.preservedKeys.constBegin(); it != c.preservedKeys.constEnd(); ++it) {
      settings.setValue(it.key(), it.value());
    }
    settings.setValue(keys::kName, c.name);
    // persist the free-form category label. Stored verbatim
    // (whitespace already trimmed at load) so a hand-edit round-trips.
    settings.setValue(keys::kType, c.type);
    settings.setValue(keys::kLauncherPath,
                      sanitizePersistedPath(c.launcher.launcherPath, "launcherPath", sectionName));
    settings.setValue(keys::kCorePath,
                      sanitizePersistedPath(c.launcher.corePath, "corePath", sectionName));
    settings.setValue(keys::kLaunchParameters, c.launcher.launchParameters);
    settings.setValue(keys::kLauncherName, c.launcher.launcherName);
    // persist the additional-launcher list as a QSettings array.
    // beginWriteArray clears any existing entries with the same prefix, so
    // launchers removed via the dialog don't linger in the INI.
    settings.beginWriteArray(keys::kAdditionalLaunchers, c.launcher.additionalLaunchers.size());
    for (int i = 0; i < c.launcher.additionalLaunchers.size(); ++i) {
      settings.setArrayIndex(i);
      const LauncherConfig &launcher = c.launcher.additionalLaunchers[i];
      settings.setValue(keys::kName, launcher.name);
      const QString launcherFieldId = QString("additionalLaunchers[%1].launcherPath").arg(i);
      const QString coreFieldId = QString("additionalLaunchers[%1].corePath").arg(i);
      settings.setValue(keys::kLauncherPath,
                        sanitizePersistedPath(launcher.launcherPath, launcherFieldId, sectionName));
      settings.setValue(keys::kCorePath,
                        sanitizePersistedPath(launcher.corePath, coreFieldId, sectionName));
      settings.setValue(keys::kLaunchParameters, launcher.launchParameters);
      // persist the preset reference (empty when inline).
      settings.setValue(keys::kPresetId, launcher.presetId);
    }
    settings.endArray();
    // persist the alias-parent name list as a QSettings array.
    // beginWriteArray clears prior entries so removals propagate.
    settings.beginWriteArray(keys::kAdditionalParents, c.additionalParentNames.size());
    for (int i = 0; i < c.additionalParentNames.size(); ++i) {
      settings.setArrayIndex(i);
      settings.setValue(keys::kName, c.additionalParentNames[i]);
    }
    settings.endArray();
    settings.setValue(keys::kDefaultLauncherIndex, c.launcher.defaultLauncherIndex);
    settings.setValue(keys::kMediaDirectory,
                      sanitizePersistedPath(c.mediaDirectory, "mediaDirectory", sectionName));
    settings.setValue(keys::kArtworkDirectory,
                      sanitizePersistedPath(c.artworkDirectory, "artworkDirectory", sectionName));
    // Legacy keys — drop them from the INI on every save so stale
    // values (e.g. an unplugged drive, a path typo) don't linger as
    // configvalidation warnings forever. The single-root artwork
    // layout has `{artwork}/video/` and `{artwork}/manual/` as the
    // canonical home for these media types.
    settings.remove(keys::kVideoDirectory);
    settings.remove(keys::kManualDirectory);
    settings.setValue(
        "placeholderArtwork",
        sanitizePersistedPath(c.placeholderArtwork, "placeholderArtwork", sectionName));
    settings.setValue(keys::kIncludeContentSubfolders, c.folderBrowsing.includeContentSubfolders);
    settings.setValue(keys::kIncludeArtworkSubfolders, c.folderBrowsing.includeArtworkSubfolders);
    settings.setValue(keys::kShowAllSubfolderItems, c.folderBrowsing.showAllSubfolderItems);
    settings.setValue(keys::kHideSubfolderTitles, c.folderBrowsing.hideSubfolderTitles);
    settings.setValue(keys::kShowHiddenFolders, c.folderBrowsing.showHiddenFolders);
    settings.setValue(keys::kExtractArchives, c.archive.extractArchives);
    settings.setValue(keys::kExtractedExtension, c.archive.extractedExtension);
    settings.setValue(keys::kExpandMode, c.expandMode);
    settings.setValue(keys::kWatchFilesystem, c.watchFilesystem);
    settings.setValue(keys::kCollectionIcon, c.collectionIcon);
    settings.setValue(keys::kExtensions, c.extensions.join(", "));
    settings.setValue(keys::kCustomArtworkTypes, c.customArtworkTypes.join(", "));
    // ScreenScraper systemeid override (round-trips even when -1 so the
    // distinction between "I haven't decided yet" and "autodetect" is
    // explicit on disk for users hand-editing the file).
    settings.setValue(keys::kScreenscraperSystemId, c.scraperOverrides.screenscraperSystemId);
    settings.setValue(keys::kScreenscraperHashArchive, c.scraperOverrides.screenscraperHashArchive);
    // Per-collection scraper override; empty = automatic (resolve by type).
    settings.setValue(keys::kScraperProviderId, c.scraperOverrides.scraperProviderId);
    // Persist datFilePaths via beginWriteArray so removed entries
    // disappear cleanly from the INI. Also drop any stale single-key
    // `datFilePath=` left behind by a pre-upgrade config — keeping
    // both around would confuse hand-editors and round-tripping.
    settings.remove(keys::kDatFilePath);
    settings.beginWriteArray(keys::kDatFilePaths, c.scraperOverrides.datFilePaths.size());
    for (int i = 0; i < c.scraperOverrides.datFilePaths.size(); ++i) {
      settings.setArrayIndex(i);
      settings.setValue(keys::kPath, c.scraperOverrides.datFilePaths[i]);
    }
    settings.endArray();
    settings.setValue(keys::kGridWidth, c.gridLayout.gridWidth);
    settings.setValue(keys::kHorizontalGridHeight, c.gridLayout.horizontalGridHeight);
    settings.setValue(keys::kGridWidthSidebarHidden, c.gridLayout.gridWidthSidebarHidden);
    settings.setValue(keys::kHorizontalGridHeightSidebarHidden,
                      c.gridLayout.horizontalGridHeightSidebarHidden);
    settings.setValue(keys::kGridHeightSidebarHidden, c.gridLayout.gridHeightSidebarHidden);
    settings.setValue(keys::kSidebarVisible, c.sidebar.sidebarVisible);
    settings.setValue(keys::kShowAllSubcollectionItems, c.showAllSubcollectionItems);
    settings.setValue(keys::kHorizontalAlignment,
                      CollectionUtils::alignmentToString(c.horizontalAlignment));
    settings.setValue(keys::kSidebarMode,
                      (c.sidebar.sidebarMode == DetailsPaneMode::Expand) ? "fixed" : "overlay");
    // sidebar enhancements.
    settings.setValue(keys::kSidebarPosition,
                      CollectionUtils::detailsPanePositionToString(c.sidebar.sidebarPosition));
    settings.setValue(
        keys::kSidebarBackgroundType,
        CollectionUtils::detailsPaneBackgroundTypeToString(c.sidebar.sidebarBackgroundType));
    settings.setValue(keys::kSidebarBackgroundColor, c.sidebar.sidebarBackgroundColor);
    settings.setValue(keys::kSidebarBackgroundImage,
                      sanitizePersistedPath(c.sidebar.sidebarBackgroundImage,
                                            "sidebarBackgroundImage", sectionName));
    settings.setValue(keys::kSidebarPattern,
                      CollectionUtils::detailsPanePatternToString(c.sidebar.sidebarPattern));
    settings.setValue(keys::kSidebarPatternIntensity, c.sidebar.sidebarPatternIntensity);
    settings.setValue(keys::kSidebarPatternColor, c.sidebar.sidebarPatternColor);
    settings.setValue(keys::kSidebarTextColor, c.sidebar.sidebarTextColor);
    settings.setValue(keys::kSidebarAccentColor, c.sidebar.sidebarAccentColor);
    settings.setValue(keys::kSidebarHeaderBgColor, c.sidebar.sidebarHeaderBgColor);
    settings.setValue(keys::kSidebarSectionBgColor, c.sidebar.sidebarSectionBgColor);
    settings.setValue(keys::kSidebarHeaderBgOpacity, c.sidebar.sidebarHeaderBgOpacity);
    settings.setValue(keys::kSidebarSectionBgOpacity, c.sidebar.sidebarSectionBgOpacity);
    settings.setValue(keys::kSidebarWidth, c.sidebar.sidebarWidth);
    settings.setValue(keys::kSidebarHeight, c.sidebar.sidebarHeight);
    settings.setValue(keys::kSidebarWidthLocked, c.sidebar.sidebarWidthLocked);
    settings.setValue(keys::kSidebarActiveTab,
                      CollectionUtils::detailsPaneTabToString(c.sidebar.sidebarActiveTab));
    settings.setValue(keys::kViewType, CollectionUtils::viewTypeToString(c.viewType));
    settings.setValue(keys::kHideMissingArtwork, c.hideMissingArtwork);
    settings.setValue(keys::kHideHorizontalScrollbar, c.gridLayout.hideHorizontalScrollbar);
    settings.setValue(keys::kHideVerticalScrollbar, c.gridLayout.hideVerticalScrollbar);
    settings.setValue(keys::kHideTitles, c.hideTitles);
    settings.setValue(keys::kHideSubcollectionTitles, c.hideSubcollectionTitles);
    // persist the title-exclusion list via beginWriteArray so
    // patterns removed by the user disappear from the INI cleanly.
    settings.beginWriteArray(keys::kTitleExclusionPatterns, c.filter.titleExclusionPatterns.size());
    for (int i = 0; i < c.filter.titleExclusionPatterns.size(); ++i) {
      settings.setArrayIndex(i);
      settings.setValue(keys::kPattern, c.filter.titleExclusionPatterns[i]);
    }
    settings.endArray();
    settings.setValue(keys::kTitleExclusionEnabled, c.filter.titleExclusionEnabled);
    settings.setValue(keys::kHorizontalSpacing, c.gridLayout.horizontalSpacing);
    settings.setValue(keys::kVerticalSpacing, c.gridLayout.verticalSpacing);
    settings.setValue(keys::kItemWidth, c.gridLayout.itemWidth);
    settings.setValue(keys::kItemHeight, c.gridLayout.itemHeight);
    settings.setValue(keys::kFontSize, c.gridLayout.fontSize);
    settings.setValue(keys::kCornerRadius, c.gridLayout.cornerRadius);
    QString bgTypeStr = "color";
    if (c.background.backgroundType == BackgroundType::Image) {
      bgTypeStr = "image";
    } else if (c.background.backgroundType == BackgroundType::Video) {
      bgTypeStr = "video";
    }
    settings.setValue(keys::kBackgroundType, bgTypeStr);
    settings.setValue(keys::kBackgroundColor, c.background.backgroundColor);
    settings.setValue(
        keys::kBackgroundImage,
        sanitizePersistedPath(c.background.backgroundImage, "backgroundImage", sectionName));
    settings.setValue(
        keys::kBackgroundVideo,
        sanitizePersistedPath(c.background.backgroundVideo, "backgroundVideo", sectionName));
    settings.setValue(keys::kPrimaryColor, c.background.primaryColor);
    settings.setValue(keys::kTileColor, c.background.tileColor);
    settings.setValue(keys::kSelectionColor, c.background.selectionColor);
    // header logo
    settings.setValue(
        keys::kHeaderLogoImage,
        sanitizePersistedPath(c.background.headerLogoImage, "headerLogoImage", sectionName));
    settings.setValue(keys::kHeaderLogoPosition,
                      CollectionUtils::headerLogoPositionToString(c.background.headerLogoPosition));
    // vignette
    settings.setValue(keys::kVignetteEnabled, c.background.vignetteEnabled);
    settings.setValue(keys::kVignetteIntensity, c.background.vignetteIntensity);
    // wallpaper parallax
    settings.setValue(keys::kWallpaperParallax, c.background.wallpaperParallax);
    settings.setValue(keys::kParallaxStrength, c.background.parallaxStrength);
    // toolbar backdrop blur
    settings.setValue(keys::kToolbarBackdropBlur, c.background.toolbarBackdropBlur);
    settings.setValue(keys::kBackdropBlurRadius, c.background.backdropBlurRadius);

    // List mode settings
    settings.setValue(keys::kListFontSize, c.listView.listFontSize);
    settings.setValue(keys::kListRowHeight, c.listView.listRowHeight);
    settings.setValue(keys::kListRowColor, c.listView.listRowColor);
    settings.setValue(keys::kListAltRowColor, c.listView.listAltRowColor);

    // Text appearance settings (per-collection)
    settings.setValue(keys::kCustomFontFamily, c.customFontFamily);
    // sidebar font override
    settings.setValue(keys::kSidebarFontFamily, c.sidebar.sidebarFontFamily);
    settings.setValue(keys::kSidebarFontPointSize, c.sidebar.sidebarFontPointSize);
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
  return ErrorUtils::Result<void>::success();
}
