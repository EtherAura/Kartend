// Manages in-memory pixmap cache with LRU eviction and optional disk
// persistence.
#include "cachemanager.h"
#include "collection/media_settings_persistence.h" // clampArtworkDiskCacheBudgetMB
#include "uiconstants/cache.h"

#include <algorithm>
#include <limits>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>

namespace {

[[nodiscard]] auto clampToCacheCostBytes(const QPixmap &pixmap) -> int {
  constexpr int DEFAULT_BITS_PER_PIXEL = 32;
  constexpr quint64 BITS_PER_BYTE = 8;

  const int bitsPerPixel = pixmap.depth() > 0 ? pixmap.depth() : DEFAULT_BITS_PER_PIXEL;
  const quint64 pixels =
      static_cast<quint64>(pixmap.width()) * static_cast<quint64>(pixmap.height());
  const quint64 bpp = static_cast<quint64>(bitsPerPixel);

  quint64 bits = 0;
  if (bpp > 0 && pixels > (std::numeric_limits<quint64>::max() / bpp)) {
    bits = std::numeric_limits<quint64>::max();
  } else {
    bits = pixels * bpp;
  }

  const quint64 bytes = bits / BITS_PER_BYTE;
  const quint64 maxInt = static_cast<quint64>(std::numeric_limits<int>::max());
  const quint64 clamped = bytes > maxInt ? maxInt : bytes;
  const int cost = static_cast<int>(clamped);
  return cost > 0 ? cost : 1;
}

} // namespace

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcCacheManager, "kartend.cachemanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcCacheManager().isDebugEnabled()) {                                                       \
      qCDebug(lcCacheManager) << msg;                                                              \
    }                                                                                              \
  } while (0)

CacheManager::CacheManager() : m_diskStorage(std::make_unique<CacheDiskStorage>()) {
  // Construction-time default. Owner wires the user-configured budget
  // via setArtworkCacheBudgetMB() during startup; until then we hold the
  // legacy compile-time default so any early artwork inserts that happen
  // before the wiring still get a budget (better than max=0 → no
  // caching). UIConstants::Cache::PIXMAP_CACHE_KB is in KB.
  constexpr qint64 BYTES_PER_KB = 1024;
  const qint64 maxBytes = static_cast<qint64>(UIConstants::Cache::PIXMAP_CACHE_KB) * BYTES_PER_KB;
  const qint64 maxInt = static_cast<qint64>(std::numeric_limits<int>::max());
  artworkCache.setMaxCost(maxBytes > maxInt ? std::numeric_limits<int>::max()
                                            : static_cast<int>(maxBytes));
  // Same deal for the on-disk budget: hold the compile-time default until the
  // owner pushes the user-configured artworkDiskCacheBudgetMB setting.
  m_artworkDiskCacheBudgetMB.storeRelaxed(UIConstants::Cache::ARTWORK_DISK_CACHE_BUDGET_MB);

  // Timer context lives with CacheManager lifetime. CacheManager is
  // created on the main thread (ApplicationManager::initialize).
  m_timerContext = new QObject();
  m_debouncedSaveTimer = new QTimer(m_timerContext);
  m_debouncedSaveTimer->setSingleShot(true);
  QObject::connect(m_debouncedSaveTimer, &QTimer::timeout, m_timerContext, [this]() {
    if (QApplication::closingDown()) {
      return;
    }
    if (m_diskStorage->isCancelled()) {
      return;
    }
    saveToDisk();
  });
}

CacheManager::~CacheManager() {
  // Cancel FIRST: scheduleSaveToDisk() is callable from disk-I/O workers and
  // gates on isCancelled() before reading the raw m_timerContext pointer, so
  // cancelling before the timer-context deletion below closes the window
  // where a worker passes the gate and invokes on a freed QObject
  // (Kartend-lwq3a — mirrors the ordering cancelPendingIo() already uses).
  m_diskStorage->cancel();

  // Then sever the timer→lambda connection BEFORE the pool drain. The lambda
  // captures 'this' and calls saveToDisk(); if it fired later in the
  // destructor (e.g. while the I/O pool drain spins the event loop), it
  // would run against a half-destructed CacheManager. Queued metacalls
  // targeting m_timerContext are dropped at its deletion.
  if (m_debouncedSaveTimer) {
    m_debouncedSaveTimer->stop();
  }
  delete m_timerContext;
  m_timerContext = nullptr;

  // Cooperative shutdown: bounded drain. The disk-I/O lambda captured the
  // cancellation token by shared_ptr value, so in-flight tasks return
  // within milliseconds in the common case. If they don't drain within the
  // budget, CacheDiskStorage abandons the pool (the OS reaps at process
  // exit; tests/suppressions/lsan.txt covers this path).
  constexpr int kShutdownDrainMs = 2000;
  if (!m_diskStorage->drainWithBudget(kShutdownDrainMs)) {
    qCWarning(lcCacheManager) << "CacheManager: I/O thread pool did not drain in"
                              << kShutdownDrainMs
                              << "ms during shutdown; abandoning pool to avoid blocking exit";
  }

  // The async getCacheSize() walk captures `this`; wait for any in-flight
  // walk before letting members go out of scope so the lambda can't touch
  // freed memory (Kartend-bwcd). waitForFinished is a no-op on a
  // default-constructed or already-finished future.
  m_cacheSizeWalkFuture.waitForFinished();
}

