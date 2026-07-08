// Sibling TU: disk-persistence orchestration for CacheManager. The
// path-hashing scheme, the SQLite timestamp store, and the worker pool that
// runs async PNG encodes now live on CacheDiskStorage. This TU keeps the
// orchestration glue: timer-driven debounce, snapshot-under-lock, and the
// shutdown-time synchronous flush.
#include "cachediskstorage.h"
#include "cachemanager.h"
#include "uiconstants/cache.h"

#include <QApplication>
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QMetaObject>
#include <QPair>
#include <QPixmap>
#include <QTimer>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcCacheManager)

void CacheManager::initialize() {
  // Kartend-7s2mv: load the timestamp store exactly once. ApplicationManager
  // kicks this off on a background QtConcurrent thread; this guard makes any
  // additional caller (now or in future) a cheap no-op instead of re-reading
  // and re-clearing the store (formerly a 50MB+ JSON parse that ran twice at
  // startup; now a single SELECT — Kartend-0ldg2).
  if (!m_initStarted.testAndSetOrdered(0, 1)) {
    return;
  }
  // Read the store into a local map with NO lock held: the SQLite open +
  // SELECT + row loop can take a while on a large store or slow disk, and
  // holding m_mutex across it blocked every GUI-thread cache call (early
  // cacheArtwork / getArtworkFromMemoryOnly) for the whole load.
  QHash<QString, qint64> loaded;
  m_diskStorage->readTimestampsInto(loaded);

  // Merge under the lock, keeping existing in-memory entries: they were
  // recorded by cache calls that ran while this load was in flight, so they
  // are always fresher than the store rows. (The old clear+reload replaced
  // them with stale rows, making the next revalidation spuriously invalidate
  // a freshly-decoded pixmap.) Keys queued for removal stay out too — a
  // store row must not resurrect an entry invalidated during the load.
  QMutexLocker locker(&m_mutex);
  for (auto it = loaded.cbegin(); it != loaded.cend(); ++it) {
    if (!fileTimestamps.contains(it.key()) && !dirtyRemovals.contains(it.key())) {
      fileTimestamps.insert(it.key(), it.value());
    }
  }
}

void CacheManager::scheduleSaveToDisk(int delayMs) {
  if (QApplication::closingDown()) {
    return;
  }
  if (m_diskStorage->isCancelled()) {
    return;
  }
  if (!m_timerContext || !m_debouncedSaveTimer) {
    return;
  }

  const int resolvedDelay = delayMs >= 0 ? delayMs : UIConstants::Cache::FLUSH_DEBOUNCE_MS;

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  int effectiveDelay = resolvedDelay;
  {
    QMutexLocker locker(&m_mutex);
    if (!m_metadataDirty && dirtyArtwork.isEmpty()) {
      return;
    }
    if (m_firstDirtyAtMs <= 0) {
      m_firstDirtyAtMs = nowMs;
    }
    if (nowMs - m_firstDirtyAtMs >= UIConstants::Cache::SAVE_DEFER_MS) {
      // Don't postpone forever during continuous background loading.
      effectiveDelay = qMin(effectiveDelay, UIConstants::Cache::QUICK_SAVE_DELAY_MS);
    }
  }

  // Coalesce: if we already posted a start-timer task that hasn't yet run on
  // m_timerContext's thread, skip posting another. During heavy cache fill this
  // avoids hundreds of redundant QueuedConnection events all racing to start
  // the same timer. The flag is cleared inside the lambda so the next round of
  // dirty work re-posts as expected.
  if (m_savePostInFlight.fetchAndStoreAcquire(1) != 0) {
    return;
  }

  // Ensure the timer is started on its owning thread.
  QMetaObject::invokeMethod(
      m_timerContext,
      [this, effectiveDelay]() {
        m_savePostInFlight.storeRelease(0);
        // Re-check cancellation: this post may have been queued BEFORE
        // cancelPendingIo() stopped the timer, and starting it here would
        // resurrect a cancelled timer (Kartend-yjklc). The fire was benign —
        // saveToDisk() re-checks isCancelled() — but the timer must stay
        // inactive after cancel so the contract is directly observable.
        if (m_diskStorage->isCancelled()) {
          return;
        }
        if (m_debouncedSaveTimer) {
          m_debouncedSaveTimer->start(effectiveDelay);
        }
      },
      Qt::QueuedConnection);
}

