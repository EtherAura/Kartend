#ifndef QUERYMANAGER_H
#define QUERYMANAGER_H

#include "collectionutils.h"
#include "errorutils.h"
#include <atomic>
#include <memory>
#include <QCache>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QThreadPool>

class SessionManager;

/**
 * @brief Worker thread database query executor.
 *
 * Threading Model:
 * - Lives on worker thread: moveToThread() in DatabaseManager constructor
 * - All slots are invoked via Qt::QueuedConnection from main thread
 * - All signals are automatically queued back to main thread
 *
 * Thread-safe by design:
 * - Owns its own QSqlDatabase connection (m_db)
 * - Uses thread-local prepared statement cache
 * - No direct access from main thread - only via signals/slots
 *
 * NEVER call methods directly from main thread - use DatabaseManager's API.
 */
class QueryManager : public QObject {
  Q_OBJECT
public:
  explicit QueryManager(SessionManager *sessionManager,
                        const QString &connectionName = QStringLiteral("kartend_worker"),
                        QObject *parent = nullptr);
  ~QueryManager() override;

public slots:
  void initDatabase();
  void ensureItemsFtsReady();
  void loadAllCollections(const QList<CollectionConfig> &allCollections);
  void loadItems(const CollectionContext &context, const QList<CollectionConfig> &allCollections);
  void loadItemsWithSubcollections(const CollectionContext &context,
                                   const QList<CollectionConfig> &allCollections);
  void updateCachedCounts(quint64 generation, const QStringList &collectionUuids);
  void fetchItemCount(const CollectionContext &context,
                      const QList<CollectionConfig> &allCollections, const QString &filter);
  void fetchItemCountWithToken(const CollectionContext &context,
                               const QList<CollectionConfig> &allCollections, const QString &filter,
                               int requestToken);
  void fetchItemsRange(const CollectionContext &context,
                       const QList<CollectionConfig> &allCollections, int offset, int limit,
                       const QString &filter);

  /// Finds the 0-based visual index of a specific file path in the current
  /// sorted order. Used to restore selection after sort mode changes.
  void fetchVisualIndexForPath(const CollectionContext &context,
                               const QList<CollectionConfig> &allCollections,
                               const QString &filePath);

  // Scans the current collection (and descendants when
  // showAllSubcollectionItems is set) if a rescan is needed, without blocking
  // query operations in another worker.
  void ensureScannedForContext(const CollectionContext &context,
                               const QList<CollectionConfig> &allCollections);

  /// Invalidates collection cache on worker thread (async)
  void invalidateCollectionCache(const QString &collectionUuid);

signals:
  void itemsLoaded(const QStringList &filePaths, const QHash<QString, QString> &fileNames,
                   const QHash<QString, QString> &fileToArtworkDir,
                   const QHash<QString, QString> &fileToMediaDir,
                   const QHash<QString, int> &fileToCollectionIndex);
  void itemCountLoaded(int count);
  void itemCountLoadedWithToken(int count, int requestToken);
  void itemsRangeLoaded(int offset, const QStringList &filePaths,
                        const QHash<QString, QString> &fileNames,
                        const QHash<QString, QString> &fileToArtworkDir,
                        const QHash<QString, QString> &fileToMediaDir,
                        const QHash<QString, int> &fileToCollectionIndex);
  /// Emitted when the visual index for a specific file path is found.
  /// Index is -1 if the file was not found in the collection.
  void visualIndexForPathLoaded(int visualIndex, const QString &filePath);
  void errorOccurred(const ErrorUtils::ErrorContext &error);
  void cachedCountsComputed(quint64 generation, qint64 globalCount,
                            const QHash<QString, qint64> &directCountsByUuid);

  /// Emitted during loadAllCollections to report scan progress.
  /// @param current The 1-based index of the collection being scanned
  /// @param total The total number of collections to scan
  /// @param collectionName The name of the collection being scanned
  void scanProgress(int current, int total, const QString &collectionName);

  /// Emitted when a long-running scan is starting (allows UI to show overlay)
  void scanStarting(const QString &collectionName, int estimatedItems);

  /// Emitted periodically during scan with items processed so far
  void scanItemsProgress(int itemsProcessed, int totalItems);

  /// Emitted after a collection rescan has been applied to the database.
  /// Allows the UI to refresh counts without blocking on the scan.
  void collectionScanCompleted(const QString &collectionUuid);

  /// Emitted alongside collectionScanCompleted with per-scan item totals.
  /// Used by the settings dialog to display an "X of Y items added" summary
  /// after a newly-added collection finishes its first scan.
  /// @param itemsScanned  Files discovered on disk that matched the collection
  ///                       extension filter (i.e. the staged count).
  /// @param itemsApplied  Items successfully upserted into the items table.
  /// @param success       True if the scan committed cleanly (no
  ///                       errors/cancellation).
  void collectionScanSummary(const QString &collectionUuid, int itemsScanned, int itemsApplied,
                             bool success);

