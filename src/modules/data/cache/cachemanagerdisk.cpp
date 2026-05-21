// Sibling TU: disk-persistence orchestration for CacheManager. The
// path-hashing scheme, JSON metadata read/write, and the worker pool that
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
  QMutexLocker locker(&m_mutex);
  fileTimestamps.clear();
  m_diskStorage->readTimestampsInto(fileTimestamps);
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
  QList<QPair<QString, QPixmap>> dirtyPixmaps;

  {
    QMutexLocker locker(&m_mutex);
    shouldWriteMetadata = m_metadataDirty && !dirtyTimestamps.isEmpty();
    if (shouldWriteMetadata) {
      // Only copy timestamps for paths that actually changed.
      for (const QString &path : std::as_const(dirtyTimestamps)) {
        if (fileTimestamps.contains(path)) {
          dirtyTimestampsCopy[path] = fileTimestamps[path];
        }
      }
      dirtyTimestamps.clear();
      m_metadataDirty = false;
    }
    for (const QString &path : std::as_const(dirtyArtwork)) {
      if (QPixmap *pix = artworkCache.object(path)) {
        dirtyPixmaps.append(qMakePair(path, *pix));
      }
    }
    dirtyArtwork.clear();
    m_firstDirtyAtMs = 0;
  }

  if (!shouldWriteMetadata && dirtyPixmaps.isEmpty()) {
    return;
  }

  // Convert pixmaps → images on the main thread (QPixmap is GUI-thread
  // bound) before handing the snapshot to the worker pool.
  QList<QPair<QString, QImage>> dirtyImages;
  dirtyImages.reserve(dirtyPixmaps.size());
  for (const auto &entry : dirtyPixmaps) {
    dirtyImages.append(qMakePair(entry.first, entry.second.toImage()));
  }

  m_diskStorage->scheduleAsyncSave(shouldWriteMetadata, dirtyTimestampsCopy, dirtyImages);
}

// Synchronous shutdown flush: cancels any in-flight async writes (so
// they can't overwrite the final snapshot), then writes the full
// timestamp map directly. Pixmap flush is intentionally skipped — it's
// expensive and the in-memory pixmaps may already be invalidated.
void CacheManager::saveToDiskForShutdown() {
  m_diskStorage->cancel();
  m_diskStorage->clearQueue();

  QHash<QString, qint64> timestampsCopy;
  {
    QMutexLocker locker(&m_mutex);
    timestampsCopy = fileTimestamps;
    dirtyArtwork.clear();
    dirtyTimestamps.clear();
  }

  CacheDiskStorage::writeTimestamps(timestampsCopy);
}
