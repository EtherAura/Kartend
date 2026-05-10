#ifndef CACHEMANAGER_H
#define CACHEMANAGER_H

#include <memory>
#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QTimer>

#include "cachediskstorage.h"

// Statistics for monitoring cache performance
struct CacheMetrics {
  qint64 memoryHits = 0;    // Found in QCache (fastest)
  qint64 diskHits = 0;      // Loaded from disk cache
  qint64 misses = 0;        // Not cached anywhere
  qint64 inserts = 0;       // New items cached
  qint64 evictions = 0;     // Items evicted by LRU
  qint64 invalidations = 0; // Stale items removed

  [[nodiscard]] double memoryHitRate() const {
    qint64 total = memoryHits + diskHits + misses;
    return total > 0 ? static_cast<double>(memoryHits) / total : 0.0;
  }

  [[nodiscard]] double totalHitRate() const {
    qint64 total = memoryHits + diskHits + misses;
    return total > 0 ? static_cast<double>(memoryHits + diskHits) / total : 0.0;
  }

  void reset() { memoryHits = diskHits = misses = inserts = evictions = invalidations = 0; }
};

class CacheManager {
public:
  CacheManager();
  ~CacheManager();

  // Non-copyable, non-movable (singleton-like usage)
  CacheManager(const CacheManager &) = delete;
  CacheManager &operator=(const CacheManager &) = delete;
  CacheManager(CacheManager &&) = delete;
  CacheManager &operator=(CacheManager &&) = delete;
  void initialize();
  void saveToDisk();
  void saveToDiskForShutdown();
  void scheduleSaveToDisk(int delayMs = -1);

  // Shutdown-safe persistence helpers
  // These allow ApplicationManager to snapshot state while the CacheManager is
  // still alive and then write it out asynchronously without holding a raw
  // pointer to this instance.
  [[nodiscard]] QHash<QString, qint64> snapshotTimestampsForShutdown() const;
  static void saveTimestampsSnapshotToDiskForShutdown(const QHash<QString, qint64> &timestampsCopy);

  // Cancels pending I/O operations and waits for in-flight tasks to complete.
  // Call before shutdown to ensure clean state for final save.
  void cancelPendingIo();

  [[nodiscard]] QPixmap getArtwork(const QString &artworkPath);
  // Memory-only lookup: never performs disk I/O or creates files.
  [[nodiscard]] QPixmap getArtworkFromMemoryOnly(const QString &artworkPath);

  // Worker-thread friendly disk cache read.
  // Returns the cached image (PNG) as QImage, without creating any QPixmap.
  [[nodiscard]] QImage tryLoadArtworkImageFromDiskCache(const QString &artworkPath);

  void cacheArtwork(const QString &artworkPath, const QPixmap &pixmap);

  // Inserts into in-memory cache only; does not mark dirty for disk
  // persistence.
  void cacheArtworkInMemoryOnly(const QString &artworkPath, const QPixmap &pixmap);

  void clearCollectionCache(int collectionIndex);
  [[nodiscard]] static qint64 getCacheSize();
  void releaseGuiResources();

  // Cache metrics access
  [[nodiscard]] CacheMetrics metrics() const;
  void resetMetrics();
  void logMetrics() const;

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
  QTimer *m_debouncedSaveTimer = nullptr;
  bool m_metadataDirty = false;
  qint64 m_firstDirtyAtMs = 0;
};

#endif // CACHEMANAGER_H
