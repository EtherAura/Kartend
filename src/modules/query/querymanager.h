#ifndef QUERYMANAGER_H
#define QUERYMANAGER_H

#include <QObject>
#include <QCache>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QHash>
#include <QDateTime>
#include <QThreadPool>
#include <atomic>
#include <memory>
#include "collectionutils.h"
#include "errorutils.h"

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
  void loadItems(const CollectionContext &context);
  void loadItemsWithSubcollections(const CollectionContext &context,
                                   const QList<CollectionConfig> &allCollections);
  void updateCachedCounts(quint64 generation, const QStringList &collectionUuids);
  void fetchItemCount(const CollectionContext &context, const QList<CollectionConfig> &allCollections, const QString &filter);
  void fetchItemsRange(const CollectionContext &context, const QList<CollectionConfig> &allCollections, int offset, int limit, const QString &filter);

  // Scans the current collection (and descendants when showAllSubcollectionItems is set)
  // if a rescan is needed, without blocking query operations in another worker.
  void ensureScannedForContext(const CollectionContext &context,
                              const QList<CollectionConfig> &allCollections);
  
  /// Invalidates collection cache on worker thread (async)
  void invalidateCollectionCache(const QString &collectionUuid);

signals:
  void itemsLoaded(const QStringList &filePaths,
                   const QHash<QString, QString> &fileNames,
                   const QHash<QString, QString> &fileToArtworkDir,
                   const QHash<QString, QString> &fileToMediaDir,
                   const QHash<QString, int> &fileToCollectionIndex);
  void itemCountLoaded(int count);
  void itemsRangeLoaded(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames);
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
  
  /// Emitted when collection cache has been invalidated
  void cacheInvalidated(const QString &collectionUuid);

public:
  /// Request cancellation of any in-progress scan (thread-safe)
  void requestCancelScan();
  
  /// Check if scan cancellation was requested (thread-safe)
  [[nodiscard]] bool isScanCancelled() const;
  
  /// Reset cancellation flag (call before starting new scan)
  void resetScanCancellation();

private:
  // Per-scan cancellation token. Reset by swapping in a new token.
  // This avoids old scan tasks resuming if cancellation is reset while
  // worker tasks are still in-flight.
  std::shared_ptr<std::atomic_bool> m_scanCancellationToken;
  QThreadPool m_scanThreadPool;
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
  // Returns true if database is open (either already was or reconnection succeeded)
  [[nodiscard]] bool ensureDatabaseConnection();

  // Detect optional search acceleration features (FTS, etc.).
  void refreshSearchCapabilities();
  bool m_itemsFtsAvailable = false;
  bool m_itemsFtsReady = true;

  [[nodiscard]] bool isItemsFtsReadyFromDb();

  void ensureCollectionScanned(int collectionIndex, const CollectionConfig &collection);
  void scanAndSaveItemsToDatabase(int collectionIndex, const CollectionConfig &collection);
  bool needsRescan(int collectionIndex, const CollectionConfig &collection);
  QStringList scanMediaDirectory(const CollectionConfig &collection,
                                 QHash<QString, QDateTime> &timestamps,
                                 QString *dirSignatureOut);
  QStringList loadItemsFromDatabaseByUuid(const QString &collectionUuid);
  QStringList loadOrScanCollection(int collectionIndex,
                                   const CollectionConfig &collection,
                                   QHash<QString, QDateTime> &timestamps);
  void saveItemsToDatabase(int collectionIndex, const QStringList &filePaths,
                           const QHash<QString, QDateTime> &timestamps,
                           const CollectionConfig &collection,
                           const QString &dirSignature);

  [[nodiscard]] bool prepareCollectionForItemsInsert(const CollectionConfig &collection,
                                                     const QString &uuid,
                                                     const QString &extSignature,
                                                     int &legacyIdOut);
  void insertItemsBatch(int legacyId, const QString &uuid,
                        const QStringList &paths,
                        const QHash<QString, QDateTime> &timestamps);

  [[nodiscard]] bool ensureScannedItemsTempTable();
  void clearScannedItemsTempTable();
  void insertScannedItemsBatch(const QStringList &paths,
                               const QHash<QString, QDateTime> &timestamps);
  [[nodiscard]] bool applyScannedItemsToDatabase(int legacyId,
                                                 const QString &collectionUuid);
  [[nodiscard]] bool deleteMissingItemsByUuidUsingScannedItems(const QString &collectionUuid);
  
  [[nodiscard]] qint64 countCollectionByUuid(const QString &collectionUuid);
  [[nodiscard]] qint64 countGlobal(const QList<CollectionConfig> &allCollections);
  [[nodiscard]] qint64 countCollectionRecursive(int collectionIndex,
                                  const QList<CollectionConfig> &allCollections);
  void clearCollectionFromDatabaseByUuid(const QString &collectionUuid);

  // Helper struct for UUID-to-directory mappings
  struct CollectionDirMaps {
    QHash<QString, QString> uuidToMediaDir;
    QHash<QString, QString> uuidToArtworkDir;
  };

  // Collects UUIDs for a collection and its descendants if showAllSubcollectionItems is set
  [[nodiscard]] QStringList collectCollectionUuids(const CollectionContext &ctx,
                                     const QList<CollectionConfig> &allCollections);

  // Builds UUID-to-directory mappings for path resolution
  [[nodiscard]] CollectionDirMaps buildDirectoryMaps(const CollectionContext &ctx,
                                       const QList<CollectionConfig> &allCollections);

  // Builds SQL IN clause with placeholders for the given UUID count
  [[nodiscard]] static QString buildUuidInClause(int uuidCount);

  static void appendFileMapsAndListCanonical(
      int collectionIndex, const CollectionConfig &expandedCollection,
      const QString &mappingArtworkDir, const QStringList &filePaths,
      QStringList &allFilePaths, QHash<QString, QString> &allFileNames,
      QHash<QString, QString> &fileToArtworkDir,
      QHash<QString, QString> &fileToMediaDir,
      QHash<QString, int> &fileToCollectionIndex, bool dedup,
      QSet<QString> *seenCanonicalPaths = nullptr,
      QHash<QString, QString> *canonicalPathCache = nullptr);
  
  static void sortFiles(QStringList &allFilePaths, SortMode mode = SortMode::NameAscending);
  static int getCharacterSortPriority(const QString &text);
};

#endif // QUERYMANAGER_H