auto CacheManager::snapshotTimestampsForShutdown() const -> CacheTimestampsSnapshot {
  // Peek (non-draining) at removal batches still awaiting a successful
  // store write — a batch snapshotted out of the dirty sets whose async
  // write failed or was dropped must still reach the shutdown write.
  // Taken before m_mutex so the two locks never nest.
  const QStringList pendingStoreRemovals = m_diskStorage->pendingRemovals();

  QMutexLocker locker(&m_mutex);
  // Capture the full map AND the pending removal set under the same lock so
  // an invalidation inside the final debounce window (its deleting flush
  // never fired) is carried through the shutdown write (Kartend-2zkm8).
  // Const/non-clearing by design: a later saveToDiskForShutdown() re-deleting
  // the same rows is a harmless no-op DELETE.
  QSet<QString> removed = dirtyRemovals;
  for (const QString &path : pendingStoreRemovals) {
    removed.insert(path);
  }
  return {fileTimestamps, QStringList(removed.cbegin(), removed.cend())};
}

void CacheManager::saveTimestampsSnapshotToDiskForShutdown(
    const CacheTimestampsSnapshot &snapshot) {
  // Write-only operation: safe to call from a worker thread. On shutdown,
  // write all timestamps (full save, not incremental) and DELETE the rows
  // still pending invalidation.
  CacheDiskStorage::writeTimestamps(snapshot.timestamps, snapshot.removedPaths);
}

void CacheManager::cancelPendingIo() {
  // Signal cancellation to any in-flight tasks.
  m_diskStorage->cancel();

  // Stop the debounced save timer to prevent new tasks from starting.
  if (m_debouncedSaveTimer) {
    m_debouncedSaveTimer->stop();
  }

  // Clear queued tasks but DON'T wait for running ones — they check the
  // cancellation token and will exit quickly. Blocking here can cause
  // multi-minute shutdown delays when a large image write batch is in
  // progress.
  m_diskStorage->clearQueue();
}

bool CacheManager::isPendingIoCancelled() const {
  return m_diskStorage->isCancelled();
}

bool CacheManager::isSaveTimerActive() const {
  return m_debouncedSaveTimer && m_debouncedSaveTimer->isActive();
}

// Releases GUI resources and resets in-memory accounting totals
void CacheManager::releaseGuiResources() {
  QMutexLocker locker(&m_mutex);
  artworkCache.clear();
  dirtyArtwork.clear();
  m_dirtyImages.clear();
}

void CacheManager::setArtworkCacheBudgetMB(int megabytes) {
  // Floor at 1 MB so a hand-edited or otherwise out-of-range value can't
  // collapse the cache into an immediate-eviction state (QCache treats
  // maxCost <= 0 as no-cache). The settings UI already clamps to [10,500]
  // on the way in; this guard catches direct API misuse / future
  // recallers.
  constexpr int kMinBudgetMB = 1;
  const int effectiveMB = std::max(kMinBudgetMB, megabytes);
  constexpr qint64 BYTES_PER_MB = 1024 * 1024;
  const qint64 maxBytes = static_cast<qint64>(effectiveMB) * BYTES_PER_MB;
  const qint64 maxInt = static_cast<qint64>(std::numeric_limits<int>::max());
  QMutexLocker locker(&m_mutex);
  artworkCache.setMaxCost(maxBytes > maxInt ? std::numeric_limits<int>::max()
                                            : static_cast<int>(maxBytes));
}

void CacheManager::setArtworkDiskCacheBudgetMB(int megabytes) {
  // Same clamp the settings load/save paths apply (0 = unlimited sentinel,
  // everything else bounded), so direct API misuse can't configure a
  // pathological evict-everything budget. Atomic store — the background
  // getCacheSize() walk reads this without taking m_mutex.
  m_artworkDiskCacheBudgetMB.storeRelaxed(
      MediaSettingsPersistence::clampArtworkDiskCacheBudgetMB(megabytes));
}

namespace {
// Revalidate a memory-cached artwork entry against disk (a stat) at most once
// per this interval per key, so the exists()+lastModified() probe doesn't fire
// for every visible tile on every viewport-artwork pass during scroll
// (Kartend-qszks). The first hit after an insert always revalidates (the map
// defaults to 0), so a single modify-then-read still detects the change.
constexpr qint64 kArtworkRevalidateIntervalMs = 5000;
} // namespace

