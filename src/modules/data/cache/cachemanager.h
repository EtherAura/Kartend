#ifndef CACHEMANAGER_H
#define CACHEMANAGER_H

#include <memory>
#include <QAtomicInteger>
#include <QCache>
#include <QFuture>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>

#include "cachediskstorage.h"
#include "icachemanager.h"

// CacheMetrics now lives in icachemanager.h — the data contract travels
// with the interface.

class CacheManager : public ICacheManager {
public:
  CacheManager();
  ~CacheManager() override;

  // Non-copyable, non-movable (singleton-like usage)
  CacheManager(const CacheManager &) = delete;
  CacheManager &operator=(const CacheManager &) = delete;
  CacheManager(CacheManager &&) = delete;
  CacheManager &operator=(CacheManager &&) = delete;
  void initialize() override;
  void saveToDisk() override;
  void saveToDiskForShutdown() override;
  void scheduleSaveToDisk(int delayMs = -1) override;

  // Shutdown-safe persistence helpers
  // These allow ApplicationManager to snapshot state while the CacheManager is
  // still alive and then write it out asynchronously without holding a raw
  // pointer to this instance.
  [[nodiscard]] CacheTimestampsSnapshot snapshotTimestampsForShutdown() const override;
  static void saveTimestampsSnapshotToDiskForShutdown(const CacheTimestampsSnapshot &snapshot);

  // Cancels pending I/O operations and waits for in-flight tasks to complete.
  // Call before shutdown to ensure clean state for final save.
  void cancelPendingIo() override;

  // Synchronous observability for the cancel contract (Kartend-yjklc): after
  // cancelPendingIo(), isPendingIoCancelled() is true (scheduleSaveToDisk and
  // the queued timer-start both early-return in this state) and the debounced
  // save timer stays inactive. Lets tests assert the "cancelled timer must
  // not fire" negative directly instead of racing the clock with a long wait.
  [[nodiscard]] bool isPendingIoCancelled() const;
  [[nodiscard]] bool isSaveTimerActive() const;

  [[nodiscard]] QPixmap getArtwork(const QString &artworkPath) override;
  // Memory-only lookup: never performs disk I/O or creates files.
  [[nodiscard]] QPixmap getArtworkFromMemoryOnly(const QString &artworkPath) override;

  // Worker-thread friendly disk cache read.
  // Returns the cached image (PNG) as QImage, without creating any QPixmap.
  [[nodiscard]] QImage tryLoadArtworkImageFromDiskCache(const QString &artworkPath) override;

  void cacheArtwork(const QString &artworkPath, const QPixmap &pixmap) override;

  // Inserts into in-memory cache only; does not mark dirty for disk
  // persistence.
  void cacheArtworkInMemoryOnly(const QString &artworkPath, const QPixmap &pixmap) override;

  void clearCollectionCache(const QString &artworkDirectoryPrefix) override;
  /// Approximate total bytes on disk under the artwork cache directory.
  /// Cached internally and refreshed via a background QDirIterator walk —
  /// the call returns the most recent cached value immediately, and
  /// dispatches a re-walk when the cached value is older than ~30 s and
  /// no walk is already in flight. First call returns 0 while the
  /// initial walk runs; subsequent calls return the real total once the
  /// walk completes. Non-blocking on the GUI thread (Kartend-bwcd).
  [[nodiscard]] qint64 getCacheSize() const override;
  void releaseGuiResources() override;

  void setArtworkCacheBudgetMB(int megabytes) override;

  /// Test-only introspection: the QCache::maxCost ceiling (in bytes) the
  /// artwork cache is currently configured for. Exposed solely so tests
  /// can assert that setArtworkCacheBudgetMB(...) pushed the expected
  /// value through and that the 1 MB floor clamps as designed
  /// (Kartend-c7mb). Not part of ICacheManager — production code reads
  /// metrics() instead.
  [[nodiscard]] int artworkCacheMaxCostForTesting() const { return artworkCache.maxCost(); }

  /// Test-only introspection + trigger for the opportunistic bookkeeping
  /// sweep (Kartend-0ldg2). Production runs pruneStaleEntries() from the
  /// background getCacheSize() walk; tests invoke it synchronously and
  /// assert the map sizes directly (same precedent as
  /// artworkCacheMaxCostForTesting).
  [[nodiscard]] int fileTimestampCountForTesting() const;
  [[nodiscard]] int lastRevalidatedCountForTesting() const;
  void pruneStaleEntriesForTesting() { pruneStaleEntries(); }

