#ifndef COLLECTIONUTILS_H
#define COLLECTIONUTILS_H

#include <QDir>
#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <uiconstants.h>

// Forward declaration for validation
namespace ErrorUtils {
struct ErrorContext;
}

enum class HorizontalAlignment { Left = 0, Center = 1, Right = 2 };

enum class SidebarMode { Overlay = 0, Expand = 1 };

enum class BackgroundType { Color = 0, Image = 1 };

namespace CollectionUtils {

[[nodiscard]] inline QString alignmentToString(HorizontalAlignment alignment) {
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

[[nodiscard]] inline HorizontalAlignment stringToAlignment(const QString &str) {
  QString lower = str.toLower();
  if (lower == "left")
    return HorizontalAlignment::Left;
  if (lower == "right")
    return HorizontalAlignment::Right;
  return HorizontalAlignment::Center;
}

} // namespace CollectionUtils

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
  bool hideSubcollectionTitles = false;
  HorizontalAlignment horizontalAlignment = HorizontalAlignment::Center;
  SidebarMode sidebarMode = SidebarMode::Overlay;
  int horizontalSpacing = UIConstants::Grid::SPACING;
  int verticalSpacing = 20;
  bool hideHorizontalScrollbar = false;
  bool hideVerticalScrollbar = false;
  int itemWidth = UIConstants::Item::DEFAULT_WIDTH;
  int itemHeight = UIConstants::Item::DEFAULT_HEIGHT;
  int fontSize = UIConstants::Item::DEFAULT_FONT_SIZE;
  int cornerRadius = UIConstants::Item::DEFAULT_CORNER_RADIUS;
  
  // Background settings
  BackgroundType backgroundType = BackgroundType::Color;
  QString backgroundColor;   // Background color (hex like #1a1a2e)
  QString backgroundImage;    // Background image path
  QString primaryColor;       // Primary UI color for toolbar, menubar, search bar
  QString tileColor;          // Color for item tiles/placeholders (if blank, uses default)
  QString selectionColor;     // Color for selection rectangle and glide overlay border
  
  // Folder browsing options
  bool includeContentSubfolders = false;   // Show subfolders as virtual navigable folders
  bool includeArtworkSubfolders = false;   // Match artwork from subfolders
  bool showAllSubfolderItems = false;      // Mix subfolder items with parent (like showAllSubcollectionItems)
  bool hideSubfolderTitles = false;        // Hide titles on virtual folder widgets
  bool showHiddenFolders = false;          // Show hidden folders (starting with dot)
  
  // Virtual subfolder tracking (runtime only, not persisted)
  QString currentSubfolder;                // Current virtual subfolder path (relative to mediaDirectory)

  CollectionConfig()
      : gridWidth(4), sidebarVisible(false),
        horizontalAlignment(HorizontalAlignment::Center) {}

  bool operator==(const CollectionConfig &other) const {
    return name == other.name && 
           launcherPath == other.launcherPath &&
           corePath == other.corePath &&
           launchParameters == other.launchParameters &&
           mediaDirectory == other.mediaDirectory &&
           artworkDirectory == other.artworkDirectory &&
           collectionIcon == other.collectionIcon &&
           extensions == other.extensions &&
           gridWidth == other.gridWidth &&
           sidebarVisible == other.sidebarVisible &&
           parentCollectionIndex == other.parentCollectionIndex &&
           isSubcollection == other.isSubcollection &&
           showAllSubcollectionItems == other.showAllSubcollectionItems &&
           hideTitles == other.hideTitles &&
           hideSubcollectionTitles == other.hideSubcollectionTitles &&
           horizontalAlignment == other.horizontalAlignment &&
           sidebarMode == other.sidebarMode &&
           horizontalSpacing == other.horizontalSpacing &&
           verticalSpacing == other.verticalSpacing &&
           hideHorizontalScrollbar == other.hideHorizontalScrollbar &&
           hideVerticalScrollbar == other.hideVerticalScrollbar &&
           itemWidth == other.itemWidth &&
           itemHeight == other.itemHeight &&
           fontSize == other.fontSize &&
           cornerRadius == other.cornerRadius &&
           backgroundType == other.backgroundType &&
           backgroundColor == other.backgroundColor &&
           backgroundImage == other.backgroundImage &&
           primaryColor == other.primaryColor &&
           tileColor == other.tileColor &&
           selectionColor == other.selectionColor &&
           includeContentSubfolders == other.includeContentSubfolders &&
           includeArtworkSubfolders == other.includeArtworkSubfolders &&
           showAllSubfolderItems == other.showAllSubfolderItems &&
           hideSubfolderTitles == other.hideSubfolderTitles &&
           showHiddenFolders == other.showHiddenFolders;
  }

