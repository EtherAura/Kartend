#ifndef ICACHEMANAGER_H
#define ICACHEMANAGER_H

#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QString>

// Statistics for monitoring cache performance.
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

/**
 * @brief Abstract interface to the artwork pixmap cache.
 *
 * Exposes the full CacheManager API so callers wired through
 * ApplicationContext can be backed by either the real (memory + disk)
 * implementation or a test double.
 *
 * Like ISessionManager — and unlike IDatabaseManager / ISettingsManager —
 * this is a plain abstract class, not a QObject: CacheManager exposes no
 * signals or slots, so there is nothing to moc.
 *
 * The static shutdown helper `saveTimestampsSnapshotToDiskForShutdown` is
 * intentionally not part of this interface — it is instance-free and stays
 * on the concrete class.
 */
class ICacheManager {
public:
  virtual ~ICacheManager() = default;

  virtual void initialize() = 0;
  virtual void saveToDisk() = 0;
  virtual void saveToDiskForShutdown() = 0;
  virtual void scheduleSaveToDisk(int delayMs = -1) = 0;

  [[nodiscard]] virtual QHash<QString, qint64> snapshotTimestampsForShutdown() const = 0;

  virtual void cancelPendingIo() = 0;

  [[nodiscard]] virtual QPixmap getArtwork(const QString &artworkPath) = 0;
  [[nodiscard]] virtual QPixmap getArtworkFromMemoryOnly(const QString &artworkPath) = 0;
  [[nodiscard]] virtual QImage tryLoadArtworkImageFromDiskCache(const QString &artworkPath) = 0;

  virtual void cacheArtwork(const QString &artworkPath, const QPixmap &pixmap) = 0;
  virtual void cacheArtworkInMemoryOnly(const QString &artworkPath, const QPixmap &pixmap) = 0;

  /// Evict cached artwork (in-memory + dirty-tracking) whose source path
  /// begins with @p artworkDirectoryPrefix. Surgical replacement for the
  /// prior blanket-clear: callers pass the collection's resolved
  /// artworkDirectory so only that collection's cache entries drop, leaving
  /// unrelated collections' caches intact. Passing an empty prefix clears
  /// nothing (caller bug — would previously have blanket-cleared).
  virtual void clearCollectionCache(const QString &artworkDirectoryPrefix) = 0;
  virtual void releaseGuiResources() = 0;

  /// Resize the in-memory artwork QCache budget. Driven by the user
  /// `pixmapCacheSizeMB` setting; clamped to a sane lower bound by the
  /// implementation to avoid degenerating into an immediate-eviction
  /// cache. Called at startup wiring and on settings change.
  virtual void setArtworkCacheBudgetMB(int megabytes) = 0;

  [[nodiscard]] virtual CacheMetrics metrics() const = 0;
  virtual void resetMetrics() = 0;
  virtual void logMetrics() const = 0;

  /// Approximate on-disk cache footprint in bytes. Implemented via a
  /// cached directory walk in the real CacheManager; tests can stub it.
  /// Kartend-davi widened the interface so ArtworkManager (which reaches
  /// the cache through ApplicationContext) can use this in its periodic
  /// "have we grown enough to flush?" heuristic without static_casting
  /// back to the concrete type.
  [[nodiscard]] virtual qint64 getCacheSize() const = 0;
};

#endif // ICACHEMANAGER_H
