#ifndef QUERYMANAGER_H
#define QUERYMANAGER_H

#include "collection/collectioncontext.h"
#include "errorutils.h"
#include "itemdetaildata.h"
#include "preparedstatementcache.h"
#include "scanservice.h"
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>

class ISessionManager;

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
  Q_DISABLE_COPY_MOVE(QueryManager)
public:
  explicit QueryManager(ISessionManager *sessionManager,
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

  /// Off-thread loader for the item detail page (Kartend-4p8o). Runs the
  /// metadata / usage / artwork DB reads on this worker's own connection plus
  /// the artwork + video filesystem probes, then emits itemDetailLoaded so the
  /// UI can fill the already-shown overlay without blocking the main thread.
  /// `requestToken` is echoed back unchanged so DetailPageManager can drop a
  /// stale result when the selection has moved on.
  void loadItemDetail(int requestToken, const QString &collectionUuid, const QString &filePath,
                      const QString &artworkDir, const QString &videoDir, const QString &manualDir);

  // Scans the current collection (and descendants when
  // showAllSubcollectionItems is set) if a rescan is needed, without blocking
  // query operations in another worker. Handles connection availability and
  // WAL-view freshness here, then delegates the scan work to m_scanService.
  void ensureScannedForContext(const CollectionContext &context,
                               const QList<CollectionConfig> &allCollections);

  /// Invalidates collection cache on worker thread (async)
  void invalidateCollectionCache(const QString &collectionUuid);

  /// Drops the cached smart-playlist scope key so the next fetch re-evaluates
  /// the filter against current item data. Cheap (in-memory) — wired to launch
  /// / usage-reset events so smart playlists like "Recently launched" refresh
  /// mid-session instead of staying frozen on their first evaluation
  /// (Kartend-s9jw).
  void invalidateSmartPlaylistScope();

signals:
  void itemsLoaded(const QStringList &filePaths, const QHash<QString, QString> &fileNames,
                   const QHash<QString, QString> &fileToArtworkDir,
                   const QHash<QString, QString> &fileToMediaDir,
                   const QHash<QString, int> &fileToCollectionIndex);
  void itemCountLoaded(int count);
  void itemCountLoadedWithToken(int count, int requestToken);
  /// Result of loadItemDetail (Kartend-4p8o). `requestToken` echoes the call.
  void itemDetailLoaded(const ItemDetailData &data, int requestToken);
  /// Trailing `requestedCollectionIndex` echoes CollectionContext::currentIndex
  /// so concurrent fetchItemsRange callers can match a result to its request
  /// — see IDatabaseManager::itemsRangeLoaded.
  void itemsRangeLoaded(int offset, const QStringList &filePaths,
                        const QHash<QString, QString> &fileNames,
                        const QHash<QString, QString> &fileToArtworkDir,
                        const QHash<QString, QString> &fileToMediaDir,
                        const QHash<QString, int> &fileToCollectionIndex,
                        int requestedCollectionIndex);
  /// Emitted when the visual index for a specific file path is found.
  /// Index is -1 if the file was not found in the collection.
  void visualIndexForPathLoaded(int visualIndex, const QString &filePath);
  void errorOccurred(const ErrorUtils::ErrorContext &error);
  void cachedCountsComputed(quint64 generation, qint64 globalCount,
                            const QHash<QString, qint64> &directCountsByUuid);

  /// Emitted during loadAllCollections to report scan progress.
  /// Owned by QueryManager: emitted only by the load loop (loadAllCollections),
  /// never by the scan subsystem.
  /// @param current The 1-based index of the collection being scanned
  /// @param total The total number of collections to scan
  /// @param collectionName The name of the collection being scanned
  void scanProgress(int current, int total, const QString &collectionName);

  // The four scan signals below are forwarded: ScanService declares and emits
  // its own copies; QueryManager's constructor connects each ScanService
  // signal to the matching QueryManager signal so DatabaseManager keeps
  // wiring to QueryManager unchanged.

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
  /// Request cancellation of any in-progress scan (thread-safe).
  /// Delegates to m_scanService.
  void requestCancelScan();

  /// Check if scan cancellation was requested (thread-safe).
  /// Delegates to m_scanService.
  [[nodiscard]] bool isScanCancelled() const;

  /// Reset cancellation flag (call before starting new scan).
  /// Delegates to m_scanService.
  void resetScanCancellation();

  /// Force connection to see latest WAL commits from other connections
  void refreshWalView();

private:
  // Thread-affinity guard. QueryManager owns a QSqlDatabase that Qt's SQL
  // drivers explicitly forbid sharing across threads. Every slot must be
  // invoked on the worker thread that owns this object. Compiles out in
  // release; fail-fast in debug. Matches the pattern in
  // InteractionStateHolder::assertOwnerThread().
  void assertOwnerThread() const {
    Q_ASSERT_X(thread() == QThread::currentThread(), "QueryManager",
               "QueryManager slot invoked from a non-owner thread; SQL "
               "connections are not thread-safe — slots must run on the "
               "worker thread (use Qt::QueuedConnection from main).");
  }

  [[nodiscard]] int fetchItemCountImpl(const CollectionContext &context,
                                       const QList<CollectionConfig> &allCollections,
                                       const QString &filter);

  ISessionManager *m_sessionManager;
  QSqlDatabase m_db;
  QString m_connectionName;

  // Nesting depth for DbTransaction on m_db. Lets a transaction-wrapped helper
  // (e.g. populateQueryUuidsTempTable) join an outer transaction (e.g.
  // populateSortedItemsCache) instead of issuing a second BEGIN that SQLite
  // silently drops — keeping the cache build atomic (Kartend-gv7f). Single
  // worker connection, so a plain int is sufficient (no cross-thread sharing).
  int m_txnDepth = 0;

  // LRU cache of prepared QSqlQuery statements bound to m_db. Reuses
  // compiled statements across slot invocations; reset on every get() so
  // stale bound values can't leak across reuse.
  static constexpr int MAX_STATEMENT_CACHE_SIZE = 32;
  PreparedStatementCache m_statementCache{MAX_STATEMENT_CACHE_SIZE};

  // The collection-rescan subsystem (filesystem walk, scanned_items staging,
  // scan-and-save pipelines, cancellation token). Borrows m_db and
  // m_statementCache by reference — declared AFTER both so those members are
  // already constructed. QueryManager keeps only a thin delegating surface:
  // its scan slots forward here, and its scan signals are re-emitted from
  // this object's matching signals (wired in the constructor).
  ScanService m_scanService{m_db, m_statementCache, this};

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

  QStringList loadItemsFromDatabaseByUuid(const QString &collectionUuid);

  // Load-or-scan hybrid used by the four load slots. Unidirectional bridge
  // into the scan subsystem: when a rescan is needed it routes through
  // m_scanService (needsRescan / scanMediaDirectory / saveItemsToDatabase),
  // otherwise it loads cached rows via loadItemsFromDatabaseByUuid. No
  // ScanService->QueryManager calls, so the load/scan boundary stays
  // untangled.
  QStringList loadOrScanCollection(int collectionIndex, const CollectionConfig &collection,
                                   QHash<QString, QDateTime> &timestamps);

  // The scan subsystem (needsRescan, scanMediaDirectory, the scan-and-save
  // pipelines, prepareCollectionForItemsInsert, the scanned_items staging)
  // was extracted into ScanService (m_scanService) — see scanservice.h.
  // The v13 path-absolutize migration and clearCollectionFromDatabaseByUuid
  // became free functions in querymanagerhelpers.h::QueryManagerInternal.

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
  // Playlist scope temp table
  // ───────────────────────────────────────────────────────────────────────────
  // For playlist-backed virtual collections, the fetch SQL needs to constrain
  // to the exact (collection_uuid, path) pairs stored in playlist_items —
  // this is layered on top of the existing collection_uuid filter via an
  // EXISTS clause. The temp table is repopulated when the playlist id changes
  // or the playlist's contents are mutated (m_cachedPlaylistScopeKey is the
  // invalidation token: hash of playlistId + max(rowid) of its items).
  [[nodiscard]] bool ensurePlaylistScopeTempTable();
  [[nodiscard]] bool populatePlaylistScopeTempTable(const QString &playlistId);
  [[nodiscard]] bool ensurePlaylistScopePopulated(const QString &playlistId);
  [[nodiscard]] QStringList loadPlaylistSourceUuids(const QString &playlistId);
  QString m_cachedPlaylistScopeKey;

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

  // appendFileMapsAndListCanonical() and sortFiles() were pure static
  // members; they are now free functions in the QueryManagerInternal
  // namespace (querymanagerhelpers.h) so they no longer widen this header.
};

#endif // QUERYMANAGER_H
