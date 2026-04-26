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
                                          const bool &needsRewrite) const {
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
void SettingsManager::loadCollections(QList<CollectionConfig> &collections) const {
  collections.clear();

  QSettings settings(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  QHash<QString, CollectionConfig> tempCollections;
  bool needsRewrite = false;

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
    config.launcherPath = settings.value("launcherPath").toString();
    config.corePath = settings.value("corePath").toString();
    config.launchParameters = settings.value("launchParameters").toString();
    config.mediaDirectory = settings.value("mediaDirectory").toString();
    config.artworkDirectory = settings.value("artworkDirectory").toString();
    config.placeholderArtwork = settings.value("placeholderArtwork").toString();
    config.includeContentSubfolders = settings.value("includeContentSubfolders", false).toBool();
    config.includeArtworkSubfolders = settings.value("includeArtworkSubfolders", false).toBool();
    config.showAllSubfolderItems = settings.value("showAllSubfolderItems", false).toBool();
    config.hideSubfolderTitles = settings.value("hideSubfolderTitles", false).toBool();
    config.showHiddenFolders = settings.value("showHiddenFolders", false).toBool();
    config.extractArchives = settings.value("extractArchives", false).toBool();
    config.extractedExtension = settings.value("extractedExtension").toString();
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

    config.gridWidth = settings.value("gridWidth", 4).toInt();
    config.sidebarVisible = settings.value("sidebarVisible", false).toBool();
    config.showAllSubcollectionItems = settings.value("showAllSubcollectionItems", false).toBool();
    config.horizontalAlignment = CollectionUtils::stringToAlignment(
        settings.value("horizontalAlignment", "center").toString());
    config.sidebarMode = (settings.value("sidebarMode", "overlay").toString() == "fixed")
                             ? SidebarMode::Expand
                             : SidebarMode::Overlay;
    config.viewType =
        CollectionUtils::stringToViewType(settings.value("viewType", "grid").toString());
    config.hideHorizontalScrollbar = settings.value("hideHorizontalScrollbar", false).toBool();
    config.hideVerticalScrollbar = settings.value("hideVerticalScrollbar", false).toBool();
    config.hideTitles = settings.value("hideTitles", false).toBool();
    config.hideSubcollectionTitles = settings.value("hideSubcollectionTitles", false).toBool();
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
    config.backgroundType = (bgType == "image") ? BackgroundType::Image : BackgroundType::Color;
    config.backgroundColor = settings.value("backgroundColor").toString();
    config.backgroundImage = settings.value("backgroundImage").toString();
    config.primaryColor = settings.value("primaryColor").toString();
    config.tileColor = settings.value("tileColor").toString();
    config.selectionColor = settings.value("selectionColor").toString();

    // List mode settings
    config.listFontSize =
        settings.value("listFontSize", UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
    config.listRowHeight =
        settings.value("listRowHeight", UIConstants::ListView::DEFAULT_ROW_HEIGHT).toInt();
    config.listRowColor = settings.value("listRowColor").toString();
    config.listAltRowColor = settings.value("listAltRowColor").toString();

    // Text appearance settings (per-collection)
    config.customFontFamily = settings.value("customFontFamily").toString();

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
}

// Persist collection configurations to disk (no lastSelected_* entries)
void SettingsManager::saveCollections(const QList<CollectionConfig> &collections) const {
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
    settings.setValue("launcherPath",
                      sanitizePersistedPath(c.launcherPath, "launcherPath", sectionName));
    settings.setValue("corePath", sanitizePersistedPath(c.corePath, "corePath", sectionName));
    settings.setValue("launchParameters", c.launchParameters);
    settings.setValue("mediaDirectory",
                      sanitizePersistedPath(c.mediaDirectory, "mediaDirectory", sectionName));
    settings.setValue("artworkDirectory",
                      sanitizePersistedPath(c.artworkDirectory, "artworkDirectory", sectionName));
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
    settings.setValue("collectionIcon", c.collectionIcon);
    settings.setValue("extensions", c.extensions.join(", "));
    settings.setValue("gridWidth", c.gridWidth);
    settings.setValue("sidebarVisible", c.sidebarVisible);
    settings.setValue("showAllSubcollectionItems", c.showAllSubcollectionItems);
    settings.setValue("horizontalAlignment",
                      CollectionUtils::alignmentToString(c.horizontalAlignment));
    settings.setValue("sidebarMode", (c.sidebarMode == SidebarMode::Expand) ? "fixed" : "overlay");
    settings.setValue("viewType", CollectionUtils::viewTypeToString(c.viewType));
    settings.setValue("hideHorizontalScrollbar", c.hideHorizontalScrollbar);
    settings.setValue("hideVerticalScrollbar", c.hideVerticalScrollbar);
    settings.setValue("hideTitles", c.hideTitles);
    settings.setValue("hideSubcollectionTitles", c.hideSubcollectionTitles);
    settings.setValue("horizontalSpacing", c.horizontalSpacing);
    settings.setValue("verticalSpacing", c.verticalSpacing);
    settings.setValue("itemWidth", c.itemWidth);
    settings.setValue("itemHeight", c.itemHeight);
    settings.setValue("fontSize", c.fontSize);
    settings.setValue("cornerRadius", c.cornerRadius);
    settings.setValue("backgroundType",
                      (c.backgroundType == BackgroundType::Image) ? "image" : "color");
    settings.setValue("backgroundColor", c.backgroundColor);
    settings.setValue("backgroundImage",
                      sanitizePersistedPath(c.backgroundImage, "backgroundImage", sectionName));
    settings.setValue("primaryColor", c.primaryColor);
    settings.setValue("tileColor", c.tileColor);
    settings.setValue("selectionColor", c.selectionColor);

    // List mode settings
    settings.setValue("listFontSize", c.listFontSize);
    settings.setValue("listRowHeight", c.listRowHeight);
    settings.setValue("listRowColor", c.listRowColor);
    settings.setValue("listAltRowColor", c.listAltRowColor);

    // Text appearance settings (per-collection)
    settings.setValue("customFontFamily", c.customFontFamily);
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
}
