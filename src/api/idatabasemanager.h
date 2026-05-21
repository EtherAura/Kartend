#ifndef IDATABASEMANAGER_H
#define IDATABASEMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include "collectionutils.h"
#include "errorutils.h"
#include "historystore.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "usagestatsstore.h"

/**
 * @brief Abstract interface to the database layer.
 *
 * Exposes the full main-thread API plus the externally-observable signal
 * contract of DatabaseManager so that callers can be wired against either the
 * real (SQLite-backed) implementation or a test double.
 *
 * Thread-safe accessors (resolveFilePath, resolveRelativeFilePath,
 * getCollectionIndexForFile, findArtworkDirectoryForFile) keep the same
 * any-thread guarantee as the concrete class — implementers must preserve it.
 */
class IDatabaseManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(IDatabaseManager)
public:
  using QObject::QObject;
  ~IDatabaseManager() override = default;

  virtual void initDatabase() = 0;

  [[nodiscard]] virtual QString connectionName() const = 0;

  virtual void loadAllCollections(const QList<CollectionConfig> &allCollections) = 0;
  virtual void loadItems(const CollectionContext &context,
                         const QList<CollectionConfig> &allCollections) = 0;
  virtual void loadItemsWithSubcollections(const CollectionContext &context,
                                           const QList<CollectionConfig> &allCollections) = 0;
  virtual void updateCachedCounts(const QList<CollectionConfig> &allCollections) = 0;

  virtual void fetchItemCount(const CollectionContext &context,
                              const QList<CollectionConfig> &allCollections,
                              const QString &filter = QString(), int requestToken = 0) = 0;
  virtual void fetchItemsRange(const CollectionContext &context,
                               const QList<CollectionConfig> &allCollections, int offset, int limit,
                               const QString &filter = QString()) = 0;

  virtual void fetchVisualIndexForPath(const CollectionContext &context,
                                       const QList<CollectionConfig> &allCollections,
                                       const QString &filePath) = 0;

  virtual void invalidateCollectionCache(const QString &collectionUuid) = 0;

  [[nodiscard]] virtual int getCollectionIndexForFile(const QString &filePath) const = 0;
  [[nodiscard]] virtual QString findArtworkDirectoryForFile(const QString &filePath) const = 0;

  [[nodiscard]] virtual QString resolveFilePath(const QString &rawEntry,
                                                const CollectionContext &context) const = 0;
  [[nodiscard]] virtual QString
  resolveRelativeFilePath(const QString &rawFileName,
                          const QHash<QString, QString> &fileNames) const = 0;
  [[nodiscard]] virtual qint64
  countCollectionRecursive(int collectionIndex,
                           const QList<CollectionConfig> &allCollections) const = 0;

  [[nodiscard]] virtual QDateTime
  loadCollectionLastScanned(const QString &collectionUuid) const = 0;

  [[nodiscard]] virtual ItemMetadataStore::ItemMetadata
  loadItemMetadata(const QString &collectionUuid, const QString &path) const = 0;
  /// Batch counterpart to loadItemMetadata. Returns a (path -> metadata)
  /// hash covering exactly @p paths. Paths without a row in item_metadata
  /// map to an empty-but-keyed ItemMetadata (mirrors single-load behaviour).
  /// Implementations may chunk internally to respect SQLite's
  /// SQLITE_LIMIT_VARIABLE_NUMBER. The default implementation loops over
  /// loadItemMetadata for correctness on mocks; the production override
  /// in DatabaseManager issues batched WHERE-path-IN queries.
  [[nodiscard]] virtual QHash<QString, ItemMetadataStore::ItemMetadata>
  loadItemMetadataBatch(const QString &collectionUuid, const QStringList &paths) const {
    QHash<QString, ItemMetadataStore::ItemMetadata> out;
    out.reserve(paths.size());
    for (const QString &path : paths) {
      out.insert(path, loadItemMetadata(collectionUuid, path));
    }
    return out;
  }
  virtual bool saveItemMetadata(const ItemMetadataStore::ItemMetadata &metadata) = 0;

  /// Drop the per-item metadata-cache entry for (collectionUuid, path).
  /// Invoked by external writers (e.g. BatchScrapeRunner's worker thread)
  /// that wrote to the SQLite file via their own connection and so
  /// bypassed the cache invalidation that saveItemMetadata /
  /// saveItemArtwork normally do. Main-thread only — the cache itself
  /// is not thread-safe (see ItemMetadataCache header). Implementations
  /// without a cache may no-op.
  virtual void invalidateMetadataCacheItem(const QString &collectionUuid, const QString &path) = 0;

  [[nodiscard]] virtual QList<ItemArtworkStore::ItemArtwork>
  loadItemArtwork(const QString &collectionUuid, const QString &path) const = 0;
  virtual bool saveItemArtwork(const ItemArtworkStore::ItemArtwork &artwork) = 0;
  virtual bool removeItemArtwork(const QString &collectionUuid, const QString &path,
                                 const QString &artworkType) = 0;

  [[nodiscard]] virtual UsageStatsStore::ItemUsageStats
  loadItemUsageStats(const QString &collectionUuid, const QString &path) const = 0;
  virtual void recordItemLaunch(const QString &collectionUuid, const QString &path) = 0;
  virtual void recordItemPlaySession(const QString &collectionUuid, const QString &path,
                                     qint64 seconds) = 0;
  [[nodiscard]] virtual UsageStatsStore::AggregateStats loadAggregateUsageStats() const = 0;
  [[nodiscard]] virtual QList<UsageStatsStore::ItemUsageRow>
  loadTopPlayedItems(int limit) const = 0;
  [[nodiscard]] virtual QList<UsageStatsStore::ItemUsageRow>
  loadRecentlyPlayedItems(int limit) const = 0;
  [[nodiscard]] virtual QList<UsageStatsStore::ItemUsageRow>
  loadNeverPlayedItems(int limit) const = 0;
  [[nodiscard]] virtual qint64 countItemsPlayedSince(const QString &isoCutoffUtc) const = 0;
  [[nodiscard]] virtual QHash<QString, UsageStatsStore::CollectionUsage>
  loadUsageByCollection() const = 0;
  virtual bool resetAllUsageStats() = 0;

  /// Move every `items` / `collections` row from `oldUuid` to
  /// `newUuid` — used when a collection rename (or media-dir change)
  /// recomputes its uuid, so play history survives the rename instead
  /// of being orphaned under the old uuid.
  virtual void migrateCollectionUuid(const QString &oldUuid, const QString &newUuid) = 0;

  /// Delete `items` / `collections` rows that belong to no collection
  /// in `liveCollections` — purges orphans left by past renames /
  /// removals so whole-library counts match the live collections.
  /// The implementation derives each collection's uuid (both the raw
  /// and the path-variable-expanded media-dir forms) so a collection
  /// scanned under either form is never mistaken for an orphan.
  virtual void purgeOrphanCollectionData(const QList<CollectionConfig> &liveCollections) = 0;

  virtual void recordHistoryEntry(const QString &collectionUuid, const QString &path,
                                  const QString &name, int maxEntries) = 0;
  [[nodiscard]] virtual QList<HistoryStore::HistoryEntry> loadRecentHistory(int limit) const = 0;
  [[nodiscard]] virtual qint64 historyEntryCount() const = 0;
  virtual bool clearHistory() = 0;

signals:
  void itemsLoaded(const QStringList &filePaths, const QHash<QString, QString> &fileNames);
  void itemCountLoaded(int count);
  void itemCountLoadedWithToken(int count, int requestToken);
  /// `requestedCollectionIndex` echoes the CollectionContext::currentIndex
  /// the fetch was issued for. itemsRangeLoaded is a shared signal, so a
  /// consumer with several fetches in flight at once (e.g. the Scraper
  /// dialog, which fires one fetchItemsRange per collection when a parent
  /// is cascade-checked) must use it to route each result to the request
  /// that asked for it — otherwise the first result is consumed by every
  /// waiting handler.
  void itemsRangeLoaded(int offset, const QStringList &filePaths,
                        const QHash<QString, QString> &fileNames,
                        const QHash<QString, QString> &fileToArtworkDir,
                        const QHash<QString, QString> &fileToMediaDir,
                        const QHash<QString, int> &fileToCollectionIndex,
                        int requestedCollectionIndex);
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

public slots:
  virtual void cancelScan() = 0;
};

#endif