  /// Emitted when collection cache has been invalidated
  void cacheInvalidated(const QString &collectionUuid);

public:
  /// Request cancellation of any in-progress scan (thread-safe)
  void requestCancelScan();

  /// Check if scan cancellation was requested (thread-safe)
  [[nodiscard]] bool isScanCancelled() const;

  /// Reset cancellation flag (call before starting new scan)
  void resetScanCancellation();

  /// Force connection to see latest WAL commits from other connections
  void refreshWalView();

private:
  [[nodiscard]] int fetchItemCountImpl(const CollectionContext &context,
                                       const QList<CollectionConfig> &allCollections,
                                       const QString &filter);

  // Per-scan cancellation token. Reset by swapping in a new token.
  // This avoids old scan tasks resuming if cancellation is reset while
  // worker tasks are still in-flight.
  std::shared_ptr<std::atomic_bool> m_scanCancellationToken;
  // Raw pointer so we can abandon on shutdown without waiting.
  QThreadPool *m_scanThreadPool = nullptr;
  SessionManager *m_sessionManager;
  QSqlDatabase m_db;
  QString m_connectionName;

  // Prepared statement cache - maps SQL strings to prepared queries
  // Reduces overhead by reusing compiled statements
  // Limited to MAX_STATEMENT_CACHE_SIZE entries with LRU eviction
  static constexpr int MAX_STATEMENT_CACHE_SIZE = 32;
  QCache<QString, QSqlQuery> m_statementCache;

  // Gets or creates a prepared statement for the given SQL
  [[nodiscard]] QSqlQuery &getPreparedStatement(const QString &sql);

  // Clears the statement cache (called when database is reopened)
  void clearStatementCache();

  // Attempts to reconnect to the database if connection was lost
  // Returns true if database is open (either already was or reconnection
  // succeeded)
  [[nodiscard]] bool ensureDatabaseConnection();

  // Full-fallback availability check used by every public slot.
  // Calls ensureDatabaseConnection(), then initDatabase() as a last resort.
  // If the database is still not open, emits a critical errorOccurred so the
  // main thread is notified instead of the slot returning silently.
  // Returns true only if m_db.isOpen() is true after all attempts.
  [[nodiscard]] bool ensureDatabaseAvailable(const char *callerContext);

  // Detect optional search acceleration features (FTS, etc.).
  void refreshSearchCapabilities();
  bool m_itemsFtsAvailable = false;
  bool m_itemsFtsReady = true;

  [[nodiscard]] bool isItemsFtsReadyFromDb();

  [[nodiscard]] bool ensureCollectionScanned(int collectionIndex,
                                             const CollectionConfig &collection);
  [[nodiscard]] bool scanAndSaveItemsToDatabase(int collectionIndex,
                                                 const CollectionConfig &collection,
                                                 int *outItemsScanned = nullptr,
                                                 int *outItemsApplied = nullptr);

  // Phase 1: Walk the filesystem (flat or recursive) and stream discovered
  // files into the scanned_items temp table. Returns the number of files
  // staged and the computed directory signature. On cancellation the temp
  // table may be partially populated; the caller decides whether to proceed.
  [[nodiscard]] bool stageFilesystemScan(const CollectionConfig &collection,
                                         const QStringList &nameFilters, int &itemsStaged,
                                         QString &dirSignatureOut);

  // Phase 2: Upsert rows from the scanned_items temp table into the
  // persistent items table, delete items no longer on disk, and update
  // collection metadata. Must be called inside a valid staging window
  // (i.e. after stageFilesystemScan succeeded and before the temp table is
  // cleared). Returns the number of items applied.
  [[nodiscard]] bool commitStagedScanResults(const CollectionConfig &collection,
                                             const QString &uuid, const QString &extSignature,
                                             const QString &dirSignature, int &itemsApplied);

  bool needsRescan(int collectionIndex, const CollectionConfig &collection);
  QStringList scanMediaDirectory(const CollectionConfig &collection,
                                 QHash<QString, QDateTime> &timestamps, QString *dirSignatureOut);
  QStringList loadItemsFromDatabaseByUuid(const QString &collectionUuid);
  QStringList loadOrScanCollection(int collectionIndex, const CollectionConfig &collection,
                                   QHash<QString, QDateTime> &timestamps);
  void saveItemsToDatabase(int collectionIndex, const QStringList &filePaths,
                           const QHash<QString, QDateTime> &timestamps,
                           const CollectionConfig &collection, const QString &dirSignature);

  [[nodiscard]] bool prepareCollectionForItemsInsert(const CollectionConfig &collection,
                                                     const QString &uuid,
                                                     const QString &extSignature, int &legacyIdOut);
  void insertItemsBatch(int legacyId, const QString &uuid, const QStringList &paths,
                        const QHash<QString, QDateTime> &timestamps);

  [[nodiscard]] bool ensureScannedItemsTempTable();
  void clearScannedItemsTempTable();
  void insertScannedItemsBatch(const QStringList &paths,
                               const QHash<QString, QDateTime> &timestamps);
  [[nodiscard]] bool applyScannedItemsToDatabase(int legacyId, const QString &collectionUuid);
  [[nodiscard]] bool deleteMissingItemsByUuidUsingScannedItems(const QString &collectionUuid);

