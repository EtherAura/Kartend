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

  void clearCollectionCache(int collectionIndex) override;
  [[nodiscard]] static qint64 getCacheSize();
  void releaseGuiResources() override;

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
  QTimer *m_debouncedSaveTimer = nullptr;
  bool m_metadataDirty = false;
  qint64 m_firstDirtyAtMs = 0;
};

#endif // CACHEMANAGER_H
