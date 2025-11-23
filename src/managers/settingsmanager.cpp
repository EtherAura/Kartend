#include "settingsmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionconfig.h"
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
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QScrollArea>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QScrollArea>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

// Construct settings manager and initialize QSettings.
SettingsManager::SettingsManager(QObject *parent) : QObject(parent) {
  QDir configDir(QDir::homePath() + "/.config/kartend");
  if (!configDir.exists()) {
    configDir.mkpath(".");
  }
}

SettingsManager::~SettingsManager() = default;

// Return absolute path to config file.
auto SettingsManager::getConfigPath() -> QString {
  QDir configDir(QDir::homePath() + "/.config/kartend");
  return configDir.absoluteFilePath("kartend.cfg");
}

auto SettingsManager::parseConfigFile(
    QFile &file, QHash<QString, CollectionConfig> &tempCollections,
    bool &needsRewrite) -> bool {
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }

  QTextStream inputStream(&file);
  QString currentSection;
  CollectionConfig currentCollection;
  bool hasValidData = false;
  bool inGeneral = false;

  while (!inputStream.atEnd()) {
    QString line = inputStream.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#')) {
      continue;
    }

    if (line.startsWith('[') && line.endsWith(']')) {
      if (!currentSection.isEmpty() && !inGeneral &&
          !currentCollection.name.isEmpty()) {
        tempCollections[currentSection] = currentCollection;
        hasValidData = true;
      }
      currentSection = line.mid(1, line.length() - 2);
      currentCollection = CollectionConfig();
      inGeneral = (currentSection == "General");
      continue;
    }

    if (inGeneral) {
      processGeneralConfigLine(line);
      continue;
    }

    if (currentSection.isEmpty()) {
      continue;
    }

    processConfigLine(line, currentCollection, needsRewrite);
  }

  if (!currentSection.isEmpty() && !inGeneral &&
      !currentCollection.name.isEmpty()) {
    tempCollections[currentSection] = currentCollection;
    hasValidData = true;
  }

  file.close();
  return hasValidData;
}

void SettingsManager::processGeneralConfigLine(const QString &line) {
  // General settings are handled by loadGeneralSettings method
  // Just skip processing these lines in loadCollections
  Q_UNUSED(line);
}

auto SettingsManager::processConfigLine(const QString &line,
                                        CollectionConfig &currentCollection,
                                        bool &needsRewrite) -> void {
  int equalPos = line.indexOf('=');
  if (equalPos == -1) {
    return;
  }

  QString key = line.left(equalPos).trimmed();
  QString value = line.mid(equalPos + 1).trimmed();

  if (key == "name") {
    currentCollection.name = value;
  } else if (key == "launcherPath") {
    currentCollection.launcherPath = value;
  } else if (key == "corePath") {
    currentCollection.corePath = value;
  } else if (key == "launchParameters") {
    currentCollection.launchParameters = value;
  } else if (key == "mediaDirectory") {
    currentCollection.mediaDirectory = value;
  } else if (key == "artworkDirectory") {
    currentCollection.artworkDirectory = value;
  } else if (key == "collectionIcon") {
    currentCollection.collectionIcon = value;
  } else if (key == "extensions") {
    setCollectionExtensions(value, currentCollection, needsRewrite);
  } else if (key == "gridWidth") {
    setCollectionGridWidth(value, currentCollection);
  } else if (key == "sidebarVisible") {
    currentCollection.sidebarVisible = (value == "true");
  } else if (key == "showAllSubcollectionItems") {
    currentCollection.showAllSubcollectionItems = (value == "true");
  } else if (key == "horizontalAlignment") {
    currentCollection.horizontalAlignment = stringToAlignment(value);
  } else if (key == "sidebarMode") {
    currentCollection.sidebarMode =
        (value == "fixed") ? SidebarMode::Expand : SidebarMode::Overlay;
  } else if (key == "hideHorizontalScrollbar") {
    currentCollection.hideHorizontalScrollbar = (value == "true");
  } else if (key == "hideVerticalScrollbar") {
    currentCollection.hideVerticalScrollbar = (value == "true");
  } else if (key == "hideTitles") {
    currentCollection.hideTitles = (value == "true");
  } else if (key == "showSubcollectionTitles") {
    currentCollection.showSubcollectionTitles = (value == "true");
  } else if (key == "horizontalSpacing") {
    setCollectionSpacing(value, currentCollection.horizontalSpacing);
  } else if (key == "verticalSpacing") {
    setCollectionSpacing(value, currentCollection.verticalSpacing);
  } else if (key == "itemWidth") {
    setCollectionItemWidth(value, currentCollection);
  } else if (key == "itemHeight") {
    setCollectionItemHeight(value, currentCollection);
  } else if (key == "fontSize") {
    setCollectionFontSize(value, currentCollection);
  }
}

