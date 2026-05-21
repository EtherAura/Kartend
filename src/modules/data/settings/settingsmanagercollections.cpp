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
#include "settingsutils.h"
#include "titlefilter.h"
#include "uiconstants/detailspane.h"
#include "uiconstants/grid.h"
#include "uiconstants/item.h"
#include "uiconstants/listview.h"

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
    config.name = settings.value("name").toString();
    // free-form category label. Empty means "untagged" — the
    // sidebar/toolbar filter resolves an empty type by walking up the parent
    // chain via CollectionUtils::effectiveCollectionType.
    config.type = settings.value("type").toString().trimmed();
    config.launcher.launcherPath =
        sanitizeLoadedPath(settings.value("launcherPath").toString(), "launcherPath", config.name);
    config.launcher.corePath =
        sanitizeLoadedPath(settings.value("corePath").toString(), "corePath", config.name);
    config.launcher.launchParameters = settings.value("launchParameters").toString();
    config.launcher.launcherName = settings.value("launcherName").toString();
    // additional launchers are stored as a QSettings array under
    // "additionalLaunchers". When the key is absent (legacy configs), the
    // collection just has the primary launcher and the array stays empty.
    const int additionalCount = settings.beginReadArray("additionalLaunchers");
    config.launcher.additionalLaunchers.reserve(additionalCount);
    for (int i = 0; i < additionalCount; ++i) {
      settings.setArrayIndex(i);
      LauncherConfig launcher;
      launcher.name = settings.value("name").toString();
      const QString launcherFieldId = QString("additionalLaunchers[%1].launcherPath").arg(i);
      const QString coreFieldId = QString("additionalLaunchers[%1].corePath").arg(i);
      launcher.launcherPath = sanitizeLoadedPath(settings.value("launcherPath").toString(),
                                                 launcherFieldId, config.name);
      launcher.corePath =
          sanitizeLoadedPath(settings.value("corePath").toString(), coreFieldId, config.name);
      launcher.launchParameters = settings.value("launchParameters").toString();
      // optional reference to a global preset.
      launcher.presetId = settings.value("presetId").toString();
      config.launcher.additionalLaunchers.append(launcher);
    }
    settings.endArray();
    // alias parents — names of additional collections this
    // collection should appear under. Stored as a QSettings string array so
    // names can contain commas/semicolons without escaping concerns.
    const int additionalParentsCount = settings.beginReadArray("additionalParents");
    config.additionalParentNames.reserve(additionalParentsCount);
    for (int i = 0; i < additionalParentsCount; ++i) {
      settings.setArrayIndex(i);
      const QString parentName = settings.value("name").toString();
      if (!parentName.isEmpty()) {
        config.additionalParentNames.append(parentName);
      }
    }
    settings.endArray();
    config.launcher.defaultLauncherIndex = settings.value("defaultLauncherIndex", 0).toInt();
    config.mediaDirectory = sanitizeLoadedPath(settings.value("mediaDirectory").toString(),
                                               "mediaDirectory", config.name);
    config.artworkDirectory = sanitizeLoadedPath(settings.value("artworkDirectory").toString(),
                                                 "artworkDirectory", config.name);
    // videoDirectory / manualDirectory were collapsed into the single-
    // root artwork layout (`{artwork}/video/`, `{artwork}/manual/`).
    // Don't read them — the struct fields stay default-empty and the
    // consumer code paths fall back to the artwork subdirs. The
    // saved-side `settings.remove(...)` calls below scrub any stale
    // values out of the INI on the next save.
    config.videoDirectory.clear();
    config.manualDirectory.clear();
    config.placeholderArtwork = sanitizeLoadedPath(settings.value("placeholderArtwork").toString(),
                                                   "placeholderArtwork", config.name);
    config.folderBrowsing.includeContentSubfolders =
        settings.value("includeContentSubfolders", false).toBool();
    config.folderBrowsing.includeArtworkSubfolders =
        settings.value("includeArtworkSubfolders", false).toBool();
    config.folderBrowsing.showAllSubfolderItems =
        settings.value("showAllSubfolderItems", false).toBool();
    config.folderBrowsing.hideSubfolderTitles =
        settings.value("hideSubfolderTitles", false).toBool();
    config.folderBrowsing.showHiddenFolders = settings.value("showHiddenFolders", false).toBool();
    config.archive.extractArchives = settings.value("extractArchives", false).toBool();
    config.archive.extractedExtension = settings.value("extractedExtension").toString();
    config.expandMode = settings.value("expandMode", false).toBool();
    config.collectionIcon = settings.value("collectionIcon").toString();

    QString extStr = settings.value("extensions").toString();
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
    QString customArtTypesStr = settings.value("customArtworkTypes").toString();
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
        settings.value("screenscraperSystemId", -1).toInt();
    // Hash inner ROM in archives. Default true — better SS match accuracy.
    config.scraperOverrides.screenscraperHashArchive =
        settings.value("screenscraperHashArchive", true).toBool();
    // Per-collection scraper override; empty = automatic (resolve by type).
    config.scraperOverrides.scraperProviderId =
        settings.value("scraperProviderId").toString().trimmed();
    // datFilePaths is persisted as a QSettings array so individual
    // entries can contain commas / brackets without delimiter
    // escaping. Legacy configs (pre-multi-DAT) shipped a single
    // `datFilePath=` key — when the array is absent and the legacy
    // key has a value, upgrade it to a one-entry list so the user
    // doesn't lose their existing DAT on first launch after upgrade.
    const int datPathCount = settings.beginReadArray("datFilePaths");
    config.scraperOverrides.datFilePaths.clear();
    config.scraperOverrides.datFilePaths.reserve(datPathCount);
    for (int i = 0; i < datPathCount; ++i) {
      settings.setArrayIndex(i);
      const QString path = settings.value("path").toString();
      if (!path.isEmpty()) config.scraperOverrides.datFilePaths.append(path);
    }
    settings.endArray();
    if (config.scraperOverrides.datFilePaths.isEmpty()) {
      const QString legacy = settings.value("datFilePath").toString();
      if (!legacy.isEmpty()) config.scraperOverrides.datFilePaths.append(legacy);
    }

    config.gridLayout.gridWidth = settings.value("gridWidth", 4).toInt();
    config.gridLayout.horizontalGridHeight = settings.value("horizontalGridHeight", 0).toInt();
    config.gridLayout.gridWidthSidebarHidden = settings.value("gridWidthSidebarHidden", 0).toInt();
    config.gridLayout.horizontalGridHeightSidebarHidden =
        settings.value("horizontalGridHeightSidebarHidden", 0).toInt();
    // alt items-per-column when a Top/Bottom-docked details pane
    // hides in Expand mode. 0 means "inherit gridWidth" — preserves existing
    // behavior for collections that haven't opted in.
    config.gridLayout.gridHeightSidebarHidden =
        settings.value("gridHeightSidebarHidden", 0).toInt();
    config.sidebar.sidebarVisible = settings.value("sidebarVisible", false).toBool();
    config.showAllSubcollectionItems = settings.value("showAllSubcollectionItems", false).toBool();
    config.horizontalAlignment = CollectionUtils::stringToAlignment(
        settings.value("horizontalAlignment", "center").toString());
    config.sidebar.sidebarMode = (settings.value("sidebarMode", "overlay").toString() == "fixed")
                                     ? DetailsPaneMode::Expand
                                     : DetailsPaneMode::Overlay;
    // sidebar enhancements.
    config.sidebar.sidebarPosition = CollectionUtils::stringToDetailsPanePosition(
        settings.value("sidebarPosition", "right").toString());
    config.sidebar.sidebarBackgroundType = CollectionUtils::stringToDetailsPaneBackgroundType(
        settings.value("sidebarBackgroundType", "color").toString());
    config.sidebar.sidebarBackgroundColor = settings.value("sidebarBackgroundColor").toString();
    config.sidebar.sidebarBackgroundImage = sanitizeLoadedPath(
        settings.value("sidebarBackgroundImage").toString(), "sidebarBackgroundImage", config.name);
    config.sidebar.sidebarPattern = CollectionUtils::stringToDetailsPanePattern(
        settings.value("sidebarPattern", "crosshatch").toString());
    config.sidebar.sidebarPatternIntensity = settings.value("sidebarPatternIntensity", 50).toInt();
    config.sidebar.sidebarPatternColor = settings.value("sidebarPatternColor").toString();
    config.sidebar.sidebarTextColor = settings.value("sidebarTextColor").toString();
    config.sidebar.sidebarAccentColor = settings.value("sidebarAccentColor").toString();
    config.sidebar.sidebarHeaderBgColor = settings.value("sidebarHeaderBgColor").toString();
    config.sidebar.sidebarSectionBgColor = settings.value("sidebarSectionBgColor").toString();
    config.sidebar.sidebarHeaderBgOpacity = settings.value("sidebarHeaderBgOpacity", 200).toInt();
    config.sidebar.sidebarSectionBgOpacity = settings.value("sidebarSectionBgOpacity", 170).toInt();
    config.sidebar.sidebarWidth =
        settings.value("sidebarWidth", UIConstants::DetailsPane::FIXED_WIDTH).toInt();
    // pane height for Top/Bottom dock. Same persistence treatment
    // as sidebarWidth (no migration of older configs needed — the default is
    // applied when the key is absent).
    config.sidebar.sidebarHeight =
        settings.value("sidebarHeight", UIConstants::DetailsPane::FIXED_HEIGHT).toInt();
    config.sidebar.sidebarWidthLocked = settings.value("sidebarWidthLocked", true).toBool();
    config.sidebar.sidebarActiveTab = CollectionUtils::stringToDetailsPaneTab(
        settings.value("sidebarActiveTab", "item").toString());
    config.viewType =
        CollectionUtils::stringToViewType(settings.value("viewType", "grid").toString());
    config.hideMissingArtwork = settings.value("hideMissingArtwork", false).toBool();
    config.gridLayout.hideHorizontalScrollbar =
        settings.value("hideHorizontalScrollbar", false).toBool();
    config.gridLayout.hideVerticalScrollbar =
        settings.value("hideVerticalScrollbar", false).toBool();
    config.hideTitles = settings.value("hideTitles", false).toBool();
    config.hideSubcollectionTitles = settings.value("hideSubcollectionTitles", false).toBool();
    // title-exclusion patterns are stored as a QSettings array so
    // each pattern can contain commas / brackets / backslashes without
    // delimiter escaping concerns. titleExclusionEnabled defaults to true so a
    // user adding patterns sees them apply immediately.
    const int titleExcludeCount = settings.beginReadArray("titleExclusionPatterns");
    config.filter.titleExclusionPatterns.reserve(titleExcludeCount);
    for (int i = 0; i < titleExcludeCount; ++i) {
      settings.setArrayIndex(i);
      const QString pattern = settings.value("pattern").toString();
      if (!pattern.isEmpty()) {
        config.filter.titleExclusionPatterns.append(pattern);
      }
    }
    settings.endArray();
    config.filter.titleExclusionEnabled = settings.value("titleExclusionEnabled", true).toBool();
    config.gridLayout.horizontalSpacing =
        settings.value("horizontalSpacing", UIConstants::Grid::SPACING).toInt();
    config.gridLayout.verticalSpacing = settings.value("verticalSpacing", 20).toInt();
    config.gridLayout.itemWidth =
        settings.value("itemWidth", UIConstants::Item::DEFAULT_WIDTH).toInt();
    config.gridLayout.itemHeight =
        settings.value("itemHeight", UIConstants::Item::DEFAULT_HEIGHT).toInt();
    config.gridLayout.fontSize =
        settings.value("fontSize", UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
    config.gridLayout.cornerRadius =
        settings.value("cornerRadius", UIConstants::Item::DEFAULT_CORNER_RADIUS).toInt();

    // Background settings
    QString bgType = settings.value("backgroundType", "color").toString().toLower();
    if (bgType == "image") {
      config.background.backgroundType = BackgroundType::Image;
    } else if (bgType == "video") {
      config.background.backgroundType = BackgroundType::Video;
    } else {
      config.background.backgroundType = BackgroundType::Color;
    }
    config.background.backgroundColor = settings.value("backgroundColor").toString();
    config.background.backgroundImage = sanitizeLoadedPath(
        settings.value("backgroundImage").toString(), "backgroundImage", config.name);
    config.background.backgroundVideo = sanitizeLoadedPath(
        settings.value("backgroundVideo").toString(), "backgroundVideo", config.name);
    config.background.primaryColor = settings.value("primaryColor").toString();
    config.background.tileColor = settings.value("tileColor").toString();
    config.background.selectionColor = settings.value("selectionColor").toString();

    // header logo
    config.background.headerLogoImage = sanitizeLoadedPath(
        settings.value("headerLogoImage").toString(), "headerLogoImage", config.name);
    config.background.headerLogoPosition = CollectionUtils::stringToHeaderLogoPosition(
        settings.value("headerLogoPosition", "topcenter").toString());

    // vignette
    config.background.vignetteEnabled = settings.value("vignetteEnabled", false).toBool();
    config.background.vignetteIntensity = settings.value("vignetteIntensity", 60).toInt();

    // wallpaper parallax
    config.background.wallpaperParallax = settings.value("wallpaperParallax", false).toBool();
    config.background.parallaxStrength = settings.value("parallaxStrength", 30).toInt();

    // toolbar backdrop blur
    config.background.toolbarBackdropBlur = settings.value("toolbarBackdropBlur", false).toBool();
    config.background.backdropBlurRadius = settings.value("backdropBlurRadius", 12).toInt();

    // List mode settings
    config.listView.listFontSize =
        settings.value("listFontSize", UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
    config.listView.listRowHeight =
        settings.value("listRowHeight", UIConstants::ListView::DEFAULT_ROW_HEIGHT).toInt();
    config.listView.listRowColor = settings.value("listRowColor").toString();
    config.listView.listAltRowColor = settings.value("listAltRowColor").toString();

    // Text appearance settings (per-collection)
    config.customFontFamily = settings.value("customFontFamily").toString();

    // sidebar font override.
    config.sidebar.sidebarFontFamily = settings.value("sidebarFontFamily").toString();
    config.sidebar.sidebarFontPointSize = settings.value("sidebarFontPointSize", 0).toInt();

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

  settings.beginGroup("General");
  settings.setValue("rememberSelection", m_generalSettings.rememberSelection);
  settings.setValue("wrapNavigation", m_generalSettings.wrapNavigation);
  settings.setValue("selectItemOnHover", m_generalSettings.selectItemOnHover);
  settings.endGroup();

  for (const QString &sectionName : sectionNames) {
    int index = sectionToIndex[sectionName];
    const CollectionConfig &c = collections[index];

    // Convert "Parent/Child" to "Parent > Child" to prevent QSettings nesting
    QString iniGroupName = sectionName;
    iniGroupName.replace("/", " > ");

    settings.beginGroup(iniGroupName);
    settings.setValue("name", c.name);
    // persist the free-form category label. Stored verbatim
    // (whitespace already trimmed at load) so a hand-edit round-trips.
    settings.setValue("type", c.type);
    settings.setValue("launcherPath",
                      sanitizePersistedPath(c.launcher.launcherPath, "launcherPath", sectionName));
    settings.setValue("corePath",
                      sanitizePersistedPath(c.launcher.corePath, "corePath", sectionName));
    settings.setValue("launchParameters", c.launcher.launchParameters);
    settings.setValue("launcherName", c.launcher.launcherName);
    // persist the additional-launcher list as a QSettings array.
    // beginWriteArray clears any existing entries with the same prefix, so
    // launchers removed via the dialog don't linger in the INI.
    settings.beginWriteArray("additionalLaunchers", c.launcher.additionalLaunchers.size());
    for (int i = 0; i < c.launcher.additionalLaunchers.size(); ++i) {
      settings.setArrayIndex(i);
      const LauncherConfig &launcher = c.launcher.additionalLaunchers[i];
      settings.setValue("name", launcher.name);
      const QString launcherFieldId = QString("additionalLaunchers[%1].launcherPath").arg(i);
      const QString coreFieldId = QString("additionalLaunchers[%1].corePath").arg(i);
      settings.setValue("launcherPath",
                        sanitizePersistedPath(launcher.launcherPath, launcherFieldId, sectionName));
      settings.setValue("corePath",
                        sanitizePersistedPath(launcher.corePath, coreFieldId, sectionName));
      settings.setValue("launchParameters", launcher.launchParameters);
      // persist the preset reference (empty when inline).
      settings.setValue("presetId", launcher.presetId);
    }
    settings.endArray();
    // persist the alias-parent name list as a QSettings array.
    // beginWriteArray clears prior entries so removals propagate.
    settings.beginWriteArray("additionalParents", c.additionalParentNames.size());
    for (int i = 0; i < c.additionalParentNames.size(); ++i) {
      settings.setArrayIndex(i);
      settings.setValue("name", c.additionalParentNames[i]);
    }
    settings.endArray();
    settings.setValue("defaultLauncherIndex", c.launcher.defaultLauncherIndex);
    settings.setValue("mediaDirectory",
                      sanitizePersistedPath(c.mediaDirectory, "mediaDirectory", sectionName));
    settings.setValue("artworkDirectory",
                      sanitizePersistedPath(c.artworkDirectory, "artworkDirectory", sectionName));
    // Legacy keys — drop them from the INI on every save so stale
    // values (e.g. an unplugged drive, a path typo) don't linger as
    // configvalidation warnings forever. The single-root artwork
    // layout has `{artwork}/video/` and `{artwork}/manual/` as the
    // canonical home for these media types.
    settings.remove(QStringLiteral("videoDirectory"));
    settings.remove(QStringLiteral("manualDirectory"));
    settings.setValue(
        "placeholderArtwork",
        sanitizePersistedPath(c.placeholderArtwork, "placeholderArtwork", sectionName));
    settings.setValue("includeContentSubfolders", c.folderBrowsing.includeContentSubfolders);
    settings.setValue("includeArtworkSubfolders", c.folderBrowsing.includeArtworkSubfolders);
    settings.setValue("showAllSubfolderItems", c.folderBrowsing.showAllSubfolderItems);
    settings.setValue("hideSubfolderTitles", c.folderBrowsing.hideSubfolderTitles);
    settings.setValue("showHiddenFolders", c.folderBrowsing.showHiddenFolders);
    settings.setValue("extractArchives", c.archive.extractArchives);
    settings.setValue("extractedExtension", c.archive.extractedExtension);
    settings.setValue("expandMode", c.expandMode);
    settings.setValue("collectionIcon", c.collectionIcon);
    settings.setValue("extensions", c.extensions.join(", "));
    settings.setValue("customArtworkTypes", c.customArtworkTypes.join(", "));
    // ScreenScraper systemeid override (round-trips even when -1 so the
    // distinction between "I haven't decided yet" and "autodetect" is
    // explicit on disk for users hand-editing the file).
    settings.setValue("screenscraperSystemId", c.scraperOverrides.screenscraperSystemId);
    settings.setValue("screenscraperHashArchive", c.scraperOverrides.screenscraperHashArchive);
    // Per-collection scraper override; empty = automatic (resolve by type).
    settings.setValue("scraperProviderId", c.scraperOverrides.scraperProviderId);
    // Persist datFilePaths via beginWriteArray so removed entries
    // disappear cleanly from the INI. Also drop any stale single-key
    // `datFilePath=` left behind by a pre-upgrade config — keeping
    // both around would confuse hand-editors and round-tripping.
    settings.remove(QStringLiteral("datFilePath"));
    settings.beginWriteArray("datFilePaths", c.scraperOverrides.datFilePaths.size());
    for (int i = 0; i < c.scraperOverrides.datFilePaths.size(); ++i) {
      settings.setArrayIndex(i);
      settings.setValue("path", c.scraperOverrides.datFilePaths[i]);
    }
    settings.endArray();
    settings.setValue("gridWidth", c.gridLayout.gridWidth);
    settings.setValue("horizontalGridHeight", c.gridLayout.horizontalGridHeight);
    settings.setValue("gridWidthSidebarHidden", c.gridLayout.gridWidthSidebarHidden);
    settings.setValue("horizontalGridHeightSidebarHidden",
                      c.gridLayout.horizontalGridHeightSidebarHidden);
    settings.setValue("gridHeightSidebarHidden", c.gridLayout.gridHeightSidebarHidden);
    settings.setValue("sidebarVisible", c.sidebar.sidebarVisible);
    settings.setValue("showAllSubcollectionItems", c.showAllSubcollectionItems);
    settings.setValue("horizontalAlignment",
                      CollectionUtils::alignmentToString(c.horizontalAlignment));
    settings.setValue("sidebarMode",
                      (c.sidebar.sidebarMode == DetailsPaneMode::Expand) ? "fixed" : "overlay");
    // sidebar enhancements.
    settings.setValue("sidebarPosition",
                      CollectionUtils::detailsPanePositionToString(c.sidebar.sidebarPosition));
    settings.setValue("sidebarBackgroundType", CollectionUtils::detailsPaneBackgroundTypeToString(
                                                   c.sidebar.sidebarBackgroundType));
    settings.setValue("sidebarBackgroundColor", c.sidebar.sidebarBackgroundColor);
    settings.setValue("sidebarBackgroundImage",
                      sanitizePersistedPath(c.sidebar.sidebarBackgroundImage,
                                            "sidebarBackgroundImage", sectionName));
    settings.setValue("sidebarPattern",
                      CollectionUtils::detailsPanePatternToString(c.sidebar.sidebarPattern));
    settings.setValue("sidebarPatternIntensity", c.sidebar.sidebarPatternIntensity);
    settings.setValue("sidebarPatternColor", c.sidebar.sidebarPatternColor);
    settings.setValue("sidebarTextColor", c.sidebar.sidebarTextColor);
    settings.setValue("sidebarAccentColor", c.sidebar.sidebarAccentColor);
    settings.setValue("sidebarHeaderBgColor", c.sidebar.sidebarHeaderBgColor);
    settings.setValue("sidebarSectionBgColor", c.sidebar.sidebarSectionBgColor);
    settings.setValue("sidebarHeaderBgOpacity", c.sidebar.sidebarHeaderBgOpacity);
    settings.setValue("sidebarSectionBgOpacity", c.sidebar.sidebarSectionBgOpacity);
    settings.setValue("sidebarWidth", c.sidebar.sidebarWidth);
    settings.setValue("sidebarHeight", c.sidebar.sidebarHeight);
    settings.setValue("sidebarWidthLocked", c.sidebar.sidebarWidthLocked);
    settings.setValue("sidebarActiveTab",
                      CollectionUtils::detailsPaneTabToString(c.sidebar.sidebarActiveTab));
    settings.setValue("viewType", CollectionUtils::viewTypeToString(c.viewType));
    settings.setValue("hideMissingArtwork", c.hideMissingArtwork);
    settings.setValue("hideHorizontalScrollbar", c.gridLayout.hideHorizontalScrollbar);
    settings.setValue("hideVerticalScrollbar", c.gridLayout.hideVerticalScrollbar);
    settings.setValue("hideTitles", c.hideTitles);
    settings.setValue("hideSubcollectionTitles", c.hideSubcollectionTitles);
    // persist the title-exclusion list via beginWriteArray so
    // patterns removed by the user disappear from the INI cleanly.
    settings.beginWriteArray("titleExclusionPatterns", c.filter.titleExclusionPatterns.size());
    for (int i = 0; i < c.filter.titleExclusionPatterns.size(); ++i) {
      settings.setArrayIndex(i);
      settings.setValue("pattern", c.filter.titleExclusionPatterns[i]);
    }
    settings.endArray();
    settings.setValue("titleExclusionEnabled", c.filter.titleExclusionEnabled);
    settings.setValue("horizontalSpacing", c.gridLayout.horizontalSpacing);
    settings.setValue("verticalSpacing", c.gridLayout.verticalSpacing);
    settings.setValue("itemWidth", c.gridLayout.itemWidth);
    settings.setValue("itemHeight", c.gridLayout.itemHeight);
    settings.setValue("fontSize", c.gridLayout.fontSize);
    settings.setValue("cornerRadius", c.gridLayout.cornerRadius);
    QString bgTypeStr = "color";
    if (c.background.backgroundType == BackgroundType::Image) {
      bgTypeStr = "image";
    } else if (c.background.backgroundType == BackgroundType::Video) {
      bgTypeStr = "video";
    }
    settings.setValue("backgroundType", bgTypeStr);
    settings.setValue("backgroundColor", c.background.backgroundColor);
    settings.setValue("backgroundImage", sanitizePersistedPath(c.background.backgroundImage,
                                                               "backgroundImage", sectionName));
    settings.setValue("backgroundVideo", sanitizePersistedPath(c.background.backgroundVideo,
                                                               "backgroundVideo", sectionName));
    settings.setValue("primaryColor", c.background.primaryColor);
    settings.setValue("tileColor", c.background.tileColor);
    settings.setValue("selectionColor", c.background.selectionColor);
    // header logo
    settings.setValue("headerLogoImage", sanitizePersistedPath(c.background.headerLogoImage,
                                                               "headerLogoImage", sectionName));
    settings.setValue("headerLogoPosition",
                      CollectionUtils::headerLogoPositionToString(c.background.headerLogoPosition));
    // vignette
    settings.setValue("vignetteEnabled", c.background.vignetteEnabled);
    settings.setValue("vignetteIntensity", c.background.vignetteIntensity);
    // wallpaper parallax
    settings.setValue("wallpaperParallax", c.background.wallpaperParallax);
    settings.setValue("parallaxStrength", c.background.parallaxStrength);
    // toolbar backdrop blur
    settings.setValue("toolbarBackdropBlur", c.background.toolbarBackdropBlur);
    settings.setValue("backdropBlurRadius", c.background.backdropBlurRadius);

    // List mode settings
    settings.setValue("listFontSize", c.listView.listFontSize);
    settings.setValue("listRowHeight", c.listView.listRowHeight);
    settings.setValue("listRowColor", c.listView.listRowColor);
    settings.setValue("listAltRowColor", c.listView.listAltRowColor);

    // Text appearance settings (per-collection)
    settings.setValue("customFontFamily", c.customFontFamily);
    // sidebar font override
    settings.setValue("sidebarFontFamily", c.sidebar.sidebarFontFamily);
    settings.setValue("sidebarFontPointSize", c.sidebar.sidebarFontPointSize);
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