// Initializes persistent cache metadata from disk
auto CacheManager::getArtwork(const QString &artworkPath) -> QPixmap {
  // Kartend-rqoa0: this is the COLD / one-shot reader. On a memory-cache miss it
  // does synchronous disk I/O + a QPixmap(cachePath) PNG decode inline (a few ms
  // per call), so it must NOT be called from the scroll/layout hot path. The
  // interactive artwork pipeline (ArtworkManager::… -> getArtworkFromMemoryOnly)
  // takes the memory-only fast path for the immediate read and routes the disk
  // decode through a worker thread instead. As of this audit there are no
  // production callers of this disk variant — every interactive read already
  // goes through getArtworkFromMemoryOnly() + worker decode — so the synchronous
  // decode below never runs on the GUI thread during scroll. Keep it for any
  // genuinely cold, one-shot caller; route new hot-path reads through the
  // memory-only variant.
  //
  // QPixmap construction + QGuiApplication::primaryScreen() below are GUI-thread
  // only. Uncalled from decode workers today, but assert the contract so a future
  // worker-side caller fails loudly instead of touching QPixmap off-thread
  // (Kartend-u61vf; the inverse of ArtworkLoadDispatcher::processBatchOnWorker).
  Q_ASSERT_X(QThread::currentThread() == QCoreApplication::instance()->thread(),
             "CacheManager::getArtwork",
             "QPixmap-returning cache reads must run on the GUI thread");
  if (artworkPath.isEmpty()) {
    return {};
  }
  if (QApplication::closingDown()) {
    return {};
  }

  bool invalidated = false;
  QMutexLocker locker(&m_mutex);
  if (QPixmap *pix = artworkCache.object(artworkPath)) {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool dueForRevalidation =
        (nowMs - m_lastRevalidatedMs.value(artworkPath, 0)) >= kArtworkRevalidateIntervalMs;
    // Only construct QFileInfo / stat the file once the throttle window has
    // elapsed — a fresh memory hit skips the QFileInfo allocation (Kartend-5agy1).
    if (dueForRevalidation) {
      const QFileInfo fileInfo(artworkPath);
      if (fileInfo.exists() && fileTimestamps.contains(artworkPath) &&
          fileTimestamps[artworkPath] != fileInfo.lastModified().toMSecsSinceEpoch()) {
        // Cache invalidation - file changed on disk
        QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
        QFile::remove(cachePath);
        artworkCache.remove(artworkPath);
        fileTimestamps.remove(artworkPath);
        dirtyTimestamps.remove(artworkPath);
        dirtyRemovals.insert(artworkPath);
        m_dirtyImages.remove(artworkPath);
        m_lastRevalidatedMs.remove(artworkPath);
        m_metadataDirty = true;
        ++m_metrics.invalidations;
        invalidated = true;
      } else {
        m_lastRevalidatedMs.insert(artworkPath, nowMs);
      }
    }
    if (!invalidated) {
      // Memory cache hit
      ++m_metrics.memoryHits;
      return *pix;
    }
  }
  locker.unlock();

  // Flush the invalidation's store-row deletion on the debounced save path —
  // an invalidate-then-never-recache key must not wait for the bounded prune
  // pass (or the next session) to drop its stale row (Kartend-9lm54).
  if (invalidated) {
    scheduleSaveToDisk();
  }

  if (QApplication::closingDown()) {
    return {};
  }

  QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
  if (QFile::exists(cachePath)) {
    // Validate the cached PNG against the source BEFORE serving it. The old
    // disk path served unconditionally and then stamped fileTimestamps with
    // the CURRENT source mtime — so a stale PNG was not only served, the
    // fresh stamp erased the very mismatch that would have invalidated it on
    // the next read, masking the staleness permanently. Two cases:
    //   * recorded timestamp present and mismatched → the source changed;
    //     invalidate instead of serving stale bytes (mirrors the memory-hit
    //     revalidation above).
    //   * recorded timestamp MISSING (lost dirty batch, store wipe on a
    //     schema mismatch) → the PNG is unvalidatable by row. Fall back to
    //     the PNG's own mtime: it is written — and touched below — strictly
    //     after the source it was encoded from, so a source newer than the
    //     PNG proves staleness. (Known windows: a source replaced during the
    //     encode's debounce/flush, or replaced with a preserved-older mtime,
    //     can pass this heuristic once; the recovered row below restores
    //     exact row validation from then on.)
    // Disk-load path (memory miss, not the hot path): stat here with a local
    // QFileInfo so the memory-hit fast path above stays allocation-free
    // (Kartend-5agy1).
    const QFileInfo sourceInfo(artworkPath);
    const bool sourceExists = sourceInfo.exists();
    const qint64 sourceTs = sourceExists ? sourceInfo.lastModified().toMSecsSinceEpoch() : 0;
    bool timestampKnown = false;
    bool stale = false;
    {
      QMutexLocker relocker(&m_mutex);
      const auto it = fileTimestamps.constFind(artworkPath);
      timestampKnown = it != fileTimestamps.constEnd();
      stale = timestampKnown && sourceExists && it.value() != sourceTs;
    }
    if (!timestampKnown && sourceExists) {
      stale = QFileInfo(cachePath).lastModified().toMSecsSinceEpoch() < sourceTs;
    }
    if (stale) {
      // Delete + miss so the re-encode path repopulates both the PNG and the
      // timestamp row. Falls through to the miss accounting below.
      QFile::remove(cachePath);
      QMutexLocker relocker(&m_mutex);
      artworkCache.remove(artworkPath);
      fileTimestamps.remove(artworkPath);
      dirtyTimestamps.remove(artworkPath);
      dirtyRemovals.insert(artworkPath);
      dirtyArtwork.remove(artworkPath);
      m_dirtyImages.remove(artworkPath);
      m_lastRevalidatedMs.remove(artworkPath);
      m_metadataDirty = true;
      ++m_metrics.invalidations;
      relocker.unlock();
      scheduleSaveToDisk();
    } else {
      QPixmap cachedPixmap(cachePath);
      if (!cachedPixmap.isNull()) {
        // Disk cache hit
        // Touch the cache file so the disk-budget eviction pass (ordered by
        // mtime) sees recently-used artwork as fresh — otherwise eviction is
        // FIFO-by-write-time and drops hot entries as readily as cold ones.
        // One utimens per disk hit; noise next to the PNG decode above. Runs
        // only AFTER validation so the touch can't bump the PNG past a newer
        // source and defeat the missing-row mtime heuristic.
        {
          QFile touch(cachePath);
          if (touch.open(QIODevice::ReadWrite)) {
            touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
          }
        }
        // Set device pixel ratio for HiDPI displays
        qreal dpr = 1.0;
        if (QGuiApplication::primaryScreen()) {
          dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
        }
        cachedPixmap.setDevicePixelRatio(dpr);

        bool recoveredRow = false;
        {
          QMutexLocker relocker(&m_mutex);
          ++m_metrics.diskHits;

          artworkCache.insert(artworkPath, new QPixmap(cachedPixmap),
                              clampToCacheCostBytes(cachedPixmap));
          if (sourceExists) {
            fileTimestamps[artworkPath] = sourceTs;
            if (!timestampKnown) {
              // Recovered row (store wipe / lost dirty batch): mark it dirty
              // so the store row is repopulated and the next session gets
              // exact row validation back instead of the mtime heuristic.
              dirtyTimestamps.insert(artworkPath);
              m_metadataDirty = true;
              recoveredRow = true;
            }
          }
        }
        if (recoveredRow) {
          scheduleSaveToDisk();
        }
        return cachedPixmap;
      }
    }
  }

  // Cache miss
  {
    QMutexLocker relocker(&m_mutex);
    ++m_metrics.misses;
  }
  return {};
}