  // Validation methods
  [[nodiscard]] bool isValid() const { return !name.isEmpty(); }
  
  [[nodiscard]] bool hasMediaDirectory() const { return !mediaDirectory.isEmpty(); }
  
  [[nodiscard]] bool hasArtworkDirectory() const { return !artworkDirectory.isEmpty(); }
  
  // Validates numeric fields are within acceptable ranges
  void clampValues() {
    gridWidth = std::clamp(gridWidth, UIConstants::Grid::MIN_WIDTH, UIConstants::Grid::MAX_WIDTH);
    itemWidth = std::clamp(itemWidth, UIConstants::Item::MIN_WIDTH, UIConstants::Item::MAX_WIDTH);
    itemHeight = std::clamp(itemHeight, UIConstants::Item::MIN_HEIGHT, UIConstants::Item::MAX_HEIGHT);
    fontSize = std::clamp(fontSize, UIConstants::Item::MIN_FONT_SIZE, UIConstants::Item::MAX_FONT_SIZE);
    cornerRadius = std::clamp(cornerRadius, UIConstants::Item::MIN_CORNER_RADIUS, UIConstants::Item::MAX_CORNER_RADIUS);
    // Spacing can be negative for overlap effects
    horizontalSpacing = std::clamp(horizontalSpacing, -100, 200);
    verticalSpacing = std::clamp(verticalSpacing, -100, 200);
  }
};

// Collection index validation helpers - reduces repeated null checks
// Placed after CollectionConfig definition to avoid forward declaration issues
namespace CollectionUtils {

// ─────────────────────────────────────────────────────────────────────────────
// Index validation helpers - reduces repeated null/bounds checks
// ─────────────────────────────────────────────────────────────────────────────

/// Validates index against collection pointer (null-safe)
[[nodiscard]] inline bool isValidIndex(int index,
                                       const QList<CollectionConfig> *collections) {
  return collections && index >= 0 && index < collections->size();
}

/// Validates index pointer against collection pointer (null-safe for both)
[[nodiscard]] inline bool isValidIndex(const int *indexPtr,
                                       const QList<CollectionConfig> *collections) {
  return indexPtr && isValidIndex(*indexPtr, collections);
}

/// Validates index against collection reference (no null check needed)
[[nodiscard]] inline bool isValidIndex(int index,
                                       const QList<CollectionConfig> &collections) {
  return index >= 0 && index < collections.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Collection property accessors with validation
// ─────────────────────────────────────────────────────────────────────────────

/// Get grid width with fallback to default - reduces duplication across managers
[[nodiscard]] inline int getGridWidth(const int *indexPtr,
                                      const QList<CollectionConfig> *collections) {
  if (!isValidIndex(indexPtr, collections)) {
    return UIConstants::Grid::DEFAULT_WIDTH;
  }
  return (*collections)[*indexPtr].gridWidth;
}

// ─────────────────────────────────────────────────────────────────────────────
// Virtual folder counting
// ─────────────────────────────────────────────────────────────────────────────

/// Count virtual folders (subdirectories) for a collection config.
/// Returns 0 if includeContentSubfolders is disabled or showAllSubfolderItems is enabled.
[[nodiscard]] inline int countVirtualFolders(const CollectionConfig &config) {
  // Only count virtual folders if includeContentSubfolders is enabled
  // AND showAllSubfolderItems is false (otherwise items are flattened)
  if (!config.includeContentSubfolders || config.showAllSubfolderItems) {
    return 0;
  }
  
  // Determine the effective directory to scan
  QString scanDir = config.mediaDirectory;
  if (!config.currentSubfolder.isEmpty()) {
    scanDir = QDir(scanDir).absoluteFilePath(config.currentSubfolder);
  }
  
  QDir dir(scanDir);
  if (!dir.exists()) {
    return 0;
  }
  
  // Count subdirectories, optionally including hidden folders
  QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
  if (config.showHiddenFolders) {
    filters |= QDir::Hidden;
  }
  return dir.entryList(filters).size();
}

} // namespace CollectionUtils

/// Sort mode for collection items
enum class SortMode {
  NameAscending,   // A → Z (default)
  NameDescending,  // Z → A
  Random           // Shuffle
};

struct CollectionContext {
  int currentIndex = -1;
  CollectionConfig config;
  QString artworkDirectory;
  QStringList filePaths;
  QHash<QString, QString> fileNames;
  SortMode sortMode = SortMode::NameAscending;  // Sort mode for this view
  bool excludeSubfoldersFromSort = false;       // Exclude subfolders/subcollections from sorting
  [[nodiscard]] bool isValid() const { return currentIndex >= 0; }
};

struct GeneralSettings {
  bool rememberSelection = false;
  bool wrapNavigation = false;
  int pixmapCacheSizeMB = 50; // Default 50MB, user configurable
  int keyboardRepeatIntervalMs = 260;      // Keyboard repeat interval in ms
  int keyboardRepeatDelayMs = 260;         // Initial delay before keyboard repeat starts
  int clickHoldDelayMs = 500;              // Click-hold activation delay in ms
  int clickHoldRepeatIntervalMs = 320;     // Interval between click-hold repeat steps
  int mouseWheelRows = 1;                  // Rows to scroll per wheel step
  int scrollAnimationDurationMs = 1500;    // Scroll animation duration in ms
  // Text appearance settings
  int titleTintSaturation = 180;           // Title text saturation (0-255)
  int titleTintLightness = 60;             // Title text lightness (0-255)
  QString titleBaseColor;                  // Base color for title text (empty = use highlight)
  QString customFontFamily;                // Custom font family (empty = system default)
  SortMode sortMode = SortMode::NameAscending;  // Current sort mode
  bool excludeSubfoldersFromSort = false;  // Exclude subfolders/subcollections from sorting
  QHash<int, int> lastSelectedItems;
  GeneralSettings() = default;
};

// Cache for collection hierarchy lookups - avoids repeated O(n) scans
class CollectionHierarchyCache {
public:
  CollectionHierarchyCache() = default;
  
