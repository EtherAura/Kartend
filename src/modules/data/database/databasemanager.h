#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QSqlDatabase>

#include "filemapcache.h"
#include "idatabasemanager.h"
#include "itemmetadatacache.h"

class CachedCountsService;
class QueryManager;
class QThread;
class SessionManager;
#include "applicationcontext_fwd.h"

/**
 * @brief Coordinates database operations via a dedicated worker thread.
 *
 * Owns the main-thread SQLite connection, two QueryManager workers (one for
 * queries, one for scans) on dedicated QThreads, and three helpers that carry
 * the responsibilities the class previously held inline:
 *   - DatabaseSchema: opens the connection, applies pragmas, builds tables and
 *     indexes (pure functions, no state).
 *   - FileMapCache: thread-safe owner of the path-to-{artworkDir,mediaDir,
 *     collectionIndex} hashes plus the relative-path resolver.
 *   - CachedCountsService: debounced async cached-count refresh.
 *
 * Threading model:
 *   - Main thread only: every public method on this class.
 *   - Worker thread: QueryManager executes the actual SQL.
 *   - Communication: signals/slots with Qt::QueuedConnection.
 *
 * Thread-safe (any thread):
 *   - resolveFilePath(), resolveRelativeFilePath(),
 *     getCollectionIndexForFile(), findArtworkDirectoryForFile() — read-only,
 *     guarded by FileMapCache's internal mutex.
 *
 * Not thread-safe (main thread only):
 *   - initDatabase(), loadAllCollections(), loadItems(), every Save/Record API.
 */