auto CacheManager::getArtworkFromMemoryOnly(const QString &artworkPath) -> QPixmap {
  // GUI-thread only — returns a QPixmap (Kartend-u61vf).
  Q_ASSERT_X(QThread::currentThread() == QCoreApplication::instance()->thread(),
             "CacheManager::getArtworkFromMemoryOnly",
             "QPixmap-returning cache reads must run on the GUI thread");
  if (artworkPath.isEmpty()) {
    return {};
  }
  if (QApplication::closingDown()) {
    return {};
  }

  QMutexLocker locker(&m_mutex);

  if (QPixmap *pix = artworkCache.object(artworkPath)) {
    // Scroll hot path: one call per pending viewport tile per
    // updateViewportArtwork pass. Throttle the per-hit stat (Kartend-qszks).
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool dueForRevalidation =
        (nowMs - m_lastRevalidatedMs.value(artworkPath, 0)) >= kArtworkRevalidateIntervalMs;
    // Only construct QFileInfo / stat the file once the throttle window has
    // elapsed — a fresh hit (the common case while scrolling) skips the
    // QFileInfo allocation entirely (Kartend-5agy1).
    if (dueForRevalidation) {
      const QFileInfo fileInfo(artworkPath);
      if (fileInfo.exists() && fileTimestamps.contains(artworkPath) &&
          fileTimestamps[artworkPath] != fileInfo.lastModified().toMSecsSinceEpoch()) {
        // Cache invalidation - file changed on disk.
        // Note: This may touch the filesystem, but avoids large image reads.
        const QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
        QFile::remove(cachePath);
        artworkCache.remove(artworkPath);
        fileTimestamps.remove(artworkPath);
        dirtyTimestamps.remove(artworkPath);
        dirtyRemovals.insert(artworkPath);
        dirtyArtwork.remove(artworkPath);
        m_dirtyImages.remove(artworkPath);
        m_lastRevalidatedMs.remove(artworkPath);
        m_metadataDirty = true;
        ++m_metrics.invalidations;
        ++m_metrics.misses;
        locker.unlock();
        // Flush the store-row deletion on the debounced save path; see
        // getArtwork (Kartend-9lm54).
        scheduleSaveToDisk();
        return {};
      }
      m_lastRevalidatedMs.insert(artworkPath, nowMs);
    }
    ++m_metrics.memoryHits;
    return *pix;
  }

  ++m_metrics.misses;
  return {};
}

