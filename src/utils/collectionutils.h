#ifndef COLLECTIONUTILS_H
#define COLLECTIONUTILS_H

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
  
  // Folder browsing options
  bool includeContentSubfolders = false;   // Show subfolders as virtual navigable folders
  bool includeArtworkSubfolders = false;   // Match artwork from subfolders
  bool showAllSubfolderItems = false;      // Mix subfolder items with parent (like showAllSubcollectionItems)
  bool hideSubfolderTitles = false;        // Hide titles on virtual folder widgets
  
  // Virtual subfolder tracking (runtime only, not persisted)
  QString currentSubfolder;                // Current virtual subfolder path (relative to mediaDirectory)

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

} // namespace CollectionUtils

struct CollectionContext {
  int currentIndex = -1;
  CollectionConfig config;
  QString artworkDirectory;
  QStringList filePaths;
  QHash<QString, QString> fileNames;
  [[nodiscard]] bool isValid() const { return currentIndex >= 0; }
};

struct GeneralSettings {
  bool rememberSelection = false;
  bool wrapNavigation = false;
  int pixmapCacheSizeMB = 50; // Default 50MB, user configurable
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