#ifndef CACHEMANAGER_H
#define CACHEMANAGER_H

#include <QHash>
#include <QMutex>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QPair>
#include <QList>
#include <QJsonObject>
#include <QCache>
#include <QImage>
#include <QThreadPool>
#include <atomic>

// Statistics for monitoring cache performance
struct CacheMetrics {
  qint64 memoryHits = 0;     // Found in QCache (fastest)
  qint64 diskHits = 0;       // Loaded from disk cache
  qint64 misses = 0;         // Not cached anywhere
  qint64 inserts = 0;        // New items cached
  qint64 evictions = 0;      // Items evicted by LRU
  qint64 invalidations = 0;  // Stale items removed

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
  void initialize();
  void saveToDisk();
  void saveToDiskForShutdown();

  // Shutdown-safe persistence helpers
  // These allow ApplicationManager to snapshot state while the CacheManager is
  // still alive and then write it out asynchronously without holding a raw
  // pointer to this instance.
  [[nodiscard]] QHash<QString, qint64> snapshotTimestampsForShutdown() const;
  static void saveTimestampsSnapshotToDiskForShutdown(
      const QHash<QString, qint64> &timestampsCopy);
  
  [[nodiscard]] QPixmap getArtwork(const QString &artworkPath);
  void cacheArtwork(const QString &artworkPath, const QPixmap &pixmap);
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
  static void writeTimestamps(const QHash<QString, qint64> &timestampsCopy);
  static void flushDirtyArtwork(const QList<QPair<QString, QImage>> &dirtyList);

  mutable QMutex m_mutex;
  QCache<QString, QPixmap> artworkCache;
  QHash<QString, qint64> fileTimestamps;
  QSet<QString> dirtyArtwork;
  CacheMetrics m_metrics;

  // Dedicated sequential pool for disk cache writes.
  // Avoids UI hitches and reduces contention with other QtConcurrent users.
  QThreadPool m_ioThreadPool;

  // Cancellation flag for in-flight/queued I/O tasks (used during shutdown).
  std::atomic_bool m_cancelIo{false};
};

#endif // CACHEMANAGER_H