// Snapshots dirty in-memory state under lock, then hands the snapshot to
// the disk-storage helper for async encode + write.
void CacheManager::saveToDisk() {
  if (QApplication::closingDown()) {
    return;
  }
  if (m_diskStorage->isCancelled()) {
    return;
  }

  bool shouldWriteMetadata = false;
  QHash<QString, qint64> dirtyTimestampsCopy;
  QStringList removedPathsCopy;
  QList<QPair<QString, QImage>> dirtyImages;
  QList<QPair<QString, QPixmap>> pixmapFallbacks;

  {
    QMutexLocker locker(&m_mutex);
    shouldWriteMetadata =
        m_metadataDirty && (!dirtyTimestamps.isEmpty() || !dirtyRemovals.isEmpty());
    if (shouldWriteMetadata) {
      // Only copy timestamps for paths that actually changed.
      for (const QString &path : std::as_const(dirtyTimestamps)) {
        if (fileTimestamps.contains(path)) {
          dirtyTimestampsCopy[path] = fileTimestamps[path];
        }
      }
      // Snapshot the invalidated keys so their store rows are DELETEd in the
      // same write batch (Kartend-9lm54).
      removedPathsCopy = QStringList(dirtyRemovals.cbegin(), dirtyRemovals.cend());
      dirtyTimestamps.clear();
      dirtyRemovals.clear();
      m_metadataDirty = false;
    }
    for (const QString &path : std::as_const(dirtyArtwork)) {
      if (QPixmap *pix = artworkCache.object(path)) {
        // Prefer the worker-decoded QImage retained at cacheArtwork time —
        // snapshotting it is an implicitly-shared copy, not a deep one, so
        // the flush window's 50-300 dirty entries cost refcounts here
        // instead of one QPixmap::toImage() deep copy (0.6–2.5 MB) each.
        const QImage retained = m_dirtyImages.value(path);
        if (!retained.isNull()) {
          dirtyImages.append(qMakePair(path, retained));
        } else {
          pixmapFallbacks.append(qMakePair(path, *pix));
        }
      }
    }
    dirtyArtwork.clear();
    // The snapshot owns the images now (shared refs); dropping the store
    // here keeps retained decodes bounded to one dirty window.
    m_dirtyImages.clear();
    m_firstDirtyAtMs = 0;
  }

  if (!shouldWriteMetadata && dirtyImages.isEmpty() && pixmapFallbacks.isEmpty()) {
    return;
  }

  // Fallback for pixmap-only inserts (no retained decode image): convert on
  // the main thread (QPixmap is GUI-thread bound) before handing the
  // snapshot to the worker pool. The hot delivery paths pass the QImage
  // through cacheArtwork, so this loop is normally empty.
  dirtyImages.reserve(dirtyImages.size() + pixmapFallbacks.size());
  for (const auto &entry : pixmapFallbacks) {
    dirtyImages.append(qMakePair(entry.first, entry.second.toImage()));
  }

  m_diskStorage->scheduleAsyncSave(shouldWriteMetadata, dirtyTimestampsCopy, removedPathsCopy,
                                   dirtyImages);
}

// Synchronous shutdown flush: cancels any in-flight async writes (so
// they can't overwrite the final snapshot), then upserts the full
// timestamp map directly (one transaction; rows already current are
// simply rewritten in place) and deletes any rows still pending
// invalidation (Kartend-9lm54). Pixmap flush is intentionally skipped —
// it's expensive and the in-memory pixmaps may already be invalidated.
void CacheManager::saveToDiskForShutdown() {
  m_diskStorage->cancel();
  m_diskStorage->clearQueue();

  QHash<QString, qint64> timestampsCopy;
  QStringList removedPathsCopy;
  {
    QMutexLocker locker(&m_mutex);
    timestampsCopy = fileTimestamps;
    removedPathsCopy = QStringList(dirtyRemovals.cbegin(), dirtyRemovals.cend());
    dirtyArtwork.clear();
    m_dirtyImages.clear();
    dirtyTimestamps.clear();
    dirtyRemovals.clear();
  }
  // Fold in removal batches whose async write failed or never ran (the
  // queue was cleared above; scheduleAsyncSave stages removals before
  // dispatching) so invalidated keys' store rows finally get deleted
  // instead of surviving into the next session. Residual window: a task
  // that already took the staged set and is still mid-write past this point
  // re-stages on failure after this take — the next session's bounded prune
  // remains the backstop for that sliver.
  removedPathsCopy += m_diskStorage->takePendingRemovals();

  // Deletions run before the upserts, so a key that was invalidated and then
  // re-seeded into fileTimestamps still lands with its current value.
  CacheDiskStorage::writeTimestamps(timestampsCopy, removedPathsCopy);
}