auto SettingsManager::setCollectionExtensions(const QString &value,
                                              CollectionConfig &collection,
                                              bool &needsRewrite) -> void {
  QStringList rawList = value.split(',', Qt::SkipEmptyParts);
  for (QString &extension : rawList) {
    extension = extension.trimmed();
  }
  QStringList normalized = ExtensionUtils::normalizeStoredExtensions(rawList);
  if (normalized != rawList) {
    needsRewrite = true;
  }
  collection.extensions = normalized;
}

auto SettingsManager::setCollectionGridWidth(const QString &value,
                                             CollectionConfig &collection)
    -> void {
  int gridWidth = value.toInt();
  if (gridWidth >= UIConstants::MIN_GRID_WIDTH &&
      gridWidth <= UIConstants::MAX_GRID_WIDTH) {
    collection.gridWidth = gridWidth;
  }
}

void SettingsManager::setCollectionSpacing(const QString &value, int &spacing) {
  bool conversionOk;
  int spacingValue = value.toInt(&conversionOk);
  if (conversionOk && spacingValue >= UIConstants::SPACING_MIN &&
      spacingValue <= UIConstants::SPACING_MAX) {
    spacing = spacingValue;
  }
}

auto SettingsManager::setCollectionItemWidth(const QString &value,
                                             CollectionConfig &collection)
    -> void {
  int width = value.toInt();
  if (width >= UIConstants::MIN_ITEM_WIDTH &&
      width <= UIConstants::MAX_ITEM_WIDTH) {
    collection.itemWidth = width;
  }
}

auto SettingsManager::setCollectionItemHeight(const QString &value,
                                              CollectionConfig &collection)
    -> void {
  int height = value.toInt();
  if (height >= UIConstants::MIN_ITEM_HEIGHT &&
      height <= UIConstants::MAX_ITEM_HEIGHT) {
    collection.itemHeight = height;
  }
}

auto SettingsManager::setCollectionFontSize(const QString &value,
                                            CollectionConfig &collection)
    -> void {
  int size = value.toInt();
  if (size >= UIConstants::MIN_FONT_SIZE &&
      size <= UIConstants::MAX_FONT_SIZE) {
    collection.fontSize = size;
  }
}

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

auto SettingsManager::processSubcollection(const QString &sectionName,
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

auto SettingsManager::findParentCollectionIndex(
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
            hierarchicalNameFor(collections[i], collections);
        if (actualParentPath == expectedParentPath) {
          return i;
        }
      }
    }
  }
  return -1;
}

// Loads collections from config (no automatic default collections; leaves list
// empty if none)
void SettingsManager::loadCollections(
    QList<CollectionConfig> &collections) const {
  collections.clear();

  QFile file(getConfigPath());
  if (!file.exists()) {
    return;
  }

  QHash<QString, CollectionConfig> tempCollections;
  bool needsRewrite = false;

  if (parseConfigFile(file, tempCollections, needsRewrite)) {
    finalizeCollections(tempCollections, collections, needsRewrite);
  }
}

