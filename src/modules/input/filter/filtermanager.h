#ifndef FILTERMANAGER_H
#define FILTERMANAGER_H

#include "collection/collectioncontext.h"
#include "collection/collectionhierarchycache.h"
#include "ifiltermanager.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class IDatabaseManager;
#include "applicationcontext_fwd.h"

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
 * to actual item indices in the ScrollDataStore's actual-index space
 * [subcollections][virtualFolders][media], enabling efficient virtual
 * scrolling with filtering. When the store's unified sort is active, callers
 * pass the store's concat→actual permutation map so the emitted indices are
 * positions in the permuted unified list (see setSourceData).
 *
 * Usage:
 *   // Setup dependencies
 *   filterManager->setApplicationContext(ctx);
 *   filterManager->setCollections(&collections);
 *   filterManager->setSourceData(&filePaths, &fileNames, &displayNames,
 * &subcollections, &virtualFolders);
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
   *
   * The five container parameters are POINTERS to caller-owned storage that
   * must outlive every later FilterManager call (production passes
   * ScrollDataStore members) — the manager stores them, it does not copy.
   * Pointers rather than const refs on purpose (Kartend-tnyff): the
   * reference version silently bound temporaries, and a braced `{}` argument
   * compiled cleanly, dangled at the end of the call statement, and cost
   * five CI jobs before ASan named it. `&QStringList{}` does not compile.
   *
   * @param filePaths List of file paths to filter.
   * @param fileNames Map of file path to display name.
   * @param filePathToDisplayName Map for subcollection item display names.
   * @param subcollections List of subcollection indices.
   * @param virtualFolders List of virtual folder paths rendered between the
   * subcollection band and the media band. Folders participate in name
   * matching (by their display name, mirroring subcollections) and shift the
   * media band so emitted indices line up with the ScrollDataStore's
   * [subcollections][virtualFolders][media] actual-index space.
   * @param unifiedConcatToActual When the store's unified sort is active, the
   * permutation map from unpermuted concat positions to actual positions in
   * the unified list (ScrollDataStore::unifiedConcatToActualMap()). Filtered
   * indices are translated through it (and re-sorted so the filtered view
   * keeps the unified display order). Pass an empty list when unified sort is
   * inactive — the spaces coincide.
   */
  void setSourceData(const QStringList *filePaths, const QHash<QString, QString> *fileNames,
                     const QHash<QString, QString> *filePathToDisplayName,
                     const QList<int> *subcollections, const QStringList *virtualFolders,
                     const QList<int> &unifiedConcatToActual = {});

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
  [[nodiscard]] QString hideMissingArtworkDirectory() const {
    return m_hideMissingArtworkDirectory;
  }
  /// True when every directory in the artwork lookup cascade behind the
  /// hideMissingArtwork key set is warm — i.e. a miss is a real "artless"
  /// verdict, not a cold cache. While false, mediaItemHasArtwork fails open
  /// (visible) and the caller should prewarm + retry (Kartend-l66sn).
  [[nodiscard]] bool artworkKeySetSettled() const;

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
  [[nodiscard]] bool matchesVirtualFolderFilter(int folderIndex, const QString &needle) const;
  [[nodiscard]] bool matchesMediaItemFilter(int mediaIndex, const QString &needle) const;

  // subcollection + virtual folder band size; media actual indices start here.
  [[nodiscard]] int prefixItemCount() const;

  // Translate m_filteredIndices from unpermuted concat space into the store's
  // actual-index space when unified sort is active (no-op otherwise), then
  // sort ascending so the filtered view keeps the unified display order.
  void remapFilteredIndicesToStoreSpace();
  [[nodiscard]] QString getDisplayNameForMediaItem(const QString &rawEntry) const;

  // returns true when artwork lookup for the media file at
  // @p mediaIndex resolves to a real file. Returns true when the artwork
  // directory or filename is empty so the predicate is a no-op for collections
  // that have no artwork pipeline configured.
  [[nodiscard]] bool mediaItemHasArtwork(int mediaIndex) const;

  // Rebuild m_artworkKeySet from the DirectoryCache listings when the cached
  // copy is stale (directory changed, or the cache's contentsGeneration
  // moved). Cheap when fresh: one generation read plus a string compare.
  void ensureArtworkKeySet() const;

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
  const QStringList *m_virtualFolders = nullptr;

  // Unified-sort permutation (concat position → actual position). Stored by
  // value (implicitly shared) because callers pass a freshly built temporary;
  // empty when the store's unified sort is inactive.
  QList<int> m_concatToActual;

  // Context for resolution
  CollectionContext m_context;

  // Per-rawEntry media display-name cache. displayNameForMediaEntry does a
  // QDir/absoluteFilePath/QFileInfo per call on the non-showAll path, and the
  // rebuild loop calls it for every media item on every keystroke; cache it and
  // invalidate in setSourceData/setContext (its only inputs besides rawEntry).
  mutable QHash<QString, QString> m_displayNameCache;

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

  // Precomputed artwork-backed key set for the hideMissingArtwork predicate
  // (ArtworkUtils::buildArtworkKeySet over m_hideMissingArtworkDirectory).
  // Every filter pass used to run the full findArtworkForFileCached cascade
  // per media item — up to 20 DirectoryCache probes under the global lock,
  // each with a potential first-miss stat sweep — a multi-second GUI freeze
  // at 10k+ items. The set is built once and reused until the cache's
  // contentsGeneration moves or the directory changes (ensureArtworkKeySet).
  mutable QSet<QString> m_artworkKeySet;
  mutable quint64 m_artworkKeySetGeneration = 0;
  mutable QString m_artworkKeySetDirectory;
  mutable bool m_artworkKeySetValid = false;
  // Whether the cascade backing m_artworkKeySet was fully cached when the
  // set was built; stamped by ensureArtworkKeySet per generation.
  mutable bool m_artworkKeySetSettled = false;
};

#endif