  /// Test-only: synchronously drain the disk-I/O worker pool so a slot can
  /// assert on the store contents a scheduled async save produced
  /// (Kartend-9lm54). Single-shot like CacheDiskStorage::drainWithBudget —
  /// the pool is gone afterwards — so call it only at the end of a slot.
  [[nodiscard]] bool drainPendingIoForTesting(int budgetMs) {
    return m_diskStorage->drainWithBudget(budgetMs);
  }

  // Cache metrics access
  [[nodiscard]] CacheMetrics metrics() const override;
  void resetMetrics() override;
  void logMetrics() const override;

private:
  /// Opportunistic GC pass run on the background getCacheSize() walk thread
  /// (Kartend-0ldg2): prunes a bounded batch of dead rows from the SQLite
  /// timestamp store, then sweeps the in-memory bookkeeping maps —
  /// fileTimestamps entries that are neither in artworkCache nor backed by
  /// a file on disk, and m_lastRevalidatedMs entries whose pixmap was
  /// evicted. Bounded per pass so a pass never stats more than a few
  /// hundred files; rotating cursors make successive passes cover the whole
  /// keyspace.
  void pruneStaleEntries();

  mutable QMutex m_mutex;
  QCache<QString, QPixmap> artworkCache;
  QHash<QString, qint64> fileTimestamps;
  // Last ms-since-epoch each key's on-disk mtime was revalidated. Bounds the
  // per-hit stat to one per key per kArtworkRevalidateIntervalMs on the scroll
  // hot path (Kartend-qszks).
  QHash<QString, qint64> m_lastRevalidatedMs;
  QSet<QString> dirtyTimestamps; // Paths whose timestamps changed since last save
  // Paths invalidated since the last save whose store rows must be DELETEd —
  // the dirty-row upsert only touches keys still in fileTimestamps, so
  // without this set an invalidated row would survive on disk until the
  // bounded prune pass happened to reach it (Kartend-9lm54).
  QSet<QString> dirtyRemovals;
  QSet<QString> dirtyArtwork;
  CacheMetrics m_metrics;

  /// Disk persistence helper — owns the cache directory layout, the
  /// path-hashing scheme, JSON metadata read/write, and the dedicated
  /// single-thread worker pool that runs the async PNG encodes +
  /// metadata writes. CacheManager keeps the in-memory state and
  /// orchestrates when to flush; the helper handles the actual disk I/O.
  std::unique_ptr<CacheDiskStorage> m_diskStorage;

  // Debounced save timer plumbing (keeps frequent cache changes from causing
  // repeated PNG encodes and metadata writes during active scrolling).
  QObject *m_timerContext = nullptr;
  QPointer<QTimer> m_debouncedSaveTimer;
  // Kartend-gro2: coalesce cross-thread timer-start posts so a flood of
  // scheduleSaveToDisk() calls during initial cache fill produces at most one
  // outstanding QueuedConnection invokeMethod task.
  QAtomicInteger<int> m_savePostInFlight = 0;
  // Kartend-7s2mv: run-once guard so the disk-timestamp load happens exactly
  // once even if initialize() is reached from more than one path. The load is
  // owned by ApplicationManager's background QtConcurrent future; this prevents
  // a second caller from re-reading and re-clearing the timestamp store
  // (formerly a 50MB+ JSON parse, now a single SELECT — Kartend-0ldg2).
  QAtomicInteger<int> m_initStarted = 0;
  bool m_metadataDirty = false;
  qint64 m_firstDirtyAtMs = 0;

  // Cache for getCacheSize() — protects against GUI-thread directory
  // walks every CHECK_INTERVAL artworks (Kartend-bwcd). m_diskCacheSizeMutex
  // serialises reads/writes of the cached value + bookkeeping fields. The
  // walk itself runs via QtConcurrent on the global pool and publishes the
  // result via the mutex. ~CacheManager waits on m_cacheSizeWalkFuture so
  // an in-flight walk can't outlive the owning object.
  mutable QMutex m_diskCacheSizeMutex;
  mutable qint64 m_cachedDiskCacheSize = 0;
  mutable qint64 m_lastDiskWalkMs = 0;
  mutable bool m_diskWalkInFlight = false;
  mutable QFuture<void> m_cacheSizeWalkFuture;
  // Rotating cursors for pruneStaleEntries' bounded batches (guarded by
  // m_diskCacheSizeMutex; only the single-flight walk thread advances them).
  QString m_pruneDbCursor;
  QString m_sweepCursor;
};

#endif // CACHEMANAGER_H
