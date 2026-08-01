#ifndef SCROLLDATAMANAGER_H
#define SCROLLDATAMANAGER_H

#include "collection/collectioncontext.h"
#include "collection/collectionhierarchycache.h"
#include <QDir>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <random>
#include <variant>

class DatabaseManager;

/**
 * @brief Represents an item in the sorted unified view (when sortSubfolders is
 * enabled).
 */
struct UnifiedItem {
  enum class Type { Subcollection, VirtualFolder, MediaFile };
  Type type;
  int originalIndex;   // Index into the original list for this type
  QString displayName; // Sort key
};

/**
 * @brief Manages scroll view data: file paths, file names, subcollections, and
 * virtual folders.
 *
 * This class encapsulates the data storage and initialization logic that was
 * previously scattered throughout ScrollManager, providing a cleaner separation
 * of concerns.
 */
class ScrollDataStore : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScrollDataStore)
public:
  explicit ScrollDataStore(QObject *parent = nullptr);
  ~ScrollDataStore() override = default;

  // ─────────────────────────────────────────────────────────────────────────
  // Initialization
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Initialize data storage for a given item count.
   * @param totalCount Number of items (excluding subcollections and virtual
   * folders)
   */
  void initializeStorage(int totalCount);

  // Resize storage without clearing already-loaded ranges.
  // Used when item counts change after a background rescan.
  void resizeStorage(int totalCount);

  /**
   * @brief Initialize subcollections from collection hierarchy.
   * @param context Current collection context
   * @param collections Pointer to collections list
   * @param hierarchyCache Optional hierarchy cache for O(1) lookups
   */
  void initializeSubcollections(const CollectionContext &context,
                                const QList<CollectionConfig> *collections,
                                const CollectionHierarchyCache *hierarchyCache);

  /**
   * @brief Initialize virtual folders from filesystem.
   * @param context Current collection context
   */
  void initializeVirtualFolders(const CollectionContext &context);

  /**
   * @brief Apply unified sorting to all items (subcollections, virtual folders,
   * files).
   * @param context Current collection context with sortMode and sortSubfolders
   * @param collections Pointer to collections list for subcollection name
   * lookup
   */
  void applyUnifiedSort(const CollectionContext &context,
                        const QList<CollectionConfig> *collections);

  /**
   * @brief Set up file path to display name mappings.
   * @param context Current collection context
   */
  void setupFilePathMappings(const CollectionContext &context);

  /**
   * @brief Clear all data.
   */
  void clear();

  // ─────────────────────────────────────────────────────────────────────────
  // Data Access
  // ─────────────────────────────────────────────────────────────────────────

  [[nodiscard]] const QStringList &filePaths() const { return m_filePaths; }
  [[nodiscard]] QStringList &filePaths() { return m_filePaths; }

  [[nodiscard]] const QHash<QString, QString> &fileNames() const { return m_fileNames; }
  [[nodiscard]] QHash<QString, QString> &fileNames() { return m_fileNames; }

  [[nodiscard]] const QList<int> &subcollections() const { return m_subcollections; }
  [[nodiscard]] const QStringList &virtualFolders() const { return m_virtualFolders; }

  [[nodiscard]] const QHash<QString, QString> &filePathToDisplayName() const {
    return m_filePathToDisplayName;
  }

  [[nodiscard]] int subcollectionCount() const { return m_subcollections.size(); }
  [[nodiscard]] int virtualFolderCount() const { return m_virtualFolders.size(); }
  [[nodiscard]] int fileCount() const { return m_filePaths.size(); }

  /**
   * @brief Get total item count (subcollections + virtual folders + files).
   */
  [[nodiscard]] int totalItemCount() const {
    return m_subcollections.size() + m_virtualFolders.size() + m_filePaths.size();
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Index Resolution
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Check if visual index corresponds to a subcollection.
   * @param actualIndex Index after filter transformation
   */
  [[nodiscard]] bool isSubcollectionIndex(int actualIndex) const {
    if (m_unifiedSortActive) {
      if (actualIndex < 0 || actualIndex >= m_unifiedItems.size()) return false;
      return m_unifiedItems[actualIndex].type == UnifiedItem::Type::Subcollection;
    }
    return actualIndex >= 0 && actualIndex < m_subcollections.size();
  }

  /**
   * @brief Check if visual index corresponds to a virtual folder.
   * @param actualIndex Index after filter transformation
   */
  [[nodiscard]] bool isVirtualFolderIndex(int actualIndex) const {
    if (m_unifiedSortActive) {
      if (actualIndex < 0 || actualIndex >= m_unifiedItems.size()) return false;
      return m_unifiedItems[actualIndex].type == UnifiedItem::Type::VirtualFolder;
    }
    int subCount = m_subcollections.size();
    return actualIndex >= subCount && actualIndex < subCount + m_virtualFolders.size();
  }

  /**
   * @brief Check if visual index corresponds to a media file.
   * @param actualIndex Index after filter transformation
   */
  [[nodiscard]] bool isMediaIndex(int actualIndex) const {
    if (m_unifiedSortActive) {
      if (actualIndex < 0 || actualIndex >= m_unifiedItems.size()) return false;
      return m_unifiedItems[actualIndex].type == UnifiedItem::Type::MediaFile;
    }
    int prefixCount = m_subcollections.size() + m_virtualFolders.size();
    return actualIndex >= prefixCount && actualIndex < prefixCount + m_filePaths.size();
  }

  /**
   * @brief Get subcollection index from visual index.
   * @param actualIndex Index after filter transformation
   * @return Subcollection index or -1 if not a subcollection
   */
  [[nodiscard]] int subcollectionIndexFromActual(int actualIndex) const {
    if (m_unifiedSortActive) {
      if (!isSubcollectionIndex(actualIndex)) return -1;
      return m_subcollections[m_unifiedItems[actualIndex].originalIndex];
    }
    if (!isSubcollectionIndex(actualIndex)) return -1;
    return m_subcollections[actualIndex];
  }

  /**
   * @brief Get virtual folder path from visual index.
   * @param actualIndex Index after filter transformation
   * @return Folder path or empty string if not a virtual folder
   */
  [[nodiscard]] QString virtualFolderFromActual(int actualIndex) const {
    if (m_unifiedSortActive) {
      if (!isVirtualFolderIndex(actualIndex)) return {};
      return m_virtualFolders[m_unifiedItems[actualIndex].originalIndex];
    }
    if (!isVirtualFolderIndex(actualIndex)) return {};
    int folderIndex = actualIndex - m_subcollections.size();
    return m_virtualFolders[folderIndex];
  }

  /**
   * @brief Get media file index (into m_filePaths) from visual index.
   * @param actualIndex Index after filter transformation
   * @return Media index or -1 if not a media item
   */
  [[nodiscard]] int mediaIndexFromActual(int actualIndex) const {
    if (m_unifiedSortActive) {
      if (!isMediaIndex(actualIndex)) return -1;
      return m_unifiedItems[actualIndex].originalIndex;
    }
    int prefixCount = m_subcollections.size() + m_virtualFolders.size();
    if (actualIndex < prefixCount) return -1;
    int mediaIndex = actualIndex - prefixCount;
    if (mediaIndex >= m_filePaths.size()) return -1;
    return mediaIndex;
  }

  /**
   * @brief Get raw file path for a media index.
   * @param mediaIndex Index into m_filePaths
   */
  [[nodiscard]] QString rawFilePath(int mediaIndex) const {
    if (mediaIndex < 0 || mediaIndex >= m_filePaths.size()) return {};
    return m_filePaths[mediaIndex];
  }

  /**
   * @brief Check if unified sorting is currently active.
   */
  [[nodiscard]] bool isUnifiedSortActive() const { return m_unifiedSortActive; }

  /**
   * @brief Permutation from unpermuted concat space to actual-index space.
   *
   * Concat space is the fixed [subcollections][virtualFolders][files] layout;
   * when unified sorting is active, actual indices are positions in the
   * permuted m_unifiedItems list instead. The returned list satisfies
   * map[concatIndex] == actual position of that item in the unified order.
   * Returns an empty list when unified sorting is inactive (the two spaces
   * coincide, so the identity map is implied). Consumers that build index
   * lists in concat space (FilterManager) translate through this so their
   * output stays classifiable by isSubcollectionIndex / isVirtualFolderIndex
   * / isMediaIndex.
   */
  [[nodiscard]] QList<int> unifiedConcatToActualMap() const;

  /**
   * @brief Find visual index for a given file path.
   * @param filePath File path to find
   * @return Visual index or -1 if not found
   */
  [[nodiscard]] int visualIndexFromFilePath(const QString &filePath) const;

  // ─────────────────────────────────────────────────────────────────────────
  // Data Updates
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Receive and store a range of items from database.
   * @param offset Starting index
   * @param paths File paths
   * @param names File path to display name mapping
   * @return List of visual indices that were updated
   */
  QList<int> receiveItemsRange(int offset, const QStringList &paths,
                               const QHash<QString, QString> &names);

private:
  QStringList m_filePaths;
  QHash<QString, QString> m_fileNames;
  // O(1) path -> index-into-m_filePaths cache for visualIndexFromFilePath (was a
  // linear scan). Rebuilt lazily on the dirty flag (set wherever this class
  // mutates m_filePaths) and, as a self-healing backstop, when a lookup returns
  // a stale index — e.g. after a write through the non-const filePaths() accessor
  // that did not flag dirty.
  mutable QHash<QString, int> m_pathToMediaIndexCache;
  mutable bool m_pathIndexCacheDirty = true;
  QList<int> m_subcollections;
  QStringList m_virtualFolders;
  QHash<QString, QString> m_filePathToDisplayName;

  // Unified sorting state
  bool m_unifiedSortActive = false;
  QList<UnifiedItem> m_unifiedItems;
};

#endif // SCROLLDATAMANAGER_H