auto CacheManager::tryLoadArtworkImageFromDiskCache(const QString &artworkPath) -> QImage {
  if (artworkPath.isEmpty()) {
    return {};
  }
  if (QApplication::closingDown()) {
    return {};
  }

  const QFileInfo fileInfo(artworkPath);
  if (!fileInfo.exists()) {
    QMutexLocker locker(&m_mutex);
    ++m_metrics.misses;
    return {};
  }

  const qint64 currentTimestamp = fileInfo.lastModified().toMSecsSinceEpoch();

  // If we have a known timestamp and it no longer matches, invalidate the disk
  // entry.
  bool timestampKnown = false;
  {
    QMutexLocker locker(&m_mutex);
    auto it = fileTimestamps.constFind(artworkPath);
    timestampKnown = it != fileTimestamps.constEnd();
    if (timestampKnown && it.value() != currentTimestamp) {
      fileTimestamps.remove(artworkPath);
      dirtyTimestamps.remove(artworkPath);
      dirtyRemovals.insert(artworkPath);
      dirtyArtwork.remove(artworkPath);
      m_dirtyImages.remove(artworkPath);
      m_metadataDirty = true;
      ++m_metrics.invalidations;

      locker.unlock();
      const QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
      QFile::remove(cachePath);
      // Flush the store-row deletion on the debounced save path; see
      // getArtwork (Kartend-9lm54). Worker-thread safe — scheduleSaveToDisk
      // posts the timer start to its owning thread.
      scheduleSaveToDisk();
      return {};
    }
  }

  const QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
  if (!QFile::exists(cachePath)) {
    QMutexLocker locker(&m_mutex);
    ++m_metrics.misses;
    return {};
  }

  if (!timestampKnown) {
    // No recorded row for this key (lost dirty batch, or a store wipe on a
    // schema mismatch). The old path served the PNG anyway and stamped the
    // CURRENT source mtime below — permanently erasing the mismatch that
    // would have invalidated a stale PNG on every later read. Treat the PNG
    // as unvalidated instead: it is written strictly after the source it was
    // encoded from (and only ever touched after successful validation, both
    // here and in getArtwork), so a source newer than the PNG proves staleness → delete
    // + miss and let the re-encode path repopulate both PNG and store row.
    if (QFileInfo(cachePath).lastModified().toMSecsSinceEpoch() < currentTimestamp) {
      QFile::remove(cachePath);
      {
        QMutexLocker locker(&m_mutex);
        dirtyTimestamps.remove(artworkPath);
        // The in-memory row is absent but a store row may still exist (the
        // lost-batch flavor) — queue the deletion regardless; a DELETE of a
        // nonexistent row is a no-op.
        dirtyRemovals.insert(artworkPath);
        dirtyArtwork.remove(artworkPath);
        m_dirtyImages.remove(artworkPath);
        m_metadataDirty = true;
        ++m_metrics.invalidations;
      }
      scheduleSaveToDisk();
      return {};
    }
  }

  QImage cachedImage(cachePath);
  if (cachedImage.isNull()) {
    QMutexLocker locker(&m_mutex);
    ++m_metrics.misses;
    return {};
  }

  // Touch the cache file so the disk-budget eviction pass (ordered by mtime)
  // sees worker-served artwork as fresh — this path serves most viewport
  // loads, so without it eviction would rank hot entries as cold and drop
  // them first. Same post-validation ordering as getArtwork's touch: it runs
  // only after the row/mtime checks above so it can't bump the PNG past a
  // newer source and defeat the missing-row heuristic.
  {
    QFile touch(cachePath);
    if (touch.open(QIODevice::ReadWrite)) {
      touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    }
  }

  bool recoveredRow = false;
  {
    QMutexLocker locker(&m_mutex);
    ++m_metrics.diskHits;
    fileTimestamps[artworkPath] = currentTimestamp;
    if (!timestampKnown) {
      // Recovered row: persist it again so the next session validates by
      // exact row match instead of the PNG-mtime heuristic above.
      dirtyTimestamps.insert(artworkPath);
      m_metadataDirty = true;
      recoveredRow = true;
    }
  }
  if (recoveredRow) {
    scheduleSaveToDisk();
  }

  return cachedImage;
}

// Caches artwork pixmap if large enough and maintains O(1) running total for
// memory accounting; evicts until under limit
void CacheManager::cacheArtwork(const QString &artworkPath, const QPixmap &pixmap) {
  cacheArtwork(artworkPath, pixmap, QImage());
}

// Image-carrying overload: the artwork delivery paths still hold the
// worker-decoded QImage the pixmap was built from, so retaining it here
// (implicitly shared) lets the debounced flush snapshot an encode-ready
// QImage with zero conversion instead of deep-copying the pixmap back on the
// GUI thread. A null @p sourceImage is the pixmap-only path — the flush
// falls back to QPixmap::toImage() for those entries.
void CacheManager::cacheArtwork(const QString &artworkPath, const QPixmap &pixmap,
                                const QImage &sourceImage) {
  if (artworkPath.isEmpty() || pixmap.isNull()) {
    return;
  }
  if (QApplication::closingDown()) {
    return;
  }
  if (pixmap.width() < UIConstants::Cache::MIN_PIXMAP_SIZE ||
      pixmap.height() < UIConstants::Cache::MIN_PIXMAP_SIZE) {
    return;
  }

  const int cost = clampToCacheCostBytes(pixmap);

  // Stat the source BEFORE taking the cache mutex: callers run on the GUI
  // thread (up to ~8 deliveries per apply tick during loading), and a cold or
  // network-mounted path stat held under m_mutex serialized the apply loop
  // against every worker cache probe contending for the same lock. The
  // captured stamp keeps the revalidation semantics identical — it reflects
  // the file the pixmap was just decoded from.
  const QFileInfo fileInfo(artworkPath);
  const bool sourceExists = fileInfo.exists();
  const qint64 sourceTs = sourceExists ? fileInfo.lastModified().toMSecsSinceEpoch() : 0;

  QMutexLocker locker(&m_mutex);
  if (QApplication::closingDown()) {
    return;
  }

  // Insert/update the pixmap and adjust running total
  artworkCache.insert(artworkPath, new QPixmap(pixmap), cost);
  if (sourceExists) {
    fileTimestamps[artworkPath] = sourceTs;
    dirtyTimestamps.insert(artworkPath); // Track which timestamps are new/changed
    // A re-cache supersedes any invalidation queued for this key. Belt and
    // braces — writeTimestamps also runs deletions before upserts, so a key
    // snapshotted into both batches still ends with the fresh row
    // (Kartend-9lm54).
    dirtyRemovals.remove(artworkPath);
  }
  dirtyArtwork.insert(artworkPath);
  if (!sourceImage.isNull()) {
    m_dirtyImages.insert(artworkPath, sourceImage);
  } else {
    // A pixmap-only re-cache may carry different content than a previously
    // retained image — drop it so the flush converts the current pixmap.
    m_dirtyImages.remove(artworkPath);
  }
  m_metadataDirty = true;
  ++m_metrics.inserts;

  locker.unlock();
  scheduleSaveToDisk();
}

