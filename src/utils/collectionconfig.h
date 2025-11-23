#ifndef COLLECTIONCONFIG_H
#define COLLECTIONCONFIG_H

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <uiconstants.h>

enum class HorizontalAlignment { Left = 0, Center = 1, Right = 2 };

enum class SidebarMode { Overlay = 0, Expand = 1 };

inline QString alignmentToString(HorizontalAlignment alignment) {
  switch (alignment) {
  case HorizontalAlignment::Left:
    return "left";
  case HorizontalAlignment::Center:
    return "center";
  case HorizontalAlignment::Right:
    return "right";
  default:
    return "center";
  }
}

inline HorizontalAlignment stringToAlignment(const QString &str) {
  QString lower = str.toLower();
  if (lower == "left")
    return HorizontalAlignment::Left;
  if (lower == "right")
    return HorizontalAlignment::Right;
  return HorizontalAlignment::Center;
}

struct CollectionConfig {
  QString name;
  QString launcherPath;
  QString corePath;
  QString launchParameters;
  QString mediaDirectory;
  QString artworkDirectory;
  QString collectionIcon;
  QStringList extensions;
  int gridWidth;
  bool sidebarVisible;
  int parentCollectionIndex = -1;
  bool isSubcollection = false;
  bool hasParent() const { return parentCollectionIndex >= 0; }
  bool showAllSubcollectionItems = false;
  bool hideTitles = false;
  bool showSubcollectionTitles = true;
  HorizontalAlignment horizontalAlignment = HorizontalAlignment::Center;
  SidebarMode sidebarMode = SidebarMode::Overlay;
  int horizontalSpacing = UIConstants::GRID_SPACING;
  int verticalSpacing = 20;
  bool hideHorizontalScrollbar = false;
  bool hideVerticalScrollbar = false;
  int itemWidth = UIConstants::DEFAULT_ITEM_WIDTH;
  int itemHeight = UIConstants::DEFAULT_ITEM_HEIGHT;
  int fontSize = UIConstants::DEFAULT_FONT_SIZE;

  CollectionConfig()
      : gridWidth(4), sidebarVisible(false),
        horizontalAlignment(HorizontalAlignment::Center) {}

  bool operator==(const CollectionConfig &other) const {
    return name == other.name && launcherPath == other.launcherPath &&
           corePath == other.corePath &&
           launchParameters == other.launchParameters &&
           mediaDirectory == other.mediaDirectory &&
           artworkDirectory == other.artworkDirectory &&
           horizontalAlignment == other.horizontalAlignment;
  }
};

struct CollectionContext {
  int currentIndex = -1;
  CollectionConfig config;
  QString artworkDirectory;
  QStringList filePaths;
  QHash<QString, QString> fileNames;
  bool isValid() const { return currentIndex >= 0; }
};

struct MainScreenConfig {
  int gridWidth;
  HorizontalAlignment horizontalAlignment;
  bool showHiddenCollections;

  MainScreenConfig()
      : gridWidth(4), horizontalAlignment(HorizontalAlignment::Center),
        showHiddenCollections(false) {}

  bool operator==(const MainScreenConfig &other) const {
    return gridWidth == other.gridWidth &&
           horizontalAlignment == other.horizontalAlignment &&
           showHiddenCollections == other.showHiddenCollections;
  }

  bool operator!=(const MainScreenConfig &other) const {
    return !(*this == other);
  }
};

struct GeneralSettings {
  bool rememberSelection = false;
  bool wrapNavigation = false;
  QHash<int, int> lastSelectedItems;
  GeneralSettings() = default;
};

inline QList<int>
collectDescendantIndices(int parentIndex,
                         const QList<CollectionConfig> &collections) {
  QList<int> descendants;
  for (int index = 0; index < collections.size(); ++index) {
    if (collections[index].parentCollectionIndex == parentIndex) {
      descendants.append(index);
      descendants.append(collectDescendantIndices(index, collections));
    }
  }
  return descendants;
}

inline QString hierarchicalNameFor(const CollectionConfig &collection,
                                   const QList<CollectionConfig> &collections) {
  if (!collection.isSubcollection || collection.parentCollectionIndex < 0) {
    return collection.name;
  }
  QStringList parts;
  parts.prepend(collection.name);
  int parent = collection.parentCollectionIndex;
  while (parent >= 0 && parent < collections.size()) {
    const CollectionConfig &p = collections[parent];
    parts.prepend(p.name);
    if (!p.isSubcollection)
      break;
    parent = p.parentCollectionIndex;
  }
  return parts.join('/');
}

inline QList<int> directChildrenOf(int parentIndex,
                                   const QList<CollectionConfig> &collections) {
  QList<int> children;
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex == parentIndex) {
      children.append(i);
    }
  }
  return children;
}

Q_DECLARE_METATYPE(CollectionConfig)
Q_DECLARE_METATYPE(CollectionContext)

#endif