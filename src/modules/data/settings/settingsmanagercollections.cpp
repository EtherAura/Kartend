// Collection load/save + finalize helpers extracted from settingsmanager.cpp:
//   - finalizeCollections (~28 LOC)
//   - loadCollections (~124 LOC)
//   - saveCollections (~147 LOC)
// The anonymous namespace helpers (findParentCollectionIndex,
// processSubcollection) used by finalizeCollections move with them.
#include "settingsmanager.h"

#include <QDir>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "collectionutils.h"
#include "configvalidation.h"
#include "errorutils.h"
#include "extensionutils.h"
#include "pathutils.h"
#include "settingsutils.h"
#include "titlefilter.h"
#include "uiconstants.h"

namespace {

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
          ErrorUtils::ErrorContext::warning(
              ErrorUtils::ErrorCode::InvalidFilePath,
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
    if (group == "General") continue;

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
    config.launcherPath =
        sanitizeLoadedPath(settings.value("launcherPath").toString(), "launcherPath", config.name);
    config.corePath =
        sanitizeLoadedPath(settings.value("corePath").toString(), "corePath", config.name);
    config.launchParameters = settings.value("launchParameters").toString();
    config.launcherName = settings.value("launcherName").toString();
    // additional launchers are stored as a QSettings array under
    // "additionalLaunchers". When the key is absent (legacy configs), the
    // collection just has the primary launcher and the array stays empty.
    const int additionalCount = settings.beginReadArray("additionalLaunchers");
    config.additionalLaunchers.reserve(additionalCount);
    for (int i = 0; i < additionalCount; ++i) {
      settings.setArrayIndex(i);
      LauncherConfig launcher;
      launcher.name = settings.value("name").toString();
      const QString launcherFieldId =
          QString("additionalLaunchers[%1].launcherPath").arg(i);
      const QString coreFieldId = QString("additionalLaunchers[%1].corePath").arg(i);
      launcher.launcherPath = sanitizeLoadedPath(settings.value("launcherPath").toString(),
                                                  launcherFieldId, config.name);
      launcher.corePath =
          sanitizeLoadedPath(settings.value("corePath").toString(), coreFieldId, config.name);
      launcher.launchParameters = settings.value("launchParameters").toString();
      // optional reference to a global preset.
      launcher.presetId = settings.value("presetId").toString();
      config.additionalLaunchers.append(launcher);
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
    config.defaultLauncherIndex = settings.value("defaultLauncherIndex", 0).toInt();
    config.mediaDirectory = sanitizeLoadedPath(settings.value("mediaDirectory").toString(),
                                                "mediaDirectory", config.name);
    config.artworkDirectory = sanitizeLoadedPath(settings.value("artworkDirectory").toString(),
                                                  "artworkDirectory", config.name);
    config.videoDirectory = sanitizeLoadedPath(settings.value("videoDirectory").toString(),
                                                "videoDirectory", config.name);
    config.manualDirectory = sanitizeLoadedPath(settings.value("manualDirectory").toString(),
                                                 "manualDirectory", config.name);
    config.placeholderArtwork = sanitizeLoadedPath(settings.value("placeholderArtwork").toString(),
                                                    "placeholderArtwork", config.name);
    config.includeContentSubfolders = settings.value("includeContentSubfolders", false).toBool();
    config.includeArtworkSubfolders = settings.value("includeArtworkSubfolders", false).toBool();
    config.showAllSubfolderItems = settings.value("showAllSubfolderItems", false).toBool();
    config.hideSubfolderTitles = settings.value("hideSubfolderTitles", false).toBool();
    config.showHiddenFolders = settings.value("showHiddenFolders", false).toBool();
    config.extractArchives = settings.value("extractArchives", false).toBool();
    config.extractedExtension = settings.value("extractedExtension").toString();
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

    config.gridWidth = settings.value("gridWidth", 4).toInt();
    config.horizontalGridHeight = settings.value("horizontalGridHeight", 0).toInt();
    config.gridWidthSidebarHidden = settings.value("gridWidthSidebarHidden", 0).toInt();
    config.horizontalGridHeightSidebarHidden =
        settings.value("horizontalGridHeightSidebarHidden", 0).toInt();
    // alt items-per-column when a Top/Bottom-docked details pane
    // hides in Expand mode. 0 means "inherit gridWidth" — preserves existing
    // behavior for collections that haven't opted in.
    config.gridHeightSidebarHidden = settings.value("gridHeightSidebarHidden", 0).toInt();
    config.sidebarVisible = settings.value("sidebarVisible", false).toBool();
    config.showAllSubcollectionItems = settings.value("showAllSubcollectionItems", false).toBool();
    config.horizontalAlignment = CollectionUtils::stringToAlignment(
        settings.value("horizontalAlignment", "center").toString());
    config.sidebarMode = (settings.value("sidebarMode", "overlay").toString() == "fixed")
                             ? DetailsPaneMode::Expand
                             : DetailsPaneMode::Overlay;
    // sidebar enhancements.
    config.sidebarPosition = CollectionUtils::stringToDetailsPanePosition(
        settings.value("sidebarPosition", "right").toString());
    config.sidebarBackgroundType = CollectionUtils::stringToDetailsPaneBackgroundType(
        settings.value("sidebarBackgroundType", "color").toString());
    config.sidebarBackgroundColor = settings.value("sidebarBackgroundColor").toString();
    config.sidebarBackgroundImage = sanitizeLoadedPath(
        settings.value("sidebarBackgroundImage").toString(), "sidebarBackgroundImage", config.name);
    config.sidebarPattern = CollectionUtils::stringToDetailsPanePattern(
        settings.value("sidebarPattern", "crosshatch").toString());
    config.sidebarPatternIntensity = settings.value("sidebarPatternIntensity", 50).toInt();
    config.sidebarPatternColor = settings.value("sidebarPatternColor").toString();
    config.sidebarTextColor = settings.value("sidebarTextColor").toString();
    config.sidebarAccentColor = settings.value("sidebarAccentColor").toString();
    config.sidebarHeaderBgColor = settings.value("sidebarHeaderBgColor").toString();
    config.sidebarSectionBgColor = settings.value("sidebarSectionBgColor").toString();
    config.sidebarHeaderBgOpacity = settings.value("sidebarHeaderBgOpacity", 200).toInt();
    config.sidebarSectionBgOpacity = settings.value("sidebarSectionBgOpacity", 170).toInt();
    config.sidebarWidth =
        settings.value("sidebarWidth", UIConstants::DetailsPane::FIXED_WIDTH).toInt();
    // pane height for Top/Bottom dock. Same persistence treatment
    // as sidebarWidth (no migration of older configs needed — the default is
    // applied when the key is absent).
    config.sidebarHeight =
        settings.value("sidebarHeight", UIConstants::DetailsPane::FIXED_HEIGHT).toInt();
    config.sidebarWidthLocked = settings.value("sidebarWidthLocked", true).toBool();
    config.sidebarActiveTab = CollectionUtils::stringToDetailsPaneTab(
        settings.value("sidebarActiveTab", "item").toString());
    config.viewType =
        CollectionUtils::stringToViewType(settings.value("viewType", "grid").toString());
    config.hideMissingArtwork = settings.value("hideMissingArtwork", false).toBool();
    config.hideHorizontalScrollbar = settings.value("hideHorizontalScrollbar", false).toBool();
    config.hideVerticalScrollbar = settings.value("hideVerticalScrollbar", false).toBool();
    config.hideTitles = settings.value("hideTitles", false).toBool();
    config.hideSubcollectionTitles = settings.value("hideSubcollectionTitles", false).toBool();
    // title-exclusion patterns are stored as a QSettings array so
    // each pattern can contain commas / brackets / backslashes without
    // delimiter escaping concerns. titleExclusionEnabled defaults to true so a
    // user adding patterns sees them apply immediately.
    const int titleExcludeCount = settings.beginReadArray("titleExclusionPatterns");
    config.titleExclusionPatterns.reserve(titleExcludeCount);
    for (int i = 0; i < titleExcludeCount; ++i) {
      settings.setArrayIndex(i);
      const QString pattern = settings.value("pattern").toString();
      if (!pattern.isEmpty()) {
        config.titleExclusionPatterns.append(pattern);
      }
    }
    settings.endArray();
    config.titleExclusionEnabled = settings.value("titleExclusionEnabled", true).toBool();
    config.horizontalSpacing =
        settings.value("horizontalSpacing", UIConstants::Grid::SPACING).toInt();
    config.verticalSpacing = settings.value("verticalSpacing", 20).toInt();
    config.itemWidth = settings.value("itemWidth", UIConstants::Item::DEFAULT_WIDTH).toInt();
    config.itemHeight = settings.value("itemHeight", UIConstants::Item::DEFAULT_HEIGHT).toInt();
    config.fontSize = settings.value("fontSize", UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
    config.cornerRadius =
        settings.value("cornerRadius", UIConstants::Item::DEFAULT_CORNER_RADIUS).toInt();

    // Background settings
    QString bgType = settings.value("backgroundType", "color").toString().toLower();
    if (bgType == "image") {
      config.backgroundType = BackgroundType::Image;
    } else if (bgType == "video") {
      config.backgroundType = BackgroundType::Video;
    } else {
      config.backgroundType = BackgroundType::Color;
    }
    config.backgroundColor = settings.value("backgroundColor").toString();
    config.backgroundImage = sanitizeLoadedPath(settings.value("backgroundImage").toString(),
                                                 "backgroundImage", config.name);
    config.backgroundVideo = sanitizeLoadedPath(settings.value("backgroundVideo").toString(),
                                                 "backgroundVideo", config.name);
    config.primaryColor = settings.value("primaryColor").toString();
    config.tileColor = settings.value("tileColor").toString();
    config.selectionColor = settings.value("selectionColor").toString();

    // header logo
    config.headerLogoImage = sanitizeLoadedPath(settings.value("headerLogoImage").toString(),
                                                 "headerLogoImage", config.name);
    config.headerLogoPosition = CollectionUtils::stringToHeaderLogoPosition(
        settings.value("headerLogoPosition", "topcenter").toString());

    // vignette
    config.vignetteEnabled = settings.value("vignetteEnabled", false).toBool();
    config.vignetteIntensity = settings.value("vignetteIntensity", 60).toInt();

    // wallpaper parallax
    config.wallpaperParallax = settings.value("wallpaperParallax", false).toBool();
    config.parallaxStrength = settings.value("parallaxStrength", 30).toInt();

    // toolbar backdrop blur
    config.toolbarBackdropBlur = settings.value("toolbarBackdropBlur", false).toBool();
    config.backdropBlurRadius = settings.value("backdropBlurRadius", 12).toInt();

    // List mode settings
    config.listFontSize =
        settings.value("listFontSize", UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
    config.listRowHeight =
        settings.value("listRowHeight", UIConstants::ListView::DEFAULT_ROW_HEIGHT).toInt();
    config.listRowColor = settings.value("listRowColor").toString();
    config.listAltRowColor = settings.value("listAltRowColor").toString();

    // Text appearance settings (per-collection)
    config.customFontFamily = settings.value("customFontFamily").toString();

    // sidebar font override.
    config.sidebarFontFamily = settings.value("sidebarFontFamily").toString();
    config.sidebarFontPointSize = settings.value("sidebarFontPointSize", 0).toInt();

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
void SettingsManager::saveCollections(const QList<CollectionConfig> &collections) {
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
  // "General"
  const QStringList existingGroups = settings.childGroups();
  for (const QString &group : existingGroups) {
    if (group != "General" && !newGroupNames.contains(group)) {
      settings.remove(group);
    }
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
                      sanitizePersistedPath(c.launcherPath, "launcherPath", sectionName));
    settings.setValue("corePath", sanitizePersistedPath(c.corePath, "corePath", sectionName));
    settings.setValue("launchParameters", c.launchParameters);
    settings.setValue("launcherName", c.launcherName);
    // persist the additional-launcher list as a QSettings array.
    // beginWriteArray clears any existing entries with the same prefix, so
    // launchers removed via the dialog don't linger in the INI.
    settings.beginWriteArray("additionalLaunchers", c.additionalLaunchers.size());
    for (int i = 0; i < c.additionalLaunchers.size(); ++i) {
      settings.setArrayIndex(i);
      const LauncherConfig &launcher = c.additionalLaunchers[i];
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
    settings.setValue("defaultLauncherIndex", c.defaultLauncherIndex);
    settings.setValue("mediaDirectory",
                      sanitizePersistedPath(c.mediaDirectory, "mediaDirectory", sectionName));
    settings.setValue("artworkDirectory",
                      sanitizePersistedPath(c.artworkDirectory, "artworkDirectory", sectionName));
    settings.setValue("videoDirectory",
                      sanitizePersistedPath(c.videoDirectory, "videoDirectory", sectionName));
    settings.setValue("manualDirectory",
                      sanitizePersistedPath(c.manualDirectory, "manualDirectory", sectionName));
    settings.setValue(
        "placeholderArtwork",
        sanitizePersistedPath(c.placeholderArtwork, "placeholderArtwork", sectionName));
    settings.setValue("includeContentSubfolders", c.includeContentSubfolders);
    settings.setValue("includeArtworkSubfolders", c.includeArtworkSubfolders);
    settings.setValue("showAllSubfolderItems", c.showAllSubfolderItems);
    settings.setValue("hideSubfolderTitles", c.hideSubfolderTitles);
    settings.setValue("showHiddenFolders", c.showHiddenFolders);
    settings.setValue("extractArchives", c.extractArchives);
    settings.setValue("extractedExtension", c.extractedExtension);
    settings.setValue("expandMode", c.expandMode);
    settings.setValue("collectionIcon", c.collectionIcon);
    settings.setValue("extensions", c.extensions.join(", "));
    settings.setValue("customArtworkTypes", c.customArtworkTypes.join(", "));
    settings.setValue("gridWidth", c.gridWidth);
    settings.setValue("horizontalGridHeight", c.horizontalGridHeight);
    settings.setValue("gridWidthSidebarHidden", c.gridWidthSidebarHidden);
    settings.setValue("horizontalGridHeightSidebarHidden", c.horizontalGridHeightSidebarHidden);
    settings.setValue("gridHeightSidebarHidden", c.gridHeightSidebarHidden);
    settings.setValue("sidebarVisible", c.sidebarVisible);
    settings.setValue("showAllSubcollectionItems", c.showAllSubcollectionItems);
    settings.setValue("horizontalAlignment",
                      CollectionUtils::alignmentToString(c.horizontalAlignment));
    settings.setValue("sidebarMode",
                      (c.sidebarMode == DetailsPaneMode::Expand) ? "fixed" : "overlay");
    // sidebar enhancements.
    settings.setValue("sidebarPosition",
                      CollectionUtils::detailsPanePositionToString(c.sidebarPosition));
    settings.setValue("sidebarBackgroundType",
                      CollectionUtils::detailsPaneBackgroundTypeToString(c.sidebarBackgroundType));
    settings.setValue("sidebarBackgroundColor", c.sidebarBackgroundColor);
    settings.setValue(
        "sidebarBackgroundImage",
        sanitizePersistedPath(c.sidebarBackgroundImage, "sidebarBackgroundImage", sectionName));
    settings.setValue("sidebarPattern",
                      CollectionUtils::detailsPanePatternToString(c.sidebarPattern));
    settings.setValue("sidebarPatternIntensity", c.sidebarPatternIntensity);
    settings.setValue("sidebarPatternColor", c.sidebarPatternColor);
    settings.setValue("sidebarTextColor", c.sidebarTextColor);
    settings.setValue("sidebarAccentColor", c.sidebarAccentColor);
    settings.setValue("sidebarHeaderBgColor", c.sidebarHeaderBgColor);
    settings.setValue("sidebarSectionBgColor", c.sidebarSectionBgColor);
    settings.setValue("sidebarHeaderBgOpacity", c.sidebarHeaderBgOpacity);
    settings.setValue("sidebarSectionBgOpacity", c.sidebarSectionBgOpacity);
    settings.setValue("sidebarWidth", c.sidebarWidth);
    settings.setValue("sidebarHeight", c.sidebarHeight);
    settings.setValue("sidebarWidthLocked", c.sidebarWidthLocked);
    settings.setValue("sidebarActiveTab",
                      CollectionUtils::detailsPaneTabToString(c.sidebarActiveTab));
    settings.setValue("viewType", CollectionUtils::viewTypeToString(c.viewType));
    settings.setValue("hideMissingArtwork", c.hideMissingArtwork);
    settings.setValue("hideHorizontalScrollbar", c.hideHorizontalScrollbar);
    settings.setValue("hideVerticalScrollbar", c.hideVerticalScrollbar);
    settings.setValue("hideTitles", c.hideTitles);
    settings.setValue("hideSubcollectionTitles", c.hideSubcollectionTitles);
    // persist the title-exclusion list via beginWriteArray so
    // patterns removed by the user disappear from the INI cleanly.
    settings.beginWriteArray("titleExclusionPatterns", c.titleExclusionPatterns.size());
    for (int i = 0; i < c.titleExclusionPatterns.size(); ++i) {
      settings.setArrayIndex(i);
      settings.setValue("pattern", c.titleExclusionPatterns[i]);
    }
    settings.endArray();
    settings.setValue("titleExclusionEnabled", c.titleExclusionEnabled);
    settings.setValue("horizontalSpacing", c.horizontalSpacing);
    settings.setValue("verticalSpacing", c.verticalSpacing);
    settings.setValue("itemWidth", c.itemWidth);
    settings.setValue("itemHeight", c.itemHeight);
    settings.setValue("fontSize", c.fontSize);
    settings.setValue("cornerRadius", c.cornerRadius);
    QString bgTypeStr = "color";
    if (c.backgroundType == BackgroundType::Image) {
      bgTypeStr = "image";
    } else if (c.backgroundType == BackgroundType::Video) {
      bgTypeStr = "video";
    }
    settings.setValue("backgroundType", bgTypeStr);
    settings.setValue("backgroundColor", c.backgroundColor);
    settings.setValue("backgroundImage",
                      sanitizePersistedPath(c.backgroundImage, "backgroundImage", sectionName));
    settings.setValue("backgroundVideo",
                      sanitizePersistedPath(c.backgroundVideo, "backgroundVideo", sectionName));
    settings.setValue("primaryColor", c.primaryColor);
    settings.setValue("tileColor", c.tileColor);
    settings.setValue("selectionColor", c.selectionColor);
    // header logo
    settings.setValue("headerLogoImage",
                      sanitizePersistedPath(c.headerLogoImage, "headerLogoImage", sectionName));
    settings.setValue("headerLogoPosition",
                      CollectionUtils::headerLogoPositionToString(c.headerLogoPosition));
    // vignette
    settings.setValue("vignetteEnabled", c.vignetteEnabled);
    settings.setValue("vignetteIntensity", c.vignetteIntensity);
    // wallpaper parallax
    settings.setValue("wallpaperParallax", c.wallpaperParallax);
    settings.setValue("parallaxStrength", c.parallaxStrength);
    // toolbar backdrop blur
    settings.setValue("toolbarBackdropBlur", c.toolbarBackdropBlur);
    settings.setValue("backdropBlurRadius", c.backdropBlurRadius);

    // List mode settings
    settings.setValue("listFontSize", c.listFontSize);
    settings.setValue("listRowHeight", c.listRowHeight);
    settings.setValue("listRowColor", c.listRowColor);
    settings.setValue("listAltRowColor", c.listAltRowColor);

    // Text appearance settings (per-collection)
    settings.setValue("customFontFamily", c.customFontFamily);
    // sidebar font override
    settings.setValue("sidebarFontFamily", c.sidebarFontFamily);
    settings.setValue("sidebarFontPointSize", c.sidebarFontPointSize);
    settings.endGroup();
  }
  settings.sync();

  if (settings.status() != QSettings::NoError) {
    ErrorUtils::logError(ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                                           "Failed to persist settings",
                                                           "SettingsManager::saveCollections")
                             .withDetails(QString("Path: %1, Status: %2")
                                              .arg(SettingsUtils::getConfigPath())
                                              .arg(static_cast<int>(settings.status()))));
  }

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
}
