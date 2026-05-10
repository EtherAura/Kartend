#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>

#include "collectionutils.h"
#include "errorutils.h"
#include "filemapcache.h"
#include "historystore.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "usagestatsstore.h"

class CachedCountsService;
class QueryManager;
class QThread;
class SessionManager;
struct ApplicationContext;

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
class DatabaseManager : public QObject {
  Q_OBJECT
public:
  explicit DatabaseManager(const ApplicationContext *ctx, QObject *parent = nullptr);
  ~DatabaseManager() override;

  void initDatabase();
  void loadAllCollections(const QList<CollectionConfig> &allCollections);
  void loadItems(const CollectionContext &context, const QList<CollectionConfig> &allCollections);
  void loadItemsWithSubcollections(const CollectionContext &context,
                                   const QList<CollectionConfig> &allCollections);
  void updateCachedCounts(const QList<CollectionConfig> &allCollections);

  void fetchItemCount(const CollectionContext &context,
                      const QList<CollectionConfig> &allCollections,
                      const QString &filter = QString(), int requestToken = 0);
  void fetchItemsRange(const CollectionContext &context,
                       const QList<CollectionConfig> &allCollections, int offset, int limit,
                       const QString &filter = QString());

  /// Finds the visual index of a specific file path in the current sorted
  /// order. Used to restore selection after sort mode changes.
  void fetchVisualIndexForPath(const CollectionContext &context,
                               const QList<CollectionConfig> &allCollections,
                               const QString &filePath);

  // Clears cached items for a collection to force a fresh rescan
  void invalidateCollectionCache(const QString &collectionUuid);

  [[nodiscard]] int getCollectionIndexForFile(const QString &filePath) const;
  [[nodiscard]] QString findArtworkDirectoryForFile(const QString &filePath) const;

  // File path resolution for relative paths in showAllSubcollectionItems mode
  [[nodiscard]] QString resolveFilePath(const QString &rawEntry,
                                        const CollectionContext &context) const;
  [[nodiscard]] QString resolveRelativeFilePath(const QString &rawFileName,
                                                const QHash<QString, QString> &fileNames) const;
  [[nodiscard]] qint64
  countCollectionRecursive(int collectionIndex,
                           const QList<CollectionConfig> &allCollections) const;

  /// Reads `collections.last_scanned` for the given UUID via the main-thread
  /// connection. Returns an invalid QDateTime when the row is
  /// missing or the query fails — callers use that as the "never scanned"
  /// signal.
  [[nodiscard]] QDateTime loadCollectionLastScanned(const QString &collectionUuid) const;

  /// Loads extended metadata for the given (collectionUuid, path) using the
  /// main-thread connection. Returns an empty `ItemMetadata` (with the keys
  /// preserved) when no row exists. Errors are logged via ErrorUtils and an
  /// empty struct is returned so the sidebar can degrade gracefully.
  [[nodiscard]] ItemMetadataStore::ItemMetadata loadItemMetadata(const QString &collectionUuid,
                                                                 const QString &path) const;

  /// Persists extended metadata via the main-thread connection. Used by
  /// user-driven editors (e.g. custom fields dialog,). Returns
  /// true on success; logs the structured error and returns false otherwise.
  bool saveItemMetadata(const ItemMetadataStore::ItemMetadata &metadata);

  /// Loads every artwork row stored for (collectionUuid, path) using the
  /// main-thread connection. Returns an empty list when the
  /// item has no rows or on database failures (errors are logged), so
  /// callers can degrade silently to subdirectory auto-discovery.
  [[nodiscard]] QList<ItemArtworkStore::ItemArtwork> loadItemArtwork(const QString &collectionUuid,
                                                                     const QString &path) const;

  /// Persists a single artwork override row via the main-thread connection
  /// Used by the per-item manual-link dialog. Returns true
  /// on success; logs the structured error and returns false otherwise.
  bool saveItemArtwork(const ItemArtworkStore::ItemArtwork &artwork);

  /// Removes a single artwork override row via the main-thread connection
  /// Used by the per-item manual-link dialog when the user
  /// clears an override. Succeeds even when no matching row exists.
  bool removeItemArtwork(const QString &collectionUuid, const QString &path,
                         const QString &artworkType);

  // ──────────────────────────────────────────────────────────────────────────
  // Usage statistics
  // ──────────────────────────────────────────────────────────────────────────

  [[nodiscard]] UsageStatsStore::ItemUsageStats loadItemUsageStats(const QString &collectionUuid,
                                                                   const QString &path) const;
  void recordItemLaunch(const QString &collectionUuid, const QString &path);
  void recordItemPlaySession(const QString &collectionUuid, const QString &path, qint64 seconds);
  [[nodiscard]] UsageStatsStore::AggregateStats loadAggregateUsageStats() const;
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow> loadTopPlayedItems(int limit) const;
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow> loadRecentlyPlayedItems(int limit) const;
  [[nodiscard]] QHash<QString, UsageStatsStore::CollectionUsage> loadUsageByCollection() const;
  bool resetAllUsageStats();

  // ──────────────────────────────────────────────────────────────────────────
  // Launch history
  // ──────────────────────────────────────────────────────────────────────────

  void recordHistoryEntry(const QString &collectionUuid, const QString &path, const QString &name,
                          int maxEntries);
  [[nodiscard]] QList<HistoryStore::HistoryEntry> loadRecentHistory(int limit) const;
  [[nodiscard]] qint64 historyEntryCount() const;
  bool clearHistory();

signals:
  void itemsLoaded(const QStringList &filePaths, const QHash<QString, QString> &fileNames);
  void itemCountLoaded(int count);
  void itemCountLoadedWithToken(int count, int requestToken);
  void itemsRangeLoaded(int offset, const QStringList &filePaths,
                        const QHash<QString, QString> &fileNames,
                        const QHash<QString, QString> &fileToArtworkDir,
                        const QHash<QString, QString> &fileToMediaDir,
                        const QHash<QString, int> &fileToCollectionIndex);
  void visualIndexForPathLoaded(int visualIndex, const QString &filePath);
  void errorOccurred(const ErrorUtils::ErrorContext &error);
  void cachedCountsUpdated();
  void scanProgress(int current, int total, const QString &collectionName);
  void scanStarting(const QString &collectionName, int estimatedItems);
  void scanItemsProgress(int itemsProcessed, int totalItems);
  void collectionScanCompleted(const QString &collectionUuid);
  void collectionScanSummary(const QString &collectionUuid, int itemsScanned, int itemsApplied,
                             bool success);
  void cacheInvalidated(const QString &collectionUuid);

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

public slots:
  /// Request cancellation of any in-progress scan
  void cancelScan();

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
                                const QHash<QString, int> &fileToCollectionIndex);

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
};

#endif
