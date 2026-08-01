// Sibling TU: disk-budget/eviction + stats maintenance for CacheManager.
// Lives here: the background getCacheSize() walk (size accounting + budget
// dispatch), evictArtworkOverDiskBudget (oldest-first PNG eviction),
// pruneStaleEntries (bounded timestamp/bookkeeping GC), and the metrics /
// testing counters. Lifecycle, lookup, and insert paths stay in
// cachemanager.cpp; the debounced disk-persistence glue lives in
// cachemanagerdisk.cpp.
#include "cachediskstorage.h"
#include "cachemanager.h"

#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcCacheManager)

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
      }
      // Opportunistic timestamp GC piggybacks on this walk thread so it
      // never runs on the GUI thread (Kartend-0ldg2). getCacheSize() is
      // conceptually a read — the const signature comes from ICacheManager —
      // but the maintenance pass mutates bookkeeping this object owns, so
      // cast the const away here rather than poisoning the maps with
      // `mutable` (precedent: databasemanager_items.cpp).
      const_cast<CacheManager *>(this)->pruneStaleEntries();
      {
        // Release the single-flight flag only after pruneStaleEntries() so
        // the flag covers the whole task: releasing it earlier would let a
        // scroll-driven getCacheSize() redispatch and overwrite
        // m_cacheSizeWalkFuture while this lambda still runs, leaving
        // ~CacheManager waiting on the wrong future.
        QMutexLocker locker(&m_diskCacheSizeMutex);
        m_lastDiskWalkMs = QDateTime::currentMSecsSinceEpoch();
        m_diskWalkInFlight = false;
        // NOLINTEND(clang-analyzer-core.NullDereference)
      }
    });
  }

  return result;
}

// Opportunistic GC for the timestamp store + in-memory bookkeeping
// (Kartend-0ldg2). Runs on the background getCacheSize() walk thread (the
// walk lambda holds the single-flight flag until this pass returns, so
// passes never overlap and the walk future is never replaced while live;
// ~CacheManager waits on the walk future, so the pass can't outlive the
// object). Each pass is bounded:
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
