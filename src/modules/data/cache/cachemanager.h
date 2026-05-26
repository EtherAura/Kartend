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
  [[nodiscard]] QHash<QString, qint64> snapshotTimestampsForShutdown() const override;
  static void saveTimestampsSnapshotToDiskForShutdown(const QHash<QString, qint64> &timestampsCopy);

  // Cancels pending I/O operations and waits for in-flight tasks to complete.
  // Call before shutdown to ensure clean state for final save.
  void cancelPendingIo() override;

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

  // Cache metrics access
  [[nodiscard]] CacheMetrics metrics() const override;
  void resetMetrics() override;
  void logMetrics() const override;

private:
  mutable QMutex m_mutex;
  QCache<QString, QPixmap> artworkCache;
  QHash<QString, qint64> fileTimestamps;
  QSet<QString> dirtyTimestamps; // Paths whose timestamps changed since last save
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
};

#endif // CACHEMANAGER_H