// Persist collection configurations to disk (no lastSelected_* entries)
void SettingsManager::saveCollections(
    const QList<CollectionConfig> &collections) const {
  QFile file(getConfigPath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return;
  }
  QTextStream out(&file);

  out << "[General]\n";
  out << "rememberSelection="
      << (m_generalSettings.rememberSelection ? "true" : "false") << "\n";
  out << "wrapNavigation="
      << (m_generalSettings.wrapNavigation ? "true" : "false") << "\n\n";

  QStringList sectionNames;
  QHash<QString, int> sectionToIndex;
  for (int i = 0; i < collections.size(); ++i) {
    QString sectionName = hierarchicalNameFor(collections[i], collections);
    if (!sectionName.isEmpty()) {
      sectionNames.append(sectionName);
      sectionToIndex[sectionName] = i;
    }
  }
  sectionNames.sort();

  for (const QString &sectionName : sectionNames) {
    int index = sectionToIndex[sectionName];
    const CollectionConfig &collection = collections[index];

    out << "[" << sectionName << "]\n";
    out << "name=" << collection.name << "\n";
    out << "launcherPath=" << collection.launcherPath << "\n";
    out << "corePath=" << collection.corePath << "\n";
    out << "launchParameters=" << collection.launchParameters << "\n";
    out << "mediaDirectory=" << collection.mediaDirectory << "\n";
    out << "artworkDirectory=" << collection.artworkDirectory << "\n";
    out << "collectionIcon=" << collection.collectionIcon << "\n";
    out << "itemWidth=" << collection.itemWidth << "\n";
    out << "itemHeight=" << collection.itemHeight << "\n";
    out << "fontSize=" << collection.fontSize << "\n";

    QStringList extensionsList;
    for (const QString &ext : collection.extensions) {
      extensionsList.append(ext);
    }
    out << "extensions=" << extensionsList.join(", ") << "\n";

    out << "gridWidth=" << collection.gridWidth << "\n";
    out << "sidebarVisible=" << (collection.sidebarVisible ? "true" : "false")
        << "\n";
    out << "showAllSubcollectionItems="
        << (collection.showAllSubcollectionItems ? "true" : "false") << "\n";
    out << "horizontalAlignment="
        << alignmentToString(collection.horizontalAlignment) << "\n";
    QString sidebarModeStr =
        (collection.sidebarMode == SidebarMode::Expand) ? "fixed" : "overlay";
    out << "sidebarMode=" << sidebarModeStr << "\n";
    out << "hideHorizontalScrollbar="
        << (collection.hideHorizontalScrollbar ? "true" : "false") << "\n";
    out << "hideVerticalScrollbar="
        << (collection.hideVerticalScrollbar ? "true" : "false") << "\n";
    out << "hideTitles=" << (collection.hideTitles ? "true" : "false") << "\n";
    out << "showSubcollectionTitles="
        << (collection.showSubcollectionTitles ? "true" : "false") << "\n";
    out << "horizontalSpacing=" << collection.horizontalSpacing << "\n";
    out << "verticalSpacing=" << collection.verticalSpacing << "\n\n";
  }
  file.close();
}

// Load main screen (global layout) settings.
void SettingsManager::loadMainScreenSettings(MainScreenConfig &config) {
  QFile file(getConfigPath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    config.gridWidth = UIConstants::DEFAULT_GRID_WIDTH;
    config.horizontalAlignment = HorizontalAlignment::Center;
    config.showHiddenCollections = false;
    return;
  }
  QTextStream inputStream(&file);
  bool inGeneral = false;
  while (!inputStream.atEnd()) {
    QString line = inputStream.readLine().trimmed();
    if (line.startsWith('[') && line.endsWith(']')) {
      QString section = line.mid(1, line.length() - 2);
      inGeneral = (section == "General");
      continue;
    }
    if (!inGeneral) {
      continue;
    }
    int equalPos = line.indexOf('=');
    if (equalPos == -1) {
      continue;
    }
    QString key = line.left(equalPos);
    QString value = line.mid(equalPos + 1);
    if (key == "MainScreen_gridWidth") {
      config.gridWidth = value.toInt();
    } else if (key == "MainScreen_horizontalAlignment") {
      config.horizontalAlignment = stringToAlignment(value);
    } else if (key == "MainScreen_showHiddenCollections") {
      config.showHiddenCollections = (value == "true");
    }
  }
  file.close();
  config.gridWidth = std::max(config.gridWidth, UIConstants::MIN_GRID_WIDTH);
  config.gridWidth = std::min(config.gridWidth, UIConstants::MAX_GRID_WIDTH);
}