class DatabaseManager : public IDatabaseManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DatabaseManager)
public:
  explicit DatabaseManager(const ApplicationContext *ctx, QObject *parent = nullptr);
  ~DatabaseManager() override;

  void initDatabase() override;

  /// Qt SQL connection name used by the main-thread connection. Suffixed
  /// per-instance so multiple DatabaseManagers can coexist without
  /// colliding in QSqlDatabase's process-global connection registry. Tests
  /// query this to verify connection cleanup without hardcoding the suffix.
  [[nodiscard]] QString connectionName() const override { return m_connectionName; }
  void loadAllCollections(const QList<CollectionConfig> &allCollections) override;
  void loadItems(const CollectionContext &context,
                 const QList<CollectionConfig> &allCollections) override;
  void loadItemsWithSubcollections(const CollectionContext &context,
                                   const QList<CollectionConfig> &allCollections) override;
  void updateCachedCounts(const QList<CollectionConfig> &allCollections) override;

  void fetchItemCount(const CollectionContext &context,
                      const QList<CollectionConfig> &allCollections,
                      const QString &filter = QString(), int requestToken = 0) override;
  void fetchItemsRange(const CollectionContext &context,
                       const QList<CollectionConfig> &allCollections, int offset, int limit,
                       const QString &filter = QString()) override;

  /// Finds the visual index of a specific file path in the current sorted
  /// order. Used to restore selection after sort mode changes.
  void fetchVisualIndexForPath(const CollectionContext &context,
                               const QList<CollectionConfig> &allCollections,
                               const QString &filePath) override;

  // Clears cached items for a collection to force a fresh rescan
  void invalidateCollectionCache(const QString &collectionUuid) override;

  [[nodiscard]] int getCollectionIndexForFile(const QString &filePath) const override;
  [[nodiscard]] QString findArtworkDirectoryForFile(const QString &filePath) const override;

  // File path resolution for relative paths in showAllSubcollectionItems mode
  [[nodiscard]] QString resolveFilePath(const QString &rawEntry,
                                        const CollectionContext &context) const override;
  [[nodiscard]] QString
  resolveRelativeFilePath(const QString &rawFileName,
                          const QHash<QString, QString> &fileNames) const override;
  [[nodiscard]] qint64
  countCollectionRecursive(int collectionIndex,
                           const QList<CollectionConfig> &allCollections) const override;

  /// Reads `collections.last_scanned` for the given UUID via the main-thread
  /// connection. Returns an invalid QDateTime when the row is
  /// missing or the query fails — callers use that as the "never scanned"
  /// signal.
  [[nodiscard]] QDateTime loadCollectionLastScanned(const QString &collectionUuid) const override;

  /// Loads extended metadata for the given (collectionUuid, path) using the
  /// main-thread connection. Returns an empty `ItemMetadata` (with the keys
  /// preserved) when no row exists. Errors are logged via ErrorUtils and an
  /// empty struct is returned so the sidebar can degrade gracefully.
  [[nodiscard]] ItemMetadataStore::ItemMetadata
  loadItemMetadata(const QString &collectionUuid, const QString &path) const override;

  /// Batched WHERE-path-IN counterpart to loadItemMetadata. Cache-aware:
  /// pulls cache hits first, issues a single chunked SELECT for the
  /// remainder, and back-populates the cache with the rows that came back.
  /// Paths without a row return as empty-but-keyed stubs.
  [[nodiscard]] QHash<QString, ItemMetadataStore::ItemMetadata>
  loadItemMetadataBatch(const QString &collectionUuid, const QStringList &paths) const override;

  /// Persists extended metadata via the main-thread connection. Used by
  /// user-driven editors (e.g. custom fields dialog,). Returns
  /// true on success; logs the structured error and returns false otherwise.
  bool saveItemMetadata(const ItemMetadataStore::ItemMetadata &metadata) override;

  /// Loads every artwork row stored for (collectionUuid, path) using the
  /// main-thread connection. Returns an empty list when the
  /// item has no rows or on database failures (errors are logged), so
  /// callers can degrade silently to subdirectory auto-discovery.
  [[nodiscard]] QList<ItemArtworkStore::ItemArtwork>
  loadItemArtwork(const QString &collectionUuid, const QString &path) const override;

  /// Persists a single artwork override row via the main-thread connection
  /// Used by the per-item manual-link dialog. Returns true
  /// on success; logs the structured error and returns false otherwise.
  bool saveItemArtwork(const ItemArtworkStore::ItemArtwork &artwork) override;

  /// Removes a single artwork override row via the main-thread connection
  /// Used by the per-item manual-link dialog when the user
  /// clears an override. Succeeds even when no matching row exists.
  bool removeItemArtwork(const QString &collectionUuid, const QString &path,
                         const QString &artworkType) override;

  /// External-writer cache invalidation hook. Drops the (uuid, path)
  /// entry from `m_metadataCache`. Called from the main thread by the
  /// BatchScrapeRunner write worker after its own connection commits a
  /// per-item save — without this the sidebar would render stale data
  /// for any item that had been touched by the cache before the scrape
  /// rewrote it. Cheap and idempotent.
  void invalidateMetadataCacheItem(const QString &collectionUuid, const QString &path) override;

  // ──────────────────────────────────────────────────────────────────────────
  // Usage statistics
  // ──────────────────────────────────────────────────────────────────────────

  [[nodiscard]] UsageStatsStore::ItemUsageStats
  loadItemUsageStats(const QString &collectionUuid, const QString &path) const override;
  void recordItemLaunch(const QString &collectionUuid, const QString &path) override;
  void recordItemPlaySession(const QString &collectionUuid, const QString &path,
                             qint64 seconds) override;
  [[nodiscard]] UsageStatsStore::AggregateStats loadAggregateUsageStats() const override;
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow> loadTopPlayedItems(int limit) const override;
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow>
  loadRecentlyPlayedItems(int limit) const override;
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow> loadNeverPlayedItems(int limit) const override;
  [[nodiscard]] qint64 countItemsPlayedSince(const QString &isoCutoffUtc) const override;
  [[nodiscard]] QHash<QString, UsageStatsStore::CollectionUsage>
  loadUsageByCollection() const override;
  bool resetAllUsageStats() override;

  void migrateCollectionUuid(const QString &oldUuid, const QString &newUuid) override;

  /// Whole-library path + artwork_path enumeration for the
  /// collection-health dashboard. Single SQL pass against the main-thread
  /// connection; no caching since the dashboard is a one-shot read.
  [[nodiscard]] QList<IDatabaseManager::ItemPathRow>
  loadAllItemPathsForCollection(const QString &collectionUuid) const override;
  /// Single SQL pass over item_metadata for the collection's per-item state
  /// flags. Only rows with at least one flag set are returned so the result
  /// hash stays small for libraries where most items have no markers.
  [[nodiscard]] QHash<QString, IDatabaseManager::ItemStateFlags>
  loadItemStateFlagsForCollection(const QString &collectionUuid) const override;
  void purgeOrphanCollectionData(const QList<CollectionConfig> &liveCollections) override;

  // ──────────────────────────────────────────────────────────────────────────
  // Launch history
  // ──────────────────────────────────────────────────────────────────────────

  void recordHistoryEntry(const QString &collectionUuid, const QString &path, const QString &name,
                          int maxEntries) override;
  [[nodiscard]] QList<HistoryStore::HistoryEntry> loadRecentHistory(int limit) const override;
  [[nodiscard]] qint64 historyEntryCount() const override;
  bool clearHistory() override;