void CacheManager::cacheArtworkInMemoryOnly(const QString &artworkPath, const QPixmap &pixmap) {
  if (artworkPath.isEmpty() || pixmap.isNull()) {
    return;
  }
  if (QApplication::closingDown()) {
    return;
  }
  if (pixmap.width() < UIConstants::Cache::MIN_PIXMAP_SIZE ||
      pixmap.height() < UIConstants::Cache::MIN_PIXMAP_SIZE) {
    return;
  }

  const int cost = clampToCacheCostBytes(pixmap);

  // Stat outside the lock — same rationale as cacheArtwork above.
  const QFileInfo fileInfo(artworkPath);
  const bool sourceExists = fileInfo.exists();
  const qint64 sourceTs = sourceExists ? fileInfo.lastModified().toMSecsSinceEpoch() : 0;

  QMutexLocker locker(&m_mutex);
  if (QApplication::closingDown()) {
    return;
  }

  artworkCache.insert(artworkPath, new QPixmap(pixmap), cost);
  // If the key is still dirty from an earlier cacheArtwork, the retained
  // image no longer matches the pixmap just inserted — drop it so the flush
  // converts the live pixmap instead of writing stale content.
  m_dirtyImages.remove(artworkPath);
  if (sourceExists) {
    fileTimestamps[artworkPath] = sourceTs;
  }
  ++m_metrics.inserts;
}

// Evicts only the artwork-cache + dirty-tracking entries whose source path
// begins with @p artworkDirectoryPrefix, leaving other collections' caches
// intact. Empty prefix is a no-op — callers that intend a full purge
// should be explicit rather than going through this collection-scoped
// path. Pattern mirrors ItemMetadataCache::invalidateCollection.
void CacheManager::clearCollectionCache(const QString &artworkDirectoryPrefix) {
  if (artworkDirectoryPrefix.isEmpty()) return;

  QMutexLocker locker(&m_mutex);

  // Normalise the prefix to a directory-terminated form so an
  // artworkDirectory of "/home/u/art" doesn't accidentally match
  // "/home/u/art-old/cover.png". QCache::keys() returns the full key list
  // — safe to copy here because the underlying storage isn't mutated
  // while we hold m_mutex.
  QString prefix = artworkDirectoryPrefix;
  if (!prefix.endsWith(QLatin1Char('/'))) {
    prefix.append(QLatin1Char('/'));
  }

  const QList<QString> allKeys = artworkCache.keys();
  for (const QString &key : allKeys) {
    if (key.startsWith(prefix)) {
      artworkCache.remove(key);
    }
  }
  // Walk dirtyArtwork with the same prefix; std::erase_if would be cleaner
  // but QSet has no project-wide erase-if helper today.
  for (auto it = dirtyArtwork.begin(); it != dirtyArtwork.end();) {
    if (it->startsWith(prefix)) {
      it = dirtyArtwork.erase(it);
    } else {
      ++it;
    }
  }
  // And the retained decode images for the same prefix, so a cleared
  // collection's dirty entries can't pin their QImages until the next flush.
  for (auto it = m_dirtyImages.begin(); it != m_dirtyImages.end();) {
    if (it.key().startsWith(prefix)) {
      it = m_dirtyImages.erase(it);
    } else {
      ++it;
    }
  }
}