  void rebuild(const QList<CollectionConfig> &collections) {
    m_directChildren.clear();
    m_allDescendants.clear();
    m_collections = &collections;
    
    // Build direct children map
    for (int i = 0; i < collections.size(); ++i) {
      int parent = collections[i].parentCollectionIndex;
      if (parent >= 0) {
        m_directChildren[parent].append(i);
      }
    }
    
    // Pre-compute all descendants for each collection
    for (int i = 0; i < collections.size(); ++i) {
      m_allDescendants[i] = computeDescendants(i);
    }
  }
  
  [[nodiscard]] QList<int> directChildren(int parentIndex) const {
    return m_directChildren.value(parentIndex);
  }
  
  [[nodiscard]] QList<int> allDescendants(int parentIndex) const {
    return m_allDescendants.value(parentIndex);
  }
  
  [[nodiscard]] bool isValid() const { return m_collections; }
  
private:
  QList<int> computeDescendants(int parentIndex) const {
    QList<int> result;
    QList<int> stack = m_directChildren.value(parentIndex);
    while (!stack.isEmpty()) {
      int idx = stack.takeFirst();
      result.append(idx);
      stack.append(m_directChildren.value(idx));
    }
    return result;
  }
  
  const QList<CollectionConfig> *m_collections = nullptr;
  QHash<int, QList<int>> m_directChildren;
  QHash<int, QList<int>> m_allDescendants;
};

// Legacy inline functions for backward compatibility
namespace CollectionUtils {

[[nodiscard]] inline QList<int>
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

[[nodiscard]] inline QString hierarchicalNameFor(const CollectionConfig &collection,
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

[[nodiscard]] inline QList<int> directChildrenOf(int parentIndex,
                                   const QList<CollectionConfig> &collections) {
  QList<int> children;
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex == parentIndex) {
      children.append(i);
    }
  }
  return children;
}

/**
 * @brief Computes a deterministic UUID from collection name and media directory.
 * @param name Collection name.
 * @param mediaDir Media directory path.
 * @return SHA1 hash as hex string.
 */
[[nodiscard]] QString computeCollectionUuid(const QString &name, const QString &mediaDir);

} // namespace CollectionUtils

Q_DECLARE_METATYPE(CollectionConfig)
Q_DECLARE_METATYPE(CollectionContext)

#endif