signals:
  // Internal signals to trigger worker
  void requestLoadAllCollections(const QList<CollectionConfig> &allCollections);
  void requestLoadItems(const CollectionContext &context,
                        const QList<CollectionConfig> &allCollections);
  void requestLoadItemsWithSubcollections(const CollectionContext &context,
                                          const QList<CollectionConfig> &allCollections);
  void requestFetchItemCount(const CollectionContext &context,
                             const QList<CollectionConfig> &allCollections, const QString &filter,
                             int requestToken);
  void requestFetchItemsRange(const CollectionContext &context,
                              const QList<CollectionConfig> &allCollections, int offset, int limit,
                              const QString &filter);
  void requestFetchVisualIndexForPath(const CollectionContext &context,
                                      const QList<CollectionConfig> &allCollections,
                                      const QString &filePath);
  void requestInvalidateCache(const QString &collectionUuid);

  // Internal signal to trigger background scan on dedicated scan worker.
  void requestEnsureScannedForContext(const CollectionContext &context,
                                      const QList<CollectionConfig> &allCollections);

  // Internal signal to trigger lazy background FTS backfill on scan worker.
  void requestEnsureItemsFtsReady();

  // Queued-connection equivalent of m_worker->requestCancelScan() — see
  // cancelScan() impl for the Kartend-fvye rationale.
  void cancelScanRequested();

public slots:
  /// Request cancellation of any in-progress scan
  void cancelScan() override;

private slots:
  void onWorkerItemsLoaded(const QStringList &filePaths, const QHash<QString, QString> &fileNames,
                           const QHash<QString, QString> &fileToArtworkDir,
                           const QHash<QString, QString> &fileToMediaDir,
                           const QHash<QString, int> &fileToCollectionIndex);
  void onWorkerItemCountLoaded(int count);
  void onWorkerItemCountLoadedWithToken(int count, int requestToken);
  void onWorkerItemsRangeLoaded(int offset, const QStringList &filePaths,
                                const QHash<QString, QString> &fileNames,
                                const QHash<QString, QString> &fileToArtworkDir,
                                const QHash<QString, QString> &fileToMediaDir,
                                const QHash<QString, int> &fileToCollectionIndex,
                                int requestedCollectionIndex);

private:
  [[nodiscard]] qint64 countCollectionByUuid(const QString &collectionUuid) const;
  void clearCollectionFromDatabaseByUuid(const QString &collectionUuid);

  // ctx is the single source of truth for sibling managers (SessionManager).
  const ApplicationContext *m_ctx = nullptr;
  QueryManager *m_worker = nullptr;
  QueryManager *m_scanWorker = nullptr;
  QThread *m_workerThread = nullptr;
  QThread *m_scanThread = nullptr;

  QSqlDatabase m_db;
  QString m_connectionName;

  FileMapCache m_fileMapCache;
  CachedCountsService *m_cachedCounts = nullptr;

  // Per-item LRU cache for the three load methods the sidebar refresh
  // fires per settled selection. Mutable so the const load methods can
  // populate it on miss. Invalidated by every Save/Record write below
  // and by invalidateCollectionCache() for whole-collection refreshes.
  mutable ItemMetadataCache m_metadataCache{ItemMetadataCache::DEFAULT_CAPACITY};
};

#endif