  // Query UUID temp table helpers - used when UUID count exceeds SQLite
  // variable limit SQLite has a default limit of 999 bind variables; we use a
  // temp table when exceeding 500
  static constexpr int MAX_UUIDS_FOR_IN_CLAUSE = 500;
  [[nodiscard]] bool ensureQueryUuidsTempTable();
  void clearQueryUuidsTempTable();
  [[nodiscard]] bool populateQueryUuidsTempTable(const QStringList &uuids);
  // Returns SQL fragment: either "IN (?,...)" for small lists or "IN (SELECT
  // uuid FROM query_uuids)" for large
  [[nodiscard]] QString buildUuidFilterClause(const QStringList &uuids, bool &useTempTable);

  // Cache to avoid repopulating temp table when UUIDs haven't changed
  // Stores a hash of the UUID list to detect changes
  QByteArray m_cachedQueryUuidsHash;
  [[nodiscard]] static QByteArray computeUuidListHash(const QStringList &uuids);
  [[nodiscard]] bool ensureQueryUuidsPopulated(const QStringList &uuids);

  // ───────────────────────────────────────────────────────────────────────────
  // Precomputed sorted order for O(1) range lookups on large collections
  // ───────────────────────────────────────────────────────────────────────────
  // When item count exceeds PRECOMPUTE_SORT_THRESHOLD, we create a temp table
  // with (position, path, uuid) that allows instant range queries via:
  //   SELECT path, uuid FROM sorted_items_cache WHERE position BETWEEN ? AND ?
  // This avoids expensive ORDER BY + OFFSET for every scroll position.
  [[nodiscard]] bool ensureSortedItemsCacheTable();
  void clearSortedItemsCache();
  [[nodiscard]] bool populateSortedItemsCache(const QStringList &uuids, const QString &filter,
                                              SortMode sortMode);
  [[nodiscard]] bool hasSortedItemsCache() const { return m_sortedItemsCacheValid; }

  // Cache validity tracking
  bool m_sortedItemsCacheValid = false;
  bool m_sortCacheBuildPending = false; // True when deferred build is scheduled
  QByteArray m_sortedItemsCacheHash;    // Hash of (uuids + filter + sortMode) to
                                        // detect changes
  QStringList m_pendingCacheUuids;      // Stored for deferred build
  QString m_pendingCacheFilter;         // Stored for deferred build
  SortMode m_pendingCacheSortMode = SortMode::NameAscending; // Stored for deferred build
  [[nodiscard]] static QByteArray computeSortCacheHash(const QStringList &uuids,
                                                       const QString &filter, SortMode sortMode);

  // Deferred cache build - runs after returning slow-path result to avoid
  // blocking
  void scheduleDeferredCacheBuild(const QStringList &uuids, const QString &filter,
                                  SortMode sortMode);
  void performDeferredCacheBuild();

  [[nodiscard]] qint64 countCollectionByUuid(const QString &collectionUuid);
  [[nodiscard]] qint64 countGlobal(const QList<CollectionConfig> &allCollections);
  [[nodiscard]] qint64 countCollectionRecursive(int collectionIndex,
                                                const QList<CollectionConfig> &allCollections);
  void clearCollectionFromDatabaseByUuid(const QString &collectionUuid);

  // Helper struct for UUID-to-directory mappings
  struct CollectionDirMaps {
    QHash<QString, QString> uuidToMediaDir;
    QHash<QString, QString> uuidToArtworkDir;
    QHash<QString, int> uuidToCollectionIndex;
  };

  // Collects UUIDs for a collection and its descendants if
  // showAllSubcollectionItems is set
  [[nodiscard]] QStringList collectCollectionUuids(const CollectionContext &ctx,
                                                   const QList<CollectionConfig> &allCollections);

  // Builds UUID-to-directory mappings for path resolution
  [[nodiscard]] CollectionDirMaps buildDirectoryMaps(const CollectionContext &ctx,
                                                     const QList<CollectionConfig> &allCollections);

  // Builds SQL IN clause with placeholders for the given UUID count
  [[nodiscard]] static QString buildUuidInClause(int uuidCount);

  static void appendFileMapsAndListCanonical(
      int collectionIndex, const CollectionConfig &expandedCollection,
      const QString &mappingArtworkDir, const QStringList &filePaths, QStringList &allFilePaths,
      QHash<QString, QString> &allFileNames, QHash<QString, QString> &fileToArtworkDir,
      QHash<QString, QString> &fileToMediaDir, QHash<QString, int> &fileToCollectionIndex,
      bool dedup, QSet<QString> *seenCanonicalPaths = nullptr,
      QHash<QString, QString> *canonicalPathCache = nullptr);

  static void sortFiles(QStringList &allFilePaths, SortMode mode = SortMode::NameAscending);
  static int getCharacterSortPriority(const QString &text);
};

#endif // QUERYMANAGER_H
