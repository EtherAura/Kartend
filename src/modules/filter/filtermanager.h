#ifndef FILTERMANAGER_H
#define FILTERMANAGER_H

#include "collectionutils.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class DatabaseManager;

/**
 * @brief Manages search filtering and subcollection filtering for the scroll view.
 *
 * FilterManager handles two types of filtering:
 * 1. Text search filtering - matches items by display name
 * 2. Subcollection filtering - shows items belonging to a subcollection and its descendants
 *
 * The manager maintains a list of filtered indices that map visual positions
 * to actual item indices, enabling efficient virtual scrolling with filtering.
 *
 * Usage:
 *   // Setup dependencies
 *   filterManager->setDatabaseManager(dbManager);
 *   filterManager->setCollections(&collections);
 *   filterManager->setSourceData(filePaths, fileNames, displayNames, subcollections);
 *
 *   // Apply text search
 *   filterManager->applyFilter("search text");
 *
 *   // Map visual index to actual index
 *   int actualIndex = filterManager->getActualIndex(visualIndex);
 */
class FilterManager : public QObject {
  Q_OBJECT
public:
  explicit FilterManager(QObject *parent = nullptr);
  ~FilterManager() override = default;

  // ─────────────────────────────────────────────────────────────────────────
  // Dependency injection
  // ─────────────────────────────────────────────────────────────────────────

  void setDatabaseManager(DatabaseManager *manager);
  void setCollections(const QList<CollectionConfig> *collections);
  void setHierarchyCache(const CollectionHierarchyCache *cache);

  /**
   * @brief Set source data for filtering.
   * @param filePaths List of file paths to filter.
   * @param fileNames Map of file path to display name.
   * @param filePathToDisplayName Map for subcollection item display names.
   * @param subcollections List of subcollection indices.
   */
  void setSourceData(const QStringList &filePaths,
                     const QHash<QString, QString> &fileNames,
                     const QHash<QString, QString> &filePathToDisplayName,
                     const QList<int> &subcollections);

  /**
   * @brief Set current collection context for display name resolution.
   */
  void setContext(const CollectionContext &context);

  // ─────────────────────────────────────────────────────────────────────────
  // Filter operations
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Apply text search filter.
   * @param searchText Text to search for (case-insensitive substring match).
   */
  void applyFilter(const QString &searchText);

  /**
   * @brief Apply subcollection filter to show items from subcollection and descendants.
   * @param subcollectionIndex Index of the subcollection to filter by.
   */
  void applySubcollectionFilter(int subcollectionIndex);

  /**
   * @brief Clear all filters and restore unfiltered view.
   */
  void clearFilter();

  // ─────────────────────────────────────────────────────────────────────────
  // Query filter state
  // ─────────────────────────────────────────────────────────────────────────

  [[nodiscard]] bool isFiltered() const { return m_isFiltered; }
  [[nodiscard]] const QString &currentFilter() const { return m_currentFilter; }
  [[nodiscard]] const QList<int> &filteredIndices() const {
    return m_filteredIndices;
  }

  /**
   * @brief Map visual index to actual item index.
   * @param visualIndex Index in the filtered view.
   * @return Actual item index, or -1 if out of range.
   */
  [[nodiscard]] int getActualIndex(int visualIndex) const;

  /**
   * @brief Get count of items after filtering.
   */
  [[nodiscard]] int filteredCount() const { return m_filteredIndices.size(); }

signals:
  /**
   * @brief Emitted when filter changes.
   * @param visibleItems Number of visible media items after filtering.
   * @param totalItems Total number of media items before filtering.
   */
  void filterChanged(int visibleItems, int totalItems);

private:
  void rebuildFilteredIndices();
  [[nodiscard]] bool matchesSubcollectionFilter(int subcollectionIndex,
                                                const QString &needle) const;
  [[nodiscard]] bool matchesMediaItemFilter(int mediaIndex,
                                            const QString &needle) const;
  [[nodiscard]] QString getDisplayNameForMediaItem(const QString &rawEntry) const;

  void determineTargetCollections(int subcollectionIndex,
                                  QSet<int> &targetCollections);
  [[nodiscard]] bool itemBelongsToTargetCollections(
      const QString &entry, const QSet<int> &targetCollections) const;

  // Dependencies
  DatabaseManager *m_databaseManager = nullptr;
  const QList<CollectionConfig> *m_collections = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;

  // Source data references
  const QStringList *m_filePaths = nullptr;
  const QHash<QString, QString> *m_fileNames = nullptr;
  const QHash<QString, QString> *m_filePathToDisplayName = nullptr;
  const QList<int> *m_subcollections = nullptr;

  // Context for resolution
  CollectionContext m_context;

  // Filter state
  bool m_isFiltered = false;
  QString m_currentFilter;
  QList<int> m_filteredIndices;
};

#endif