// Returns the cached on-disk size and (when stale) dispatches a background
// walk to refresh it. The previous implementation walked the cache
// directory via QDirIterator on the GUI thread every CHECK_INTERVAL
// artworks during scroll — O(N files) per call (Kartend-bwcd).
qint64 CacheManager::getCacheSize() const {
  constexpr qint64 STALE_AFTER_MS = 30000;
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  bool shouldDispatch = false;
  qint64 result = 0;
  {
    QMutexLocker locker(&m_diskCacheSizeMutex);
    result = m_cachedDiskCacheSize;
    if (!m_diskWalkInFlight &&
        (m_lastDiskWalkMs == 0 || nowMs - m_lastDiskWalkMs >= STALE_AFTER_MS)) {
      m_diskWalkInFlight = true;
      shouldDispatch = true;
    }
  }

  if (shouldDispatch) {
    // Capture `this` — ~CacheManager waits on m_cacheSizeWalkFuture so
    // the lambda can't outlive the owning object.
    m_cacheSizeWalkFuture = QtConcurrent::run([this]() {
      const QString cacheDirPath = CacheDiskStorage::cacheDirectory();
      const QString artworkPrefix = cacheDirPath + QStringLiteral("/artwork/");
      qint64 totalSize = 0;
      QList<DiskCacheFile> artworkFiles;
      QDirIterator dirIt(cacheDirPath, QDir::Files, QDirIterator::Subdirectories);
      while (dirIt.hasNext()) {
        dirIt.next();
        const QFileInfo info = dirIt.fileInfo();
        totalSize += info.size();
        // Eviction candidates are the decoded-artwork PNGs only — the
        // metadata store under metadata/ must never be evicted.
        if (info.filePath().startsWith(artworkPrefix)) {
          artworkFiles.append(
              {info.filePath(), info.size(), info.lastModified().toMSecsSinceEpoch()});
        }
      }
      // Disk budget, piggybacked on this walk since it already enumerated
      // every file with sizes: without a cap the artwork cache grows by one
      // PNG per artwork ever decoded — silent and unbounded on end-user
      // machines. The budget is the user-configured artworkDiskCacheBudgetMB
      // setting (0 = unlimited; evictArtworkOverDiskBudget no-ops on a
      // non-positive budget), defaulting to ARTWORK_DISK_CACHE_BUDGET_MB.
      // Same const_cast rationale as pruneStaleEntries() below.
      const qint64 diskBudgetBytes =
          static_cast<qint64>(m_artworkDiskCacheBudgetMB.loadRelaxed()) * 1024 * 1024;
      totalSize -= const_cast<CacheManager *>(this)->evictArtworkOverDiskBudget(
          std::move(artworkFiles), totalSize, diskBudgetBytes);
      {
        // NOLINTBEGIN(clang-analyzer-core.NullDereference) — analyzer can't
        // see that ~CacheManager waits on m_cacheSizeWalkFuture, so the
        // lambda never outlives `this`. The captured pointer is safe.
        QMutexLocker locker(&m_diskCacheSizeMutex);
        m_cachedDiskCacheSize = totalSize;
        m_lastDiskWalkMs = QDateTime::currentMSecsSinceEpoch();
        m_diskWalkInFlight = false;
        // NOLINTEND(clang-analyzer-core.NullDereference)
      }
      // Opportunistic timestamp GC piggybacks on this walk thread so it
      // never runs on the GUI thread (Kartend-0ldg2). getCacheSize() is
      // conceptually a read — the const signature comes from ICacheManager —
      // but the maintenance pass mutates bookkeeping this object owns, so
      // cast the const away here rather than poisoning the maps with
      // `mutable` (precedent: databasemanager_items.cpp).
      const_cast<CacheManager *>(this)->pruneStaleEntries();
    });
  }

  return result;
}

