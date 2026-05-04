#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QTimer>

#include "collectionutils.h"
#include "errorutils.h"
#include "historystore.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "usagestatsstore.h"

class SessionManager;

/**
 * @brief Coordinates database operations via a dedicated worker thread.
 *
 * Threading Model:
 * - Main thread only: All public methods (loadItems, fetchItemCount, etc.)
 * - Worker thread: QueryManager executes actual SQL queries
 * - Communication: Signals/slots with Qt::QueuedConnection for thread safety
 *
 * Thread-safe operations:
 * - resolveFilePath(), resolveRelativeFilePath() - read-only, no shared mutable
 * state
 * - Signal emissions are queued to main thread automatically
 *
 * NOT thread-safe (main thread only):
 * - initDatabase(), loadAllCollections(), loadItems()
 * - All setup and configuration methods
 */
class DatabaseManager : public QObject {
  Q_OBJECT
public:
  explicit DatabaseManager(SessionManager *sessionManager, QObject *parent = nullptr);
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
  /// connection (Kartend-3mn). Returns an invalid QDateTime when the row is
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
  /// user-driven editors (e.g. custom fields dialog, Kartend-hpln). Returns
  /// true on success; logs the structured error and returns false otherwise.
  bool saveItemMetadata(const ItemMetadataStore::ItemMetadata &metadata);

  /// Loads every artwork row stored for (collectionUuid, path) using the
  /// main-thread connection (Kartend-un3l). Returns an empty list when the
  /// item has no rows or on database failures (errors are logged), so
  /// callers can degrade silently to subdirectory auto-discovery.
  [[nodiscard]] QList<ItemArtworkStore::ItemArtwork> loadItemArtwork(const QString &collectionUuid,
                                                                     const QString &path) const;

  /// Persists a single artwork override row via the main-thread connection
  /// (Kartend-53vk). Used by the per-item manual-link dialog. Returns true
  /// on success; logs the structured error and returns false otherwise.
  bool saveItemArtwork(const ItemArtworkStore::ItemArtwork &artwork);

  /// Removes a single artwork override row via the main-thread connection
  /// (Kartend-53vk). Used by the per-item manual-link dialog when the user
  /// clears an override. Succeeds even when no matching row exists.
  bool removeItemArtwork(const QString &collectionUuid, const QString &path,
                         const QString &artworkType);

  // ──────────────────────────────────────────────────────────────────────────
  // Usage statistics (Kartend-7vi)
  // ──────────────────────────────────────────────────────────────────────────

  /// Loads play_count/last_played/total_play_seconds for a single item via the
  /// main-thread connection. Returns a default-initialized struct on missing
  /// row or DB error (errors logged), so the sidebar can degrade silently.
  [[nodiscard]] UsageStatsStore::ItemUsageStats loadItemUsageStats(const QString &collectionUuid,
                                                                   const QString &path) const;

  /// Increments play_count and stamps last_played for the given item.
  /// Best-effort: failures are logged; the launch path does not block on this.
  void recordItemLaunch(const QString &collectionUuid, const QString &path);

  /// Adds `seconds` to the item's cumulative total_play_seconds. Called when
  /// runtime detection (Kartend-qxv) reports a tracked process exit. Negative
  /// or zero durations are dropped.
  void recordItemPlaySession(const QString &collectionUuid, const QString &path, qint64 seconds);

  /// Whole-library aggregate counters used by the Statistics dialog header.
  [[nodiscard]] UsageStatsStore::AggregateStats loadAggregateUsageStats() const;

  /// Top items by play_count (descending). `limit` is clamped server-side to
  /// [1, 1000] so callers can pass user-driven values without sanitizing.
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow> loadTopPlayedItems(int limit) const;

  /// Most recently launched items (descending last_played). `limit` clamped.
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow> loadRecentlyPlayedItems(int limit) const;

  /// Per-collection breakdown keyed on collection_uuid. Includes uuids whose
  /// CollectionConfig may have been deleted, so totals match the aggregate.
  [[nodiscard]] QHash<QString, UsageStatsStore::CollectionUsage> loadUsageByCollection() const;

  /// Resets every usage column to zero/NULL across the entire library.
  /// Returns true on success.
  bool resetAllUsageStats();

  // ──────────────────────────────────────────────────────────────────────────
  // Launch history (Kartend-fse)
  // ──────────────────────────────────────────────────────────────────────────

  /// Appends a row to launch_history and trims down to `maxEntries` (when
  /// > 0). Best-effort: failures are logged but never block the launch path.
  /// Pass `name` when the caller already knows the user-visible title;
  /// HistoryStore falls back to the path's basename otherwise.
  void recordHistoryEntry(const QString &collectionUuid, const QString &path, const QString &name,
                          int maxEntries);

  /// Most recent history entries first. `limit` is clamped to [1, 100000].
  [[nodiscard]] QList<HistoryStore::HistoryEntry> loadRecentHistory(int limit) const;

