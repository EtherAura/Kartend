#ifndef CACHEDISKSTORAGE_H
#define CACHEDISKSTORAGE_H

#include <atomic>
#include <memory>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class QThreadPool;

/// Owns CacheManager's disk-persistence responsibilities: the cache
/// directory layout, the path-hashing scheme that maps an artwork file to
/// its on-disk cache filename, the SQLite timestamp store, and the
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

  /// Path to the LEGACY cache metadata JSON file. Timestamps now live in
  /// the SQLite store (databasePath()); this path is retained only for the
  /// one-way migration that imports a pre-existing JSON blob and renames it
  /// to "<name>.migrated" on the store's first open (Kartend-0ldg2).
  [[nodiscard]] static QString metadataPath();

  /// Path to the SQLite timestamp store (path TEXT PRIMARY KEY,
  /// ts_ms INTEGER). Lives beside the legacy JSON under metadata/. Its own
  /// file, deliberately NOT media.db — the cache layer stays independent of
  /// DatabaseManager.
  [[nodiscard]] static QString databasePath();

  /// Read the timestamp store synchronously (one full SELECT) and populate
  /// @p outTimestamps. Returns silently when neither the store nor the
  /// legacy JSON exists (first-run case — no empty store is created) or the
  /// store can't be opened. Imports + renames a legacy JSON file first when
  /// one is present.
  void readTimestampsInto(QHash<QString, qint64> &outTimestamps) const;

  /// Upsert ONLY @p dirtyTimestamps into the store — and DELETE the
  /// @p removedPaths rows — in one transaction. Rows in neither batch are
  /// untouched: this is the keyed-store replacement for the old
  /// read-50MB/merge/rewrite JSON cycle. Removals run BEFORE the upserts,
  /// so a key invalidated and re-cached within the same debounce window
  /// (present in both batches) ends up with the fresh row (Kartend-9lm54).
  /// Used both inline (shutdown snapshot, where the "dirty" batch is the
  /// full map) and from the async-save lambda. Static because the caller
  /// already snapshotted state — the helper has no per-instance
  /// dependencies.
  static void writeTimestamps(const QHash<QString, qint64> &dirtyTimestamps,
                              const QStringList &removedPaths = {});

  /// Opportunistic GC: examine up to @p maxRowsToExamine rows (ordered by
  /// path, resuming after @p *cursorPath) and DELETE the ones whose source
  /// path no longer exists on disk, in one transaction. Advances the cursor
  /// to the last examined row, clearing it when the keyspace is exhausted so
  /// the next pass wraps to the start. Returns the deleted paths so the
  /// caller can sweep its in-memory bookkeeping for the same keys. No-op
  /// (and cursor reset) when the store doesn't exist yet.
  [[nodiscard]] static QStringList pruneDeadTimestamps(int maxRowsToExamine, QString *cursorPath);

  /// Schedule an async save on the worker pool. Encodes @p dirtyImages
  /// to PNG inside the dedicated cache directory; writes (or skips
  /// writing) @p dirtyTimestamps and the @p removedTimestampPaths
  /// deletions based on @p shouldWriteMetadata. The lambda captures the
  /// cancellation token by shared_ptr value so it keeps observing
  /// cancellation even if the storage instance is being torn down.
  void scheduleAsyncSave(bool shouldWriteMetadata, const QHash<QString, qint64> &dirtyTimestamps,
                         const QStringList &removedTimestampPaths,
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