// Opportunistic GC for the timestamp store + in-memory bookkeeping
// (Kartend-0ldg2). Runs on the background getCacheSize() walk thread (the
// walk is single-flight, so passes never overlap; ~CacheManager waits on the
// walk future, so the pass can't outlive the object). Each pass is bounded:
// it stats at most 2 * kPruneBatchPerWalk files, so a pass over a 100k-entry
// store costs milliseconds and the rotating cursors cover the full keyspace
// over successive walks (one walk per ≥30 s of getCacheSize() traffic).
void CacheManager::pruneStaleEntries() {
  constexpr int kPruneBatchPerWalk = 256;
  if (m_diskStorage->isCancelled()) {
    return; // shutting down — leave the store alone
  }

  QString dbCursor;
  QString memCursor;
  {
    QMutexLocker locker(&m_diskCacheSizeMutex);
    dbCursor = m_pruneDbCursor;
    memCursor = m_sweepCursor;
  }

  // 1. Disk store: delete a bounded batch of rows whose source path no
  // longer exists. Returns the deleted paths so the in-memory maps can drop
  // the same keys without re-stating them.
  const QStringList deadDbRows =
      CacheDiskStorage::pruneDeadTimestamps(kPruneBatchPerWalk, &dbCursor);

  // 2. In-memory bookkeeping. Collect the stat candidates under the lock,
  // stat OUTSIDE the lock (filesystem I/O must not block the GUI-thread
  // cache paths), then re-check + erase under the lock.
  QStringList candidates;
  bool sweepExhausted = false;
  {
    QMutexLocker locker(&m_mutex);
    // a) Revalidation stamps are only consulted on artworkCache hits; an
    // entry whose pixmap was evicted is dead weight. Dropping it merely
    // forces one revalidating stat if the key is ever re-inserted. No stat
    // needed, so this sweep is unbounded (O(map) pointer work).
    for (auto it = m_lastRevalidatedMs.begin(); it != m_lastRevalidatedMs.end();) {
      if (!artworkCache.contains(it.key())) {
        it = m_lastRevalidatedMs.erase(it);
      } else {
        ++it;
      }
    }
    // b) Keys the disk prune just confirmed dead.
    for (const QString &path : deadDbRows) {
      if (!artworkCache.contains(path)) {
        fileTimestamps.remove(path);
        dirtyTimestamps.remove(path);
      }
    }
    // c) Candidates for the stat-based sweep: fileTimestamps keys not
    // currently in artworkCache (covers entries seeded from the store at
    // startup AND cacheArtworkInMemoryOnly inserts the store never sees).
    for (auto it = fileTimestamps.cbegin(); it != fileTimestamps.cend(); ++it) {
      if (it.key() > memCursor && !artworkCache.contains(it.key())) {
        candidates.append(it.key());
      }
    }
  }
  // Keep only the kPruneBatchPerWalk smallest keys above the cursor so the
  // batch is deterministic against QHash's arbitrary iteration order.
  if (candidates.size() <= kPruneBatchPerWalk) {
    sweepExhausted = true;
  } else {
    std::partial_sort(candidates.begin(), candidates.begin() + kPruneBatchPerWalk,
                      candidates.end());
    candidates.resize(kPruneBatchPerWalk);
  }

  QStringList deadMemKeys;
  for (const QString &path : std::as_const(candidates)) {
    if (!QFileInfo::exists(path)) {
      deadMemKeys.append(path);
    }
  }
  {
    QMutexLocker locker(&m_mutex);
    for (const QString &path : std::as_const(deadMemKeys)) {
      // Re-check under the lock — the key may have been re-cached while we
      // were statting.
      if (!artworkCache.contains(path)) {
        fileTimestamps.remove(path);
        dirtyTimestamps.remove(path);
        m_lastRevalidatedMs.remove(path);
      }
    }
  }

  {
    QMutexLocker locker(&m_diskCacheSizeMutex);
    m_pruneDbCursor = dbCursor;
    m_sweepCursor = (sweepExhausted || candidates.isEmpty())
                        ? QString()
                        : *std::max_element(candidates.cbegin(), candidates.cend());
  }
}

qint64 CacheManager::evictArtworkOverDiskBudget(QList<DiskCacheFile> files, qint64 totalDiskBytes,
                                                qint64 budgetBytes) {
  if (budgetBytes <= 0 || totalDiskBytes <= budgetBytes) {
    return 0;
  }
  // Oldest-touched first. Cache PNGs are written once and touched on every
  // disk-cache hit (getArtwork), so mtime approximates last use; entries the
  // user still scrolls past regularly survive, cold ones go first.
  std::sort(files.begin(), files.end(),
            [](const DiskCacheFile &a, const DiskCacheFile &b) { return a.mtimeMs < b.mtimeMs; });
  const qint64 target = budgetBytes - budgetBytes / 10;
  qint64 freed = 0;
  int evicted = 0;
  for (const DiskCacheFile &file : std::as_const(files)) {
    if (totalDiskBytes - freed <= target) {
      break;
    }
    if (m_diskStorage->isCancelled()) {
      break; // shutting down — leave the rest for the next session's walk
    }
    // A failed remove (e.g. a writer holds the file on Windows) is skipped;
    // the file stays a candidate for the next pass. The orphaned timestamps
    // row (keyed by source path, not recoverable from the PNG name) is
    // harmless: the next load finds no cached file and simply re-encodes.
    if (QFile::remove(file.path)) {
      freed += file.size;
      ++evicted;
    }
  }
  if (evicted > 0) {
    qCInfo(lcCacheManager) << "Artwork disk cache exceeded its" << (budgetBytes / (1024 * 1024))
                           << "MB budget — evicted" << evicted << "oldest files ("
                           << (freed / (1024 * 1024)) << "MB )";
  }
  return freed;
}

int CacheManager::fileTimestampCountForTesting() const {
  QMutexLocker locker(&m_mutex);
  return fileTimestamps.size();
}

int CacheManager::lastRevalidatedCountForTesting() const {
  QMutexLocker locker(&m_mutex);
  return m_lastRevalidatedMs.size();
}

int CacheManager::dirtyImageCountForTesting() const {
  QMutexLocker locker(&m_mutex);
  return m_dirtyImages.size();
}

auto CacheManager::metrics() const -> CacheMetrics {
  QMutexLocker locker(&m_mutex);
  return m_metrics;
}

void CacheManager::resetMetrics() {
  QMutexLocker locker(&m_mutex);
  m_metrics.reset();
}

void CacheManager::logMetrics() const {
  if (!lcCacheManager().isDebugEnabled()) {
    return;
  }
  CacheMetrics m = metrics();
  qCDebug(lcCacheManager) << "CacheManager metrics:"
                          << "memHits=" << m.memoryHits << "diskHits=" << m.diskHits
                          << "misses=" << m.misses << "inserts=" << m.inserts
                          << "evictions=" << m.evictions << "invalidations=" << m.invalidations
                          << "memHitRate=" << QString::number(m.memoryHitRate() * 100, 'f', 1)
                          << "%"
                          << "totalHitRate=" << QString::number(m.totalHitRate() * 100, 'f', 1)
                          << "%";
}
