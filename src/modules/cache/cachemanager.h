#ifndef CACHEMANAGER_H
#define CACHEMANAGER_H

#include <QCache>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <memory>

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

  void reset() {
    memoryHits = diskHits = misses = inserts = evictions = invalidations = 0;
  }
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
  static void saveTimestampsSnapshotToDiskForShutdown(
      const QHash<QString, qint64> &timestampsCopy);

  // Cancels pending I/O operations and waits for in-flight tasks to complete.
  // Call before shutdown to ensure clean state for final save.
  void cancelPendingIo();

  [[nodiscard]] QPixmap getArtwork(const QString &artworkPath);
  // Memory-only lookup: never performs disk I/O or creates files.
  [[nodiscard]] QPixmap getArtworkFromMemoryOnly(const QString &artworkPath);

  // Worker-thread friendly disk cache read.
  // Returns the cached image (PNG) as QImage, without creating any QPixmap.
  [[nodiscard]] QImage
  tryLoadArtworkImageFromDiskCache(const QString &artworkPath);

  void cacheArtwork(const QString &artworkPath, const QPixmap &pixmap);

  // Inserts into in-memory cache only; does not mark dirty for disk
  // persistence.
  void cacheArtworkInMemoryOnly(const QString &artworkPath,
                                const QPixmap &pixmap);

  void clearCollectionCache(int collectionIndex);
  [[nodiscard]] static qint64 getCacheSize();
  void releaseGuiResources();

  // Cache metrics access
  [[nodiscard]] CacheMetrics metrics() const;
  void resetMetrics();
  void logMetrics() const;

private:
  static QString getCacheDirectory();
  static QString getArtworkCachePath(const QString &artworkPath);

  void readTimestamps(const QJsonObject &root);
  static void writeTimestamps(const QHash<QString, qint64> &dirtyTimestamps,
                              const QString &metadataPath);

  mutable QMutex m_mutex;
  QCache<QString, QPixmap> artworkCache;
  QHash<QString, qint64> fileTimestamps;
  QSet<QString>
      dirtyTimestamps; // Paths whose timestamps changed since last save
  QSet<QString> dirtyArtwork;
  CacheMetrics m_metrics;

  // Dedicated sequential pool for disk cache writes.
  // Avoids UI hitches and reduces contention with other QtConcurrent users.
  // Raw pointer so we can abandon it on shutdown without waiting.
  QThreadPool *m_ioThreadPool = nullptr;

  // Cancellation flag for in-flight/queued I/O tasks (used during shutdown).
  // Shared pointer so lambdas can safely check after CacheManager destruction.
  std::shared_ptr<std::atomic_bool> m_cancelIo =
      std::make_shared<std::atomic_bool>(false);

  // Debounced save timer plumbing (keeps frequent cache changes from causing
  // repeated PNG encodes and metadata writes during active scrolling).
  QObject *m_timerContext = nullptr;
  QTimer *m_debouncedSaveTimer = nullptr;
  bool m_metadataDirty = false;
  qint64 m_firstDirtyAtMs = 0;
};

#endif // CACHEMANAGER_H
