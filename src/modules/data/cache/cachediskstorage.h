#ifndef CACHEDISKSTORAGE_H
#define CACHEDISKSTORAGE_H

#include <atomic>
#include <memory>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPair>
#include <QString>

class QThreadPool;

/// Owns CacheManager's disk-persistence responsibilities: the cache
/// directory layout, the path-hashing scheme that maps an artwork file to
/// its on-disk cache filename, the JSON metadata read/write, and the
/// async write pipeline (dedicated single-thread QThreadPool plus the
/// cooperative cancellation token that keeps shutdown bounded).
///
/// CacheManager retains the in-memory state (QCache, fileTimestamps,
/// dirty sets, metrics, debounced save timer) and orchestrates when to
/// flush; the storage helper only knows about disk shape + async I/O.
class CacheDiskStorage {
public:
  CacheDiskStorage();
  ~CacheDiskStorage();

  CacheDiskStorage(const CacheDiskStorage &) = delete;
  CacheDiskStorage &operator=(const CacheDiskStorage &) = delete;
  CacheDiskStorage(CacheDiskStorage &&) = delete;
  CacheDiskStorage &operator=(CacheDiskStorage &&) = delete;

  /// On-disk root for cached artwork + metadata. Creates artwork/ +
  /// metadata/ subdirectories on first call (idempotent).
  [[nodiscard]] static QString cacheDirectory();

  /// Hash @p artworkPath to its on-disk cache filename. MD5 of the UTF-8
  /// path bytes; the same input always produces the same output, so a
  /// rename of the source file produces a different cache entry that
  /// will be rebuilt on next decode.
  [[nodiscard]] static QString artworkCachePath(const QString &artworkPath);

  /// Path to the cache metadata JSON file (timestamps + future fields).
  [[nodiscard]] static QString metadataPath();

  /// Read the cache metadata file synchronously and populate @p outTimestamps
  /// with the deserialized timestamp map. Returns silently when the file
  /// doesn't exist (first-run case) or is malformed.
  void readTimestampsInto(QHash<QString, qint64> &outTimestamps) const;

  /// Write the timestamp map to the metadata JSON file synchronously,
  /// merging with any existing fields. Used both inline (shutdown
  /// snapshot) and from the async-save lambda. Static because the
  /// caller already snapshotted state — the helper has no per-instance
  /// dependencies for the write.
  static void writeTimestamps(const QHash<QString, qint64> &dirtyTimestamps);

  /// Schedule an async save on the worker pool. Encodes @p dirtyImages
  /// to PNG inside the dedicated cache directory; writes (or skips
  /// writing) @p dirtyTimestamps based on @p shouldWriteMetadata. The
  /// lambda captures the cancellation token by shared_ptr value so it
  /// keeps observing cancellation even if the storage instance is
  /// being torn down.
  void scheduleAsyncSave(bool shouldWriteMetadata, const QHash<QString, qint64> &dirtyTimestamps,
                         const QList<QPair<QString, QImage>> &dirtyImages);

  /// Set the cancellation token. In-flight tasks see it via shared_ptr;
  /// queued tasks check it before running.
  void cancel();

  /// True when cancel() has been called and the token still reads cancelled.
  [[nodiscard]] bool isCancelled() const;

  /// Drop the queued tasks. Doesn't wait for in-flight work — those
  /// tasks check the cancellation token themselves.
  void clearQueue();

  /// Bounded-wait shutdown: gives queued + in-flight tasks @p budgetMs
  /// to drain after observing the cancellation flag, then abandons the
  /// pool if it's still draining (matches CacheManager's existing
  /// shutdown policy — the OS reclaims at process exit). Returns true
  /// when the pool drained cleanly within the budget.
  [[nodiscard]] bool drainWithBudget(int budgetMs);

private:
  std::shared_ptr<std::atomic_bool> m_cancelToken;
  // Raw pointer; intentionally leaked at shutdown when the drain budget
  // expires (~QThreadPool blocks; the OS reaps).
  QThreadPool *m_pool;
};

#endif // CACHEDISKSTORAGE_H