// Save main screen settings while preserving other lines.
namespace {
// Reads all lines and removes existing MainScreen_* keys from [General]
auto readAndFilterGeneralSection(QFile &file, QStringList &lines,
                                 bool &foundGeneral) -> void {
  bool inGeneral = false;
  QTextStream inputStream(&file);
  while (!inputStream.atEnd()) {
    const QString line = inputStream.readLine();
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
      const QString section = trimmed.mid(1, trimmed.length() - 2);
      inGeneral = (section == "General");
      if (inGeneral) {
        foundGeneral = true;
      }
      lines.append(line);
      continue;
    }
    if (inGeneral) {
      if (trimmed.startsWith("MainScreen_gridWidth=") ||
          trimmed.startsWith("MainScreen_horizontalAlignment=") ||
          trimmed.startsWith("MainScreen_showHiddenCollections=")) {
        continue;
      }
    }
    lines.append(line);
  }
}

// Inserts the three MainScreen_* entries immediately after [General]
void insertMainScreenEntries(QStringList &lines,
                             const MainScreenConfig &config) {
  for (int i = 0; i < lines.size(); ++i) {
    if (lines[i].trimmed() == "[General]") {
      lines.insert(i + 1,
                   QString("MainScreen_gridWidth=%1").arg(config.gridWidth));
      lines.insert(i + 2,
                   QString("MainScreen_horizontalAlignment=%1")
                       .arg(alignmentToString(config.horizontalAlignment)));
      lines.insert(i + 3,
                   QString("MainScreen_showHiddenCollections=%1")
                       .arg(config.showHiddenCollections ? "true" : "false"));
      break;
    }
  }
}
} // namespace

void SettingsManager::saveMainScreenSettings(const MainScreenConfig &config) {
  QString configPath = getConfigPath();
  QStringList lines;
  bool foundGeneral = false;

  QFile file(configPath);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    readAndFilterGeneralSection(file, lines, foundGeneral);
    file.close();
  }

  if (!foundGeneral) {
    lines.prepend("");
    lines.prepend("[General]");
  }

  insertMainScreenEntries(lines, config);

  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&file);
    for (const QString &line : lines) {
      out << line << "\n";
    }
    file.close();
  }
}

// Expand variables/placeholders in a configured string.
auto SettingsManager::expandConfigVariables(const QString &input,
                                            const QString &collectionName)
    -> QString {
  return PathUtils::validateAndExpandPath(input, collectionName);
}