  /// Total number of rows in launch_history. Used by the dialog header.
  [[nodiscard]] qint64 historyEntryCount() const;

  /// Wipes the launch_history table. Returns true on success.
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

  // Emitted after cached counts have been recomputed and persisted.
  void cachedCountsUpdated();

  /// Emitted during collection scanning to report progress.
  /// @param current The 1-based index of the collection being scanned
  /// @param total The total number of collections to scan
  /// @param collectionName The name of the collection being scanned
  void scanProgress(int current, int total, const QString &collectionName);

  /// Emitted when a long-running scan is starting
  void scanStarting(const QString &collectionName, int estimatedItems);

  /// Emitted periodically during scan with items processed so far
  void scanItemsProgress(int itemsProcessed, int totalItems);

  /// Emitted when a collection rescan has been applied to the database.
  /// Consumers can refresh counts without blocking the UI.
  void collectionScanCompleted(const QString &collectionUuid);

  /// Forwarded from QueryManager; carries per-scan item totals so callers can
  /// display an "X of Y items added" summary after a scan completes.
  void collectionScanSummary(const QString &collectionUuid, int itemsScanned, int itemsApplied,
                             bool success);

  /// Emitted when collection cache has been invalidated (async operation
  /// complete)
  void cacheInvalidated(const QString &collectionUuid);

  // Internal signals to trigger worker
  void requestLoadAllCollections(const QList<CollectionConfig> &allCollections);
  void requestLoadItems(const CollectionContext &context,
                        const QList<CollectionConfig> &allCollections);
  void requestLoadItemsWithSubcollections(const CollectionContext &context,
                                          const QList<CollectionConfig> &allCollections);
  void requestUpdateCachedCounts(quint64 generation, const QStringList &collectionUuids);
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
  void onWorkerCachedCountsComputed(quint64 generation, qint64 globalCount,
                                    const QHash<QString, qint64> &directCountsByUuid);
  void dispatchCachedCountsUpdate();

private:
  class QueryManager *m_worker;
  class QueryManager *m_scanWorker = nullptr;
  SessionManager *m_sessionManager;
  class QThread *m_workerThread;
  class QThread *m_scanThread = nullptr;

  bool needsRescan(int collectionIndex, const CollectionConfig &collection);
  static QStringList scanMediaDirectory(const CollectionConfig &collection,
                                        QHash<QString, QDateTime> &timestamps);
  QStringList loadItemsFromDatabaseByUuid(const QString &collectionUuid);
  QStringList loadOrScanCollection(int collectionIndex, const CollectionConfig &collection,
                                   QHash<QString, QDateTime> &timestamps);
  void saveItemsToDatabase(int collectionIndex, const QStringList &filePaths,
                           const QHash<QString, QDateTime> &timestamps,
                           const CollectionConfig &collection);
  static int getCharacterSortPriority(const QString &text);
  static void sortFiles(QStringList &allFilePaths);
  [[nodiscard]] qint64 countCollectionByUuid(const QString &collectionUuid) const;
  [[nodiscard]] qint64 countGlobal(const QList<CollectionConfig> &allCollections);
  void clearCollectionFromDatabaseByUuid(const QString &collectionUuid);

  QSqlDatabase m_db;
  QString m_connectionName;

  // Debounced async cached-count update state.
  QTimer *m_cachedCountsUpdateTimer = nullptr;
  quint64 m_cachedCountsGeneration = 0;
  quint64 m_inFlightCachedCountsGeneration = 0;
  QList<CollectionConfig> m_pendingCountsCollections;
  QStringList m_pendingCountsUuids;
  QList<CollectionConfig> m_inFlightCountsCollections;
  QStringList m_inFlightCountsUuids;

  mutable QMutex m_dataMutex; // Protects m_fileToArtworkDir,
                              // m_fileToCollectionIndex, m_fileToMediaDir
  QHash<QString, QString> m_fileToArtworkDir;
  QHash<QString, int> m_fileToCollectionIndex;
  QHash<QString, QString> m_fileToMediaDir; // Maps file paths to their media directories

  // Cache for resolving relative entries (e.g. "subdir/game.rom" or "game.rom")
  // to a known absolute path without repeatedly scanning the full fileNames
  // map. Built from the latest onWorkerItemsLoaded() payload.
  QHash<QString, QString> m_relativeToFullPath;

  static void
  appendFileMapsAndListCanonical(int collectionIndex, const CollectionConfig &expandedCollection,
                                 const QString &mappingArtworkDir, const QStringList &filePaths,
                                 QStringList &allFilePaths, QHash<QString, QString> &allFileNames,
                                 QHash<QString, QString> &fileToArtworkDir,
                                 QHash<QString, QString> &fileToMediaDir,
                                 QHash<QString, int> &fileToCollectionIndex, bool dedup);
};

#endif