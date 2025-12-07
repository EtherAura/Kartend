// Handles config file I/O, collection settings, and the settings dialog interface.
#include "settingsmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "extensionutils.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "uiconstants.h"
#include "settingsutils.h"
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <algorithm>
#include <QStandardPaths>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")
#define debugLog(msg) qCDebug(lcSettingsManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

// Construct settings manager and initialize QSettings.
SettingsManager::SettingsManager(SessionManager *sessionManager,
                                 ArtworkManager *artworkManager,
                                 CacheManager *cacheManager,
                                 QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager),
      m_artworkManager(artworkManager), m_cacheManager(cacheManager) {
  QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  QDir configDir(configPath);
  if (!configDir.exists()) {
    configDir.mkpath(".");
  }
}

SettingsManager::~SettingsManager() = default;



#include <QSettings>

namespace {

auto findParentCollectionIndex(
    const QStringList &parts, const QString &immediateParentName,
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

auto processSubcollection(const QString &sectionName,
                                           CollectionConfig &collection,
                                           QList<CollectionConfig> &collections)
    -> void {
  QStringList parts = sectionName.split('/', Qt::KeepEmptyParts);
  if (parts.size() < 2) {
    return;
  }

  const QString &immediateParentName = parts[parts.size() - 2];
  int parentIndex =
      findParentCollectionIndex(parts, immediateParentName, collections);

  if (parentIndex >= 0) {
    collection.parentCollectionIndex = parentIndex;
    collection.isSubcollection = true;
    collections.append(collection);
  }
}

auto detectChanges(
    const QList<CollectionConfig> &newCollections,
    const QList<CollectionConfig> &originalCollections,
    int viewingCollectionIndex, bool &needsReload, bool &gridWidthChangedForView,
    bool &alignmentChangedForView, bool &spacingChangedForView,
    bool &scrollbarChangedForView, bool &sidebarModeChangedForView,
    bool &titleChangedForView, bool &fontSizeChangedForView,
    bool &hideTitlesChangedForView, bool &appearanceChangedForView) -> bool;

} // namespace

void SettingsManager::finalizeCollections(
    const QHash<QString, CollectionConfig> &tempCollections,
    QList<CollectionConfig> &collections, bool &needsRewrite) const {
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
void SettingsManager::loadCollections(
    QList<CollectionConfig> &collections) const {
  collections.clear();

  QSettings settings(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  QHash<QString, CollectionConfig> tempCollections;
  bool needsRewrite = false;

  QStringList groups = settings.childGroups();
  for (const QString &group : groups) {
    if (group == "General") continue;

    // Convert "Parent > Child" back to "Parent/Child" for internal hierarchy processing
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
    config.includeContentSubfolders = settings.value("includeContentSubfolders", false).toBool();
    config.includeArtworkSubfolders = settings.value("includeArtworkSubfolders", false).toBool();
    config.showAllSubfolderItems = settings.value("showAllSubfolderItems", false).toBool();
    config.hideSubfolderTitles = settings.value("hideSubfolderTitles", false).toBool();
    config.showHiddenFolders = settings.value("showHiddenFolders", false).toBool();
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
    config.horizontalAlignment = CollectionUtils::stringToAlignment(settings.value("horizontalAlignment", "center").toString());
    config.sidebarMode = (settings.value("sidebarMode", "overlay").toString() == "fixed") ? SidebarMode::Expand : SidebarMode::Overlay;
    config.hideHorizontalScrollbar = settings.value("hideHorizontalScrollbar", false).toBool();
    config.hideVerticalScrollbar = settings.value("hideVerticalScrollbar", false).toBool();
    config.hideTitles = settings.value("hideTitles", false).toBool();
    config.hideSubcollectionTitles = settings.value("hideSubcollectionTitles", false).toBool();
    config.horizontalSpacing = settings.value("horizontalSpacing", UIConstants::Grid::SPACING).toInt();
    config.verticalSpacing = settings.value("verticalSpacing", 20).toInt();
    config.itemWidth = settings.value("itemWidth", UIConstants::Item::DEFAULT_WIDTH).toInt();
    config.itemHeight = settings.value("itemHeight", UIConstants::Item::DEFAULT_HEIGHT).toInt();
    config.fontSize = settings.value("fontSize", UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
    config.cornerRadius = settings.value("cornerRadius", UIConstants::Item::DEFAULT_CORNER_RADIUS).toInt();
    
    // Background settings
    QString bgType = settings.value("backgroundType", "color").toString().toLower();
    config.backgroundType = (bgType == "image") ? BackgroundType::Image : BackgroundType::Color;
    config.backgroundColor = settings.value("backgroundColor").toString();
    config.backgroundImage = settings.value("backgroundImage").toString();
    config.primaryColor = settings.value("primaryColor").toString();
    config.tileColor = settings.value("tileColor").toString();
    config.selectionColor = settings.value("selectionColor").toString();

    // Validate and clamp numeric values to acceptable ranges
    config.clampValues();

    // Use the internal name (with slashes) for hierarchy processing
    tempCollections[internalGroupName] = config;
    settings.endGroup();
  }

  finalizeCollections(tempCollections, collections, needsRewrite);
}

// Persist collection configurations to disk (no lastSelected_* entries)
void SettingsManager::saveCollections(
    const QList<CollectionConfig> &collections) const {
  QSettings settings(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  
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

  // Remove only groups that are NOT in the new collections list and NOT "General"
  const QStringList existingGroups = settings.childGroups();
  for (const QString &group : existingGroups) {
    if (group != "General" && !newGroupNames.contains(group)) {
      settings.remove(group);
    }
  }

  settings.beginGroup("General");
  settings.setValue("rememberSelection", m_generalSettings.rememberSelection);
  settings.setValue("wrapNavigation", m_generalSettings.wrapNavigation);
  settings.endGroup();

  for (const QString &sectionName : sectionNames) {
    int index = sectionToIndex[sectionName];
    const CollectionConfig &c = collections[index];

    // Convert "Parent/Child" to "Parent > Child" to prevent QSettings nesting
    QString iniGroupName = sectionName;
    iniGroupName.replace("/", " > ");

    settings.beginGroup(iniGroupName);
    settings.setValue("name", c.name);
    settings.setValue("launcherPath", c.launcherPath);
    settings.setValue("corePath", c.corePath);
    settings.setValue("launchParameters", c.launchParameters);
    settings.setValue("mediaDirectory", c.mediaDirectory);
    settings.setValue("artworkDirectory", c.artworkDirectory);
    settings.setValue("includeContentSubfolders", c.includeContentSubfolders);
    settings.setValue("includeArtworkSubfolders", c.includeArtworkSubfolders);
    settings.setValue("showAllSubfolderItems", c.showAllSubfolderItems);
    settings.setValue("hideSubfolderTitles", c.hideSubfolderTitles);
    settings.setValue("showHiddenFolders", c.showHiddenFolders);
    settings.setValue("collectionIcon", c.collectionIcon);
    settings.setValue("extensions", c.extensions.join(", "));
    settings.setValue("gridWidth", c.gridWidth);
    settings.setValue("sidebarVisible", c.sidebarVisible);
    settings.setValue("showAllSubcollectionItems", c.showAllSubcollectionItems);
    settings.setValue("horizontalAlignment", CollectionUtils::alignmentToString(c.horizontalAlignment));
    settings.setValue("sidebarMode", (c.sidebarMode == SidebarMode::Expand) ? "fixed" : "overlay");
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
    settings.setValue("backgroundType", (c.backgroundType == BackgroundType::Image) ? "image" : "color");
    settings.setValue("backgroundColor", c.backgroundColor);
    settings.setValue("backgroundImage", c.backgroundImage);
    settings.setValue("primaryColor", c.primaryColor);
    settings.setValue("tileColor", c.tileColor);
    settings.setValue("selectionColor", c.selectionColor);
    settings.endGroup();
  }
  settings.sync();
}






// Launch settings dialog and apply accepted modifications.
void SettingsManager::openSettingsDialog(const SettingsDialogContext &context) {
  if (!context.collections ||
      !context.currentCollectionIndex) {
    return;
  }

  QList<CollectionConfig> &collections = *context.collections;
  int &currentCollectionIndex = *context.currentCollectionIndex;
  QWidget *parent = context.parent;
  SidebarManager *sidebarManager = context.sidebarManager;
  ScrollManager *scrollManager = context.scrollManager;
  NavigationManager *navigationManager = context.navigationManager;

  int viewingCollectionIndex = currentCollectionIndex;
  QList<CollectionConfig> originalCollections = collections;

  SettingsDialog dlg(parent, collections, viewingCollectionIndex);

  connect(
      &dlg, &SettingsDialog::collectionSaved, this,
      [this, &collections](const QList<CollectionConfig> &savedCollections) {
        collections = savedCollections;
        saveCollections(collections);
      });

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  QList<CollectionConfig> newCollections = dlg.getCollections();

  bool hasChanges = false;
  bool needsReload = false;
  bool gridWidthChangedForView = false;
  bool alignmentChangedForView = false;
  bool spacingChangedForView = false;
  bool scrollbarChangedForView = false;
  bool sidebarModeChangedForView = false;
  bool titleChangedForView = false;
  bool fontSizeChangedForView = false;
  bool hideTitlesChangedForView = false;
  bool appearanceChangedForView = false;

  hasChanges = detectChanges(
      newCollections, originalCollections, viewingCollectionIndex, needsReload,
      gridWidthChangedForView, alignmentChangedForView, spacingChangedForView,
      scrollbarChangedForView, sidebarModeChangedForView, titleChangedForView,
      fontSizeChangedForView, hideTitlesChangedForView, appearanceChangedForView);

  if (!hasChanges) {
    return;
  }

  if (m_artworkManager->getTimerCoordinator()) {
    m_artworkManager->getTimerCoordinator()->stopAllTimers();
  }

  collections = newCollections;
  saveCollections(collections);
  emit collectionsModified();
  auto normalizeCollectionIndex = [](const QList<CollectionConfig> &list,
                                     int desiredIndex) -> int {
    if (list.isEmpty()) {
      return -1;
    }
    if (desiredIndex < 0) {
      return 0;
    }
    if (desiredIndex >= list.size()) {
      return list.size() - 1;
    }
    return desiredIndex;
  };

  int resolvedCollectionIndex =
      normalizeCollectionIndex(collections, viewingCollectionIndex);
  currentCollectionIndex = resolvedCollectionIndex;
  viewingCollectionIndex = resolvedCollectionIndex;

  if (sidebarManager) {
    if (resolvedCollectionIndex >= 0) {
      sidebarManager->applySidebarStateForCollection(resolvedCollectionIndex);
    }
  }

  // Always apply appearance settings (colors) after any changes
  if (navigationManager && resolvedCollectionIndex >= 0) {
    navigationManager->applyBackgroundForCollection(resolvedCollectionIndex);
    navigationManager->applyPrimaryColorForCollection(resolvedCollectionIndex);
  }

  if (needsReload) {
    handleReloadRequired(collections, newCollections, originalCollections,
                         viewingCollectionIndex, sidebarManager,
                         scrollManager, navigationManager, m_artworkManager,
                         m_cacheManager, currentCollectionIndex);
  } else {
    handleLayoutChanges(parent, collections, viewingCollectionIndex,
                        titleChangedForView, scrollbarChangedForView,
                        sidebarModeChangedForView, gridWidthChangedForView,
                        spacingChangedForView, alignmentChangedForView,
                        fontSizeChangedForView, hideTitlesChangedForView,
                        sidebarManager, scrollManager, m_artworkManager,
                        currentCollectionIndex);
    
    // If only appearance changed, still refresh widgets to show new colors
    if (appearanceChangedForView && scrollManager) {
      scrollManager->recreateLayout();
    }
  }
}

namespace {
// Compares fields that do not require a reload
auto compareNonReloadFields(const CollectionConfig &configA,
                            const CollectionConfig &configB, bool &hasChanges)
    -> void {
  if (configA.name != configB.name ||
      configA.launcherPath != configB.launcherPath ||
      configA.corePath != configB.corePath ||
      configA.launchParameters != configB.launchParameters ||
      configA.parentCollectionIndex != configB.parentCollectionIndex ||
      configA.isSubcollection != configB.isSubcollection) {
    hasChanges = true;
  }
}

// Compares fields that trigger reload
void compareReloadFields(const CollectionConfig &configA,
                         const CollectionConfig &configB, bool &hasChanges,
                         bool &needsReload) {
  if (configA.mediaDirectory != configB.mediaDirectory ||
      configA.artworkDirectory != configB.artworkDirectory ||
      configA.includeContentSubfolders != configB.includeContentSubfolders ||
      configA.includeArtworkSubfolders != configB.includeArtworkSubfolders ||
      configA.showAllSubcollectionItems != configB.showAllSubcollectionItems ||
      configA.showAllSubfolderItems != configB.showAllSubfolderItems ||
      configA.hideSubfolderTitles != configB.hideSubfolderTitles ||
      configA.showHiddenFolders != configB.showHiddenFolders ||
      configA.extensions != configB.extensions) {
    hasChanges = true;
    needsReload = true;
  }
}

// Updates flags for the viewing collection only
void updateViewingFlags(const CollectionConfig &configA,
                        const CollectionConfig &configB, bool &hasChanges,
                        bool &gridWidthChanged, bool &alignmentChanged,
                        bool &spacingChanged, bool &scrollbarChanged,
                        bool &sidebarModeChanged, bool &titleChanged,
                        bool &fontSizeChanged, bool &hideTitlesChanged,
                        bool &appearanceChanged) {
  if (configA.gridWidth != configB.gridWidth) {
    hasChanges = true;
    gridWidthChanged = true;
  }
  if (configA.horizontalAlignment != configB.horizontalAlignment) {
    hasChanges = true;
    alignmentChanged = true;
  }
  if (configA.horizontalSpacing != configB.horizontalSpacing ||
      configA.verticalSpacing != configB.verticalSpacing) {
    hasChanges = true;
    spacingChanged = true;
  }
  if (configA.itemWidth != configB.itemWidth ||
      configA.itemHeight != configB.itemHeight) {
    hasChanges = true;
    spacingChanged = true;
  }
  if (configA.fontSize != configB.fontSize ||
      configA.cornerRadius != configB.cornerRadius) {
    hasChanges = true;
    fontSizeChanged = true;
  }
  if (configA.hideTitles != configB.hideTitles ||
      configA.hideSubcollectionTitles != configB.hideSubcollectionTitles) {
    hasChanges = true;
    hideTitlesChanged = true;
  }
  if (configA.hideHorizontalScrollbar != configB.hideHorizontalScrollbar ||
      configA.hideVerticalScrollbar != configB.hideVerticalScrollbar) {
    hasChanges = true;
    scrollbarChanged = true;
  }
  if (configA.sidebarMode != configB.sidebarMode) {
    hasChanges = true;
    sidebarModeChanged = true;
  }
  if (configA.name != configB.name) {
    titleChanged = true;
  }
  // Appearance changes (colors)
  if (configA.primaryColor != configB.primaryColor ||
      configA.tileColor != configB.tileColor ||
      configA.selectionColor != configB.selectionColor ||
      configA.backgroundColor != configB.backgroundColor ||
      configA.backgroundImage != configB.backgroundImage ||
      configA.backgroundType != configB.backgroundType) {
    hasChanges = true;
    appearanceChanged = true;
  }
}

// Title update logic separated from handleLayoutChanges
void updateWindowTitle(QWidget *parent, int viewingIndex,
                       const QList<CollectionConfig> &collections) {
  if (auto *mw = qobject_cast<MainWindow *>(parent)) {
    mw->updateWindowTitleForCollection(viewingIndex);
  }
  auto *titleLabel = parent->findChild<QLabel *>("itemsTitleLabel");
  if (titleLabel) {
    QString title = collections[viewingIndex].name;
    if (collections[viewingIndex].isSubcollection &&
        collections[viewingIndex].parentCollectionIndex >= 0 &&
        collections[viewingIndex].parentCollectionIndex < collections.size()) {
      const QString parentName =
          collections[collections[viewingIndex].parentCollectionIndex].name;
      title = parentName + " > " + title;
    }
    titleLabel->setText(title);
  }
}

// Applies scrollbar settings for the viewing collection
void applyScrollbarSettings(QWidget *parent, int viewingIndex,
                            const QList<CollectionConfig> &collections) {
  auto *scrollArea = parent->findChild<QScrollArea *>("itemScrollArea");
  if (scrollArea) {
    SettingsUtils::applyHorizontalScrollbarSetting(scrollArea, viewingIndex,
                                                   collections);
    SettingsUtils::applyVerticalScrollbarSetting(scrollArea, viewingIndex,
                                                 collections);
  }
}

// Updates sidebar layout when mode changes
void refreshSidebar(SidebarManager *sidebarManager,
                    QList<CollectionConfig> &collections,
                    int currentCollectionIndex) {
  if (sidebarManager) {
    sidebarManager->updateSidebarLayout(currentCollectionIndex);
  }
}

// Handles scroll manager branching
void handleScrollBranch(ScrollManager *scrollManager,
                        ArtworkManager *artworkManager,
                        const QList<CollectionConfig> &collections,
                        int viewingIndex, bool spacingChanged,
                        bool sidebarModeChanged, bool gridWidthChanged,
                        bool alignmentChanged, bool fontSizeChanged,
                        bool hideTitlesChanged) {
  if (!scrollManager) {
    return;
  }
  if (spacingChanged || sidebarModeChanged || fontSizeChanged ||
      hideTitlesChanged) {
    scrollManager->primeLayoutFor(collections[viewingIndex]);
    scrollManager->recreateLayout();
    return;
  }
  if (gridWidthChanged) {
    scrollManager->updateGridWidth(collections[viewingIndex].gridWidth);
    // Delay layout recalculation to allow grid width change to propagate -
    // nested timer ensures artwork updates happen after layout is stable
    QTimer::singleShot(UIConstants::Timing::LONG_DELAY_MS, [scrollManager,
                                                       artworkManager,
                                                       viewingIndex,
                                                       collections]() {
      if (scrollManager) {
        scrollManager->preCalculateLayout();
        scrollManager->forceVirtualViewUpdate();
        QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS, [scrollManager,
                                                             artworkManager,
                                                             viewingIndex,
                                                             collections]() {
          if (scrollManager) {
            scrollManager->updateVirtualView();
            if (artworkManager) {
              artworkManager->updateViewportArtwork();
            }
            scrollManager->centerHorizontalScrollbar(viewingIndex, collections);
          }
        });
      }
    });
    return;
  }
  if (alignmentChanged) {
    scrollManager->recenterVirtualContainer();
  } else {
    scrollManager->handleLayoutChange();
  }
}
auto detectChanges(
    const QList<CollectionConfig> &newCollections,
    const QList<CollectionConfig> &originalCollections,
    int viewingCollectionIndex, bool &needsReload, bool &gridWidthChangedForView,
    bool &alignmentChangedForView, bool &spacingChangedForView,
    bool &scrollbarChangedForView, bool &sidebarModeChangedForView,
    bool &titleChangedForView, bool &fontSizeChangedForView,
    bool &hideTitlesChangedForView, bool &appearanceChangedForView) -> bool {
  bool hasChanges = false;
  if (newCollections.size() != originalCollections.size()) {
    hasChanges = true;
    needsReload = true;
  }

  for (int i = 0; i < newCollections.size(); ++i) {
    if (i >= originalCollections.size()) {
      hasChanges = true;
      needsReload = true;
      continue;
    }
    const CollectionConfig &newConfig = newCollections[i];
    const CollectionConfig &oldConfig = originalCollections[i];
    compareNonReloadFields(newConfig, oldConfig, hasChanges);
    compareReloadFields(newConfig, oldConfig, hasChanges, needsReload);
    if (i == viewingCollectionIndex) {
      updateViewingFlags(newConfig, oldConfig, hasChanges,
                         gridWidthChangedForView, alignmentChangedForView,
                         spacingChangedForView, scrollbarChangedForView,
                         sidebarModeChangedForView, titleChangedForView,
                         fontSizeChangedForView, hideTitlesChangedForView,
                         appearanceChangedForView);
    }
  }
  return hasChanges;
}

} // namespace

auto SettingsManager::handleReloadRequired(
    QList<CollectionConfig> &collections,
    const QList<CollectionConfig> &newCollections,
    const QList<CollectionConfig> &originalCollections,
    int viewingCollectionIndex, SidebarManager *sidebarManager,
    ScrollManager *scrollManager, NavigationManager *navigationManager,
    ArtworkManager *artworkManager, CacheManager *cacheManager,
    int currentCollectionIndex) -> void {
  if (artworkManager) {
    artworkManager->cancelAllArtworkLoading();
  }
  if (viewingCollectionIndex >= 0 &&
      viewingCollectionIndex < collections.size()) {
    const QString &mediaDirectory =
        newCollections[viewingCollectionIndex].mediaDirectory;
    const QString &originalMediaDirectory =
        originalCollections[viewingCollectionIndex].mediaDirectory;
    bool mediaDirectoryChanged = (mediaDirectory != originalMediaDirectory);
    bool extensionsChanged =
        (newCollections[viewingCollectionIndex].extensions !=
         originalCollections[viewingCollectionIndex].extensions);

    if (mediaDirectoryChanged || extensionsChanged) {
      if (cacheManager) {
        cacheManager->clearCollectionCache(viewingCollectionIndex);
      }
      if (artworkManager) {
        artworkManager->clearLoadedArtworkState();
        artworkManager->clearWidgetReferences();
      }
    }

    if (scrollManager) {
      scrollManager->cleanup();
    }

    // Delay collection reload to allow cleanup operations to complete -
    // prevents race conditions with ongoing artwork loads or animations
    QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS,
                       [navigationManager, viewingCollectionIndex]() {
                         if (navigationManager) {
                           navigationManager->safeReloadCollection(
                               viewingCollectionIndex);
                         }
                       });
  }
}

auto SettingsManager::handleLayoutChanges(
    QWidget *parent, QList<CollectionConfig> &collections,
    int viewingCollectionIndex, bool titleChangedForView,
    bool scrollbarChangedForView, bool sidebarModeChangedForView,
    bool gridWidthChangedForView, bool spacingChangedForView,
    bool alignmentChangedForView, bool fontSizeChangedForView,
    bool hideTitlesChangedForView, SidebarManager *sidebarManager,
    ScrollManager *scrollManager, ArtworkManager *artworkManager,
    int currentCollectionIndex) -> void {
  if (viewingCollectionIndex < 0 ||
      viewingCollectionIndex >= collections.size()) {
    return;
  }
  if (titleChangedForView) {
    updateWindowTitle(parent, viewingCollectionIndex, collections);
  }
  if (scrollbarChangedForView) {
    applyScrollbarSettings(parent, viewingCollectionIndex, collections);
  }
  if (sidebarModeChangedForView) {
    refreshSidebar(sidebarManager, collections, currentCollectionIndex);
  }
  handleScrollBranch(scrollManager, artworkManager, collections, viewingCollectionIndex,
                     spacingChangedForView, sidebarModeChangedForView,
                     gridWidthChangedForView, alignmentChangedForView,
                     fontSizeChangedForView, hideTitlesChangedForView);
}



// Loads general settings (selection indices now resolved from persistent cache
// separately)
void SettingsManager::loadGeneralSettings(GeneralSettings &settings) {
  QSettings s(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  s.beginGroup("General");
  settings.rememberSelection = s.value("rememberSelection", true).toBool();
  settings.wrapNavigation = s.value("wrapNavigation", false).toBool();
  settings.pixmapCacheSizeMB = s.value("pixmapCacheSizeMB", 50).toInt();
  // Clamp to reasonable range: 10MB - 500MB
  settings.pixmapCacheSizeMB = qBound(10, settings.pixmapCacheSizeMB, 500);
  // Load timing settings (direct ms/count values)
  settings.keyboardRepeatIntervalMs = s.value("keyboardRepeatIntervalMs", 260).toInt();
  settings.keyboardRepeatDelayMs = s.value("keyboardRepeatDelayMs", 260).toInt();
  settings.clickHoldDelayMs = s.value("clickHoldDelayMs", 500).toInt();
  settings.clickHoldRepeatIntervalMs = s.value("clickHoldRepeatIntervalMs", 320).toInt();
  settings.mouseWheelRows = s.value("mouseWheelRows", 1).toInt();
  settings.scrollAnimationDurationMs = s.value("scrollAnimationDurationMs", 1500).toInt();
  // Load text appearance settings
  settings.titleTintSaturation = s.value("titleTintSaturation", 180).toInt();
  settings.titleTintLightness = s.value("titleTintLightness", 60).toInt();
  settings.titleBaseColor = s.value("titleBaseColor", QString()).toString();
  settings.customFontFamily = s.value("customFontFamily", QString()).toString();
  s.endGroup();

  settings.lastSelectedItems.clear();
  m_generalSettings = settings;
}

// Saves general settings (no legacy last_selected.dat persistence)
void SettingsManager::saveGeneralSettings(const GeneralSettings &settings) {
  m_generalSettings.rememberSelection = settings.rememberSelection;
  m_generalSettings.wrapNavigation = settings.wrapNavigation;
  m_generalSettings.pixmapCacheSizeMB = qBound(10, settings.pixmapCacheSizeMB, 500);
  m_generalSettings.keyboardRepeatIntervalMs = settings.keyboardRepeatIntervalMs;
  m_generalSettings.keyboardRepeatDelayMs = settings.keyboardRepeatDelayMs;
  m_generalSettings.clickHoldDelayMs = settings.clickHoldDelayMs;
  m_generalSettings.clickHoldRepeatIntervalMs = settings.clickHoldRepeatIntervalMs;
  m_generalSettings.mouseWheelRows = settings.mouseWheelRows;
  m_generalSettings.scrollAnimationDurationMs = settings.scrollAnimationDurationMs;
  m_generalSettings.titleTintSaturation = settings.titleTintSaturation;
  m_generalSettings.titleTintLightness = settings.titleTintLightness;
  m_generalSettings.titleBaseColor = settings.titleBaseColor;
  m_generalSettings.customFontFamily = settings.customFontFamily;

  QSettings s(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  s.beginGroup("General");
  s.setValue("rememberSelection", m_generalSettings.rememberSelection);
  s.setValue("wrapNavigation", m_generalSettings.wrapNavigation);
  s.setValue("pixmapCacheSizeMB", m_generalSettings.pixmapCacheSizeMB);
  s.setValue("keyboardRepeatIntervalMs", m_generalSettings.keyboardRepeatIntervalMs);
  s.setValue("keyboardRepeatDelayMs", m_generalSettings.keyboardRepeatDelayMs);
  s.setValue("clickHoldDelayMs", m_generalSettings.clickHoldDelayMs);
  s.setValue("clickHoldRepeatIntervalMs", m_generalSettings.clickHoldRepeatIntervalMs);
  s.setValue("mouseWheelRows", m_generalSettings.mouseWheelRows);
  s.setValue("scrollAnimationDurationMs", m_generalSettings.scrollAnimationDurationMs);
  s.setValue("titleTintSaturation", m_generalSettings.titleTintSaturation);
  s.setValue("titleTintLightness", m_generalSettings.titleTintLightness);
  s.setValue("titleBaseColor", m_generalSettings.titleBaseColor);
  s.setValue("customFontFamily", m_generalSettings.customFontFamily);
  s.endGroup();
  s.sync();
}

// Updates a single collection's last selected item (in-memory only; persistent
// cache handled elsewhere)
void SettingsManager::setLastSelectedItem(int collectionIndex, int itemIndex) {
  if (collectionIndex < 0) {
    return;
  }
  m_generalSettings.lastSelectedItems[collectionIndex] = itemIndex;
}

auto SettingsManager::getLastSelectedItem(int collectionIndex) const -> int {
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if ((mainWindow) && collectionIndex >= 0 &&
      collectionIndex < mainWindow->m_collections.size()) {
    QString hierarchicalName = CollectionUtils::hierarchicalNameFor(
        mainWindow->m_collections[collectionIndex], mainWindow->m_collections);
    int persistentIndex = -1;
    if (m_sessionManager) {
      persistentIndex =
          m_sessionManager->getLastSelectedIndex(hierarchicalName);
    }
    if (persistentIndex >= 0) {
      return persistentIndex;
    }

    QString collectionName = mainWindow->m_collections[collectionIndex].name;
    if (m_sessionManager) {
      persistentIndex =
          m_sessionManager->getLastSelectedIndex(collectionName);
    }
    if (persistentIndex >= 0) {
      return persistentIndex;
    }
  }

  if (m_generalSettings.lastSelectedItems.contains(collectionIndex)) {
    return m_generalSettings.lastSelectedItems.value(collectionIndex, -1);
  }

  return -1;
}