// Launch settings dialog and apply accepted modifications.
void SettingsManager::openSettingsDialog(QWidget *parent,
                                         QList<CollectionConfig> &collections,
                                         int &currentCollectionIndex,
                                         SidebarManager *sidebarManager,
                                         ScrollManager *scrollManager,
                                         NavigationManager *navigationManager) {
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

  hasChanges = detectChanges(
      newCollections, originalCollections, viewingCollectionIndex, needsReload,
      gridWidthChangedForView, alignmentChangedForView, spacingChangedForView,
      scrollbarChangedForView, sidebarModeChangedForView, titleChangedForView,
      fontSizeChangedForView, hideTitlesChangedForView);

  if (!hasChanges) {
    return;
  }

  if (ArtworkManager::instance().getTimerCoordinator() != nullptr) {
    ArtworkManager::instance().getTimerCoordinator()->stopAllTimers();
  }

  collections = newCollections;
  saveCollections(collections);
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

  if (sidebarManager != nullptr) {
    sidebarManager->setCollections(&collections);
    if (resolvedCollectionIndex >= 0) {
      sidebarManager->applySidebarStateForCollection(resolvedCollectionIndex);
    }
  }

  if (needsReload) {
    handleReloadRequired(collections, newCollections, originalCollections,
                         viewingCollectionIndex, sidebarManager,
                         scrollManager, navigationManager,
                         currentCollectionIndex);
  } else {
    handleLayoutChanges(parent, collections, viewingCollectionIndex,
                        titleChangedForView, scrollbarChangedForView,
                        sidebarModeChangedForView, gridWidthChangedForView,
                        spacingChangedForView, alignmentChangedForView,
                        fontSizeChangedForView, hideTitlesChangedForView,
                        sidebarManager, scrollManager,
                        currentCollectionIndex);
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
      configA.showAllSubcollectionItems != configB.showAllSubcollectionItems ||
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
                        bool &fontSizeChanged, bool &hideTitlesChanged) {
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
  if (configA.fontSize != configB.fontSize) {
    hasChanges = true;
    fontSizeChanged = true;
  }
  if (configA.hideTitles != configB.hideTitles) {
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
}

// Title update logic separated from handleLayoutChanges
void updateWindowTitle(QWidget *parent, int viewingIndex,
                       const QList<CollectionConfig> &collections) {
  if (auto *mw = qobject_cast<MainWindow *>(parent)) {
    mw->updateWindowTitleForCollection(viewingIndex);
  }
  auto *titleLabel = parent->findChild<QLabel *>("itemsTitleLabel");
  if (titleLabel != nullptr) {
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
  SettingsManager::applyHorizontalScrollbarSetting(parent, viewingIndex,
                                                   collections);
  SettingsManager::applyVerticalScrollbarSetting(parent, viewingIndex,
                                                 collections);
}

// Updates sidebar layout when mode changes
void refreshSidebar(SidebarManager *sidebarManager,
                    QList<CollectionConfig> &collections,
                    int currentCollectionIndex) {
  if (sidebarManager != nullptr) {
    sidebarManager->setCollections(&collections);
    sidebarManager->updateSidebarLayout(currentCollectionIndex);
  }
}

// Handles scroll manager branching
void handleScrollBranch(ScrollManager *scrollManager,
                        const QList<CollectionConfig> &collections,
                        int viewingIndex, bool spacingChanged,
                        bool sidebarModeChanged, bool gridWidthChanged,
                        bool alignmentChanged, bool fontSizeChanged,
                        bool hideTitlesChanged) {
  if (scrollManager == nullptr) {
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
    QTimer::singleShot(UIConstants::LONG_TIMER_DELAY, [scrollManager,
                                                       viewingIndex,
                                                       collections]() {
      if (scrollManager) {
        scrollManager->preCalculateLayout();
        scrollManager->forceVirtualViewUpdate();
        QTimer::singleShot(UIConstants::MEDIUM_TIMER_DELAY, [scrollManager,
                                                             viewingIndex,
                                                             collections]() {
          if (scrollManager) {
            scrollManager->updateVirtualView();
            ArtworkManager::instance().updateViewportArtwork();
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
} // namespace

auto SettingsManager::detectChanges(
    const QList<CollectionConfig> &newCollections,
    const QList<CollectionConfig> &originalCollections,
    int viewingCollectionIndex, bool &needsReload, bool &gridWidthChangedForView,
    bool &alignmentChangedForView, bool &spacingChangedForView,
    bool &scrollbarChangedForView, bool &sidebarModeChangedForView,
    bool &titleChangedForView, bool &fontSizeChangedForView,
    bool &hideTitlesChangedForView) -> bool {
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
                         fontSizeChangedForView, hideTitlesChangedForView);
    }
  }
  return hasChanges;
}

auto SettingsManager::handleReloadRequired(
    QList<CollectionConfig> &collections,
    const QList<CollectionConfig> &newCollections,
    const QList<CollectionConfig> &originalCollections,
    int viewingCollectionIndex, SidebarManager *sidebarManager,
    ScrollManager *scrollManager,
    NavigationManager *navigationManager, int currentCollectionIndex) -> void {
  ArtworkManager::instance().cancelAllArtworkLoading();
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
      CacheManager::instance().clearCollectionCache(viewingCollectionIndex);
      ArtworkManager::instance().clearLoadedArtworkState();
      ArtworkManager::instance().clearWidgetReferences();
    }

    if (scrollManager != nullptr) {
      scrollManager->cleanup();
    }

    QTimer::singleShot(UIConstants::MEDIUM_TIMER_DELAY,
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
    bool hideTitlesChangedForView,
    SidebarManager *sidebarManager, ScrollManager *scrollManager,
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
  handleScrollBranch(scrollManager, collections, viewingCollectionIndex,
                     spacingChangedForView, sidebarModeChangedForView,
                     gridWidthChangedForView, alignmentChangedForView,
                     fontSizeChangedForView, hideTitlesChangedForView);
}

// Apply horizontal scrollbar policy for collection.
void SettingsManager::applyHorizontalScrollbarSetting(
    QWidget *parent, int collectionIndex,
    const QList<CollectionConfig> &collections) {
  auto *itemScrollArea = parent->findChild<QScrollArea *>("itemScrollArea");
  if ((itemScrollArea == nullptr) || collectionIndex < 0 ||
      collectionIndex >= collections.size()) {
    return;
  }
  const CollectionConfig &collection = collections[collectionIndex];
  if (collection.hideHorizontalScrollbar) {
    itemScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  } else {
    itemScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

// Apply vertical scrollbar policy for collection.
void SettingsManager::applyVerticalScrollbarSetting(
    QWidget *parent, int collectionIndex,
    const QList<CollectionConfig> &collections) {
  auto *itemScrollArea = parent->findChild<QScrollArea *>("itemScrollArea");
  if ((itemScrollArea == nullptr) || collectionIndex < 0 ||
      collectionIndex >= collections.size()) {
    return;
  }
  const CollectionConfig &collection = collections[collectionIndex];
  if (collection.hideVerticalScrollbar) {
    itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  } else {
    itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

// Loads general settings (selection indices now resolved from persistent cache
// separately)
void SettingsManager::loadGeneralSettings(GeneralSettings &settings) {
  GeneralSettings loaded;
  loaded.rememberSelection = true;
  loaded.wrapNavigation = false;
  loaded.lastSelectedItems.clear();

  QFile cfg(getConfigPath());
  bool inGeneral = false;
  if (cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream inputStream(&cfg);
    while (!inputStream.atEnd()) {
      QString line = inputStream.readLine().trimmed();
      if (line.startsWith('[') && line.endsWith(']')) {
        QString section = line.mid(1, line.length() - 2);
        inGeneral = (section.compare("General", Qt::CaseInsensitive) == 0);
        continue;
      }
      if (!inGeneral) {
        continue;
      }
      int equalPos = line.indexOf('=');
      if (equalPos == -1) {
        continue;
      }
      QString key = line.left(equalPos).trimmed();
      QString value = line.mid(equalPos + 1).trimmed();
      if (key.compare("rememberSelection", Qt::CaseInsensitive) == 0) {
        loaded.rememberSelection =
            (value.compare("true", Qt::CaseInsensitive) == 0);
      } else if (key.compare("wrapNavigation", Qt::CaseInsensitive) == 0) {
        loaded.wrapNavigation =
            (value.compare("true", Qt::CaseInsensitive) == 0);
      }
    }
    cfg.close();
  }

  m_generalSettings = loaded;
  settings = loaded;
}

// Saves general settings (no legacy last_selected.dat persistence)
namespace {
auto readAndFilterGeneralForGeneralSettings(QFile &cfg, QStringList &lines,
                                            bool &foundGeneral) -> void;
auto ensureGeneralSectionPresence(QStringList &lines, bool foundGeneral)
    -> void;
auto insertGeneralEntries(QStringList &lines, bool rememberSelection,
                          bool wrapNavigation) -> void;
} // namespace
void SettingsManager::saveGeneralSettings(const GeneralSettings &settings) {
  m_generalSettings.rememberSelection = settings.rememberSelection;
  m_generalSettings.wrapNavigation = settings.wrapNavigation;

  const QString cfgPath = getConfigPath();
  QStringList lines;
  bool foundGeneral = false;

  QFile cfg(cfgPath);
  if (cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
    readAndFilterGeneralForGeneralSettings(cfg, lines, foundGeneral);
    cfg.close();
  }

  ensureGeneralSectionPresence(lines, foundGeneral);
  insertGeneralEntries(lines, m_generalSettings.rememberSelection,
                       m_generalSettings.wrapNavigation);

  if (cfg.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&cfg);
    for (const QString &line : lines) {
      out << line << "\n";
    }
    cfg.close();
  }
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
  if ((mainWindow != nullptr) && collectionIndex >= 0 &&
      collectionIndex < mainWindow->m_collections.size()) {
    QString hierarchicalName = hierarchicalNameFor(
        mainWindow->m_collections[collectionIndex], mainWindow->m_collections);
    int persistentIndex =
        SessionManager::instance().getLastSelectedIndex(hierarchicalName);
    if (persistentIndex >= 0) {
      return persistentIndex;
    }

    QString collectionName = mainWindow->m_collections[collectionIndex].name;
    persistentIndex =
        SessionManager::instance().getLastSelectedIndex(collectionName);
    if (persistentIndex >= 0) {
      return persistentIndex;
    }
  }

  if (m_generalSettings.lastSelectedItems.contains(collectionIndex)) {
    return m_generalSettings.lastSelectedItems.value(collectionIndex, -1);
  }

  return -1;
}
// Reads the file and filters [General] entries for general settings keys only.
namespace {

/// Reads settings file and filters
/// rememberSelection/wrapNavigation/lastSelected_* from the [General] section
/// while preserving all other lines.
auto readAndFilterGeneralForGeneralSettings(QFile &cfg, QStringList &lines,
                                            bool &foundGeneral) -> void {
  lines.clear();
  foundGeneral = false;

  if (!cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  const QString content = QString::fromUtf8(cfg.readAll());
  cfg.close();

  const QStringList rawLines = content.split(QLatin1Char('\n'));
  lines.reserve(rawLines.size());

  bool inGeneral = false;
  for (const QString &raw : rawLines) {
    const QString &line = raw;
    const QString trimmed = line.trimmed();

    if (trimmed.startsWith(QLatin1Char('[')) &&
        trimmed.endsWith(QLatin1Char(']'))) {
      const QString section = trimmed.mid(1, trimmed.size() - 2);
      inGeneral =
          section.compare(QStringLiteral("General"), Qt::CaseInsensitive) == 0;
      if (inGeneral) {
        foundGeneral = true;
      }
      lines.push_back(line);
      continue;
    }

    if (!inGeneral) {
      lines.push_back(line);
      continue;
    }

    const int equalsIndex = trimmed.indexOf(QLatin1Char('='));
    if (equalsIndex <= 0) {
      lines.push_back(line);
      continue;
    }

    const QString key = trimmed.left(equalsIndex);
    if (key.compare(QStringLiteral("rememberSelection"), Qt::CaseInsensitive) ==
        0) {
      continue;
    }
    if (key.compare(QStringLiteral("wrapNavigation"), Qt::CaseInsensitive) ==
        0) {
      continue;
    }
    if (key.startsWith(QStringLiteral("lastSelected_"), Qt::CaseInsensitive)) {
      continue;
    }

    lines.push_back(line);
  }
}

/// Ensures a [General] section header exists by inserting it at the top when
/// absent.
auto ensureGeneralSectionPresence(QStringList &lines, bool foundGeneral)
    -> void {
  if (foundGeneral) {
    return;
  }
  lines.push_front(QStringLiteral("[General]"));
}

/// Inserts rememberSelection and wrapNavigation entries immediately after the
/// [General] section header.
auto insertGeneralEntries(QStringList &lines, bool rememberSelection,
                          bool wrapNavigation) -> void {
  int insertPos = -1;
  for (int i = 0, lineCount = lines.size(); i < lineCount; ++i) {
    const QString trimmed = lines[i].trimmed();
    if (trimmed.startsWith(QLatin1Char('[')) &&
        trimmed.endsWith(QLatin1Char(']'))) {
      const QString section = trimmed.mid(1, trimmed.size() - 2);
      if (section.compare(QStringLiteral("General"), Qt::CaseInsensitive) ==
          0) {
        insertPos = i + 1;
        break;
      }
    }
  }
  if (insertPos < 0) {
    return;
  }

  lines.insert(insertPos++,
               QString::fromLatin1("rememberSelection=%1")
                   .arg(rememberSelection ? QStringLiteral("true")
                                          : QStringLiteral("false")));
  lines.insert(insertPos++, QString::fromLatin1("wrapNavigation=%1")
                                .arg(wrapNavigation ? QStringLiteral("true")
                                                    : QStringLiteral("false")));
}

} // namespace
