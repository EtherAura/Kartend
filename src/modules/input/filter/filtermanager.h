#ifndef FILTERMANAGER_H
#define FILTERMANAGER_H

#include "collectionutils.h"
#include "ifiltermanager.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class IDatabaseManager;
struct ApplicationContext;

/**
 * @brief Manages search filtering and subcollection filtering for the scroll
 * view.
 *
 * FilterManager handles two types of filtering:
 * 1. Text search filtering - matches items by display name
 * 2. Subcollection filtering - shows items belonging to a subcollection and its
 * descendants
 *
 * The manager maintains a list of filtered indices that map visual positions
 * to actual item indices, enabling efficient virtual scrolling with filtering.
 *
 * Usage:
 *   // Setup dependencies
 *   filterManager->setApplicationContext(ctx);
 *   filterManager->setCollections(&collections);
 *   filterManager->setSourceData(filePaths, fileNames, displayNames,
 * subcollections);
 *
 *   // Apply text search
 *   filterManager->applyFilter("search text");
 *
 *   // Map visual index to actual index
 *   int actualIndex = filterManager->getActualIndex(visualIndex);
 */
class FilterManager : public QObject, public IFilterManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(FilterManager)
public:
  explicit FilterManager(QObject *parent = nullptr);
  ~FilterManager() override = default;

  // ─────────────────────────────────────────────────────────────────────────
  // Dependency injection
  // ─────────────────────────────────────────────────────────────────────────

  // Kartend-davi: ctx-routed access replaces the cached pointer; callers
  // wire the context once instead of pushing a separate IDatabaseManager
  // pointer that can drift out of sync with ApplicationContext::managers.
  void setApplicationContext(const ApplicationContext *ctx);
  void setCollections(const QList<CollectionConfig> *collections);
  void setHierarchyCache(const CollectionHierarchyCache *cache);

  /**
   * @brief Set source data for filtering.
   * @param filePaths List of file paths to filter.
   * @param fileNames Map of file path to display name.
   * @param filePathToDisplayName Map for subcollection item display names.
   * @param subcollections List of subcollection indices.
   */
  void setSourceData(const QStringList &filePaths, const QHash<QString, QString> &fileNames,
                     const QHash<QString, QString> &filePathToDisplayName,
                     const QList<int> &subcollections);

  /**
   * @brief Set current collection context for display name resolution.
   */
  void setContext(const CollectionContext &context);

  /**
   * @brief Configure the "hide items without artwork" predicate.
   *
   * When @p enabled is true, every active filter — including the unfiltered
   * passthrough — additionally hides media items whose artwork lookup in
   * @p artworkDirectory returns no match. Subcollections and virtual folders
   * are not affected. Toggling the flag does not refresh the filtered view on
   * its own; callers should invoke the apply* / clearFilter() entry points
   * (or the ScrollManager-level refresh) to rebuild indices.
   */
  void setHideMissingArtworkFilter(bool enabled, const QString &artworkDirectory);

  [[nodiscard]] bool hideMissingArtworkEnabled() const { return m_hideMissingArtwork; }

  // ─────────────────────────────────────────────────────────────────────────
  // Filter operations
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Apply text search filter.
   * @param searchText Text to search for (case-insensitive substring match).
   */
  void applyFilter(const QString &searchText);

  /**
   * @brief Apply subcollection filter to show items from subcollection and
   * descendants.
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

  [[nodiscard]] bool isFiltered() const override { return m_isFiltered; }
  [[nodiscard]] const QString &currentFilter() const { return m_currentFilter; }
  [[nodiscard]] const QList<int> &filteredIndices() const { return m_filteredIndices; }

  /**
   * @brief Map visual index to actual item index.
   * @param visualIndex Index in the filtered view.
   * @return Actual item index, or -1 if out of range.
   */
  [[nodiscard]] int getActualIndex(int visualIndex) const override;

  /**
   * @brief Get count of items after filtering.
   */
  [[nodiscard]] int filteredCount() const override { return m_filteredIndices.size(); }

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
  [[nodiscard]] bool matchesMediaItemFilter(int mediaIndex, const QString &needle) const;
  [[nodiscard]] QString getDisplayNameForMediaItem(const QString &rawEntry) const;

  // returns true when artwork lookup for the media file at
  // @p mediaIndex resolves to a real file. Returns true when the artwork
  // directory or filename is empty so the predicate is a no-op for collections
  // that have no artwork pipeline configured.
  [[nodiscard]] bool mediaItemHasArtwork(int mediaIndex) const;

  void determineTargetCollections(int subcollectionIndex, QSet<int> &targetCollections);
  [[nodiscard]] bool itemBelongsToTargetCollections(const QString &entry,
                                                    const QSet<int> &targetCollections) const;

  // Dependencies
  [[nodiscard]] IDatabaseManager *dbMgr() const;

  const ApplicationContext *m_ctx = nullptr;
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

  // per-collection "hide items without artwork" toggle.
  // Combines with the active search/subcollection filter as an additional
  // predicate; when no other filter is active the manager still reports
  // m_isFiltered = true so the visual→actual index map runs through
  // m_filteredIndices.
  bool m_hideMissingArtwork = false;
  QString m_hideMissingArtworkDirectory;
};

#endif
