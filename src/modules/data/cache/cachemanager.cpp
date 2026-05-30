// Manages in-memory pixmap cache with LRU eviction and optional disk
// persistence.
#include "cachemanager.h"
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
  // Sever the timer→lambda connection FIRST. The lambda captures 'this' and
  // calls saveToDisk(); if it fired later in the destructor (e.g. while the
  // I/O pool drain spins the event loop), it would run against a
  // half-destructed CacheManager.
  if (m_debouncedSaveTimer) {
    m_debouncedSaveTimer->stop();
  }
  delete m_timerContext;
  m_timerContext = nullptr;

  // Cooperative shutdown: cancel + bounded drain. The disk-I/O lambda
  // captured the cancellation token by shared_ptr value, so in-flight
  // tasks return within milliseconds in the common case. If they don't
  // drain within the budget, CacheDiskStorage abandons the pool (the
  // OS reaps at process exit; tests/suppressions/lsan.txt covers this path).
  m_diskStorage->cancel();
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

auto CacheManager::snapshotTimestampsForShutdown() const -> QHash<QString, qint64> {
  QMutexLocker locker(&m_mutex);
  return fileTimestamps;
}

void CacheManager::saveTimestampsSnapshotToDiskForShutdown(
    const QHash<QString, qint64> &timestampsCopy) {
  // Write-only operation: safe to call from a worker thread. On shutdown,
  // write all timestamps (full save, not incremental).
  CacheDiskStorage::writeTimestamps(timestampsCopy);
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

// Releases GUI resources and resets in-memory accounting totals
void CacheManager::releaseGuiResources() {
  QMutexLocker locker(&m_mutex);
  artworkCache.clear();
  dirtyArtwork.clear();
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

// Initializes persistent cache metadata from disk
auto CacheManager::getArtwork(const QString &artworkPath) -> QPixmap {
  if (artworkPath.isEmpty()) {
    return {};
  }
  if (QApplication::closingDown()) {
    return {};
  }

  QMutexLocker locker(&m_mutex);
  QFileInfo fileInfo(artworkPath);
  if (QPixmap *pix = artworkCache.object(artworkPath)) {
    if (fileInfo.exists() && fileTimestamps.contains(artworkPath) &&
        fileTimestamps[artworkPath] != fileInfo.lastModified().toMSecsSinceEpoch()) {
      // Cache invalidation - file changed on disk
      QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
      QFile::remove(cachePath);
      artworkCache.remove(artworkPath);
      fileTimestamps.remove(artworkPath);
      m_metadataDirty = true;
      ++m_metrics.invalidations;
    } else {
      // Memory cache hit
      ++m_metrics.memoryHits;
      return *pix;
    }
  }
  locker.unlock();

  if (QApplication::closingDown()) {
    return {};
  }

  QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
  if (QFile::exists(cachePath)) {
    QPixmap cachedPixmap(cachePath);
    if (!cachedPixmap.isNull()) {
      // Disk cache hit
      // Set device pixel ratio for HiDPI displays
      qreal dpr = 1.0;
      if (QGuiApplication::primaryScreen()) {
        dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
      }
      cachedPixmap.setDevicePixelRatio(dpr);

      QMutexLocker relocker(&m_mutex);
      ++m_metrics.diskHits;

      artworkCache.insert(artworkPath, new QPixmap(cachedPixmap),
                          clampToCacheCostBytes(cachedPixmap));
      if (fileInfo.exists()) {
        fileTimestamps[artworkPath] = fileInfo.lastModified().toMSecsSinceEpoch();
      }
      return cachedPixmap;
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
  if (artworkPath.isEmpty()) {
    return {};
  }
  if (QApplication::closingDown()) {
    return {};
  }

  QFileInfo fileInfo(artworkPath);
  QMutexLocker locker(&m_mutex);

  if (QPixmap *pix = artworkCache.object(artworkPath)) {
    if (fileInfo.exists() && fileTimestamps.contains(artworkPath) &&
        fileTimestamps[artworkPath] != fileInfo.lastModified().toMSecsSinceEpoch()) {
      // Cache invalidation - file changed on disk.
      // Note: This may touch the filesystem, but avoids large image reads.
      const QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
      QFile::remove(cachePath);
      artworkCache.remove(artworkPath);
      fileTimestamps.remove(artworkPath);
      dirtyArtwork.remove(artworkPath);
      m_metadataDirty = true;
      ++m_metrics.invalidations;
    } else {
      ++m_metrics.memoryHits;
      return *pix;
    }
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
  {
    QMutexLocker locker(&m_mutex);
    auto it = fileTimestamps.constFind(artworkPath);
    if (it != fileTimestamps.constEnd() && it.value() != currentTimestamp) {
      fileTimestamps.remove(artworkPath);
      dirtyArtwork.remove(artworkPath);
      m_metadataDirty = true;
      ++m_metrics.invalidations;

      locker.unlock();
      const QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
      QFile::remove(cachePath);
      return {};
    }
  }

  const QString cachePath = CacheDiskStorage::artworkCachePath(artworkPath);
  if (!QFile::exists(cachePath)) {
    QMutexLocker locker(&m_mutex);
    ++m_metrics.misses;
    return {};
  }

  QImage cachedImage(cachePath);
  if (cachedImage.isNull()) {
    QMutexLocker locker(&m_mutex);
    ++m_metrics.misses;
    return {};
  }

  {
    QMutexLocker locker(&m_mutex);
    ++m_metrics.diskHits;
    fileTimestamps[artworkPath] = currentTimestamp;
  }

  return cachedImage;
}

// Caches artwork pixmap if large enough and maintains O(1) running total for
// memory accounting; evicts until under limit
void CacheManager::cacheArtwork(const QString &artworkPath, const QPixmap &pixmap) {
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

  QMutexLocker locker(&m_mutex);
  if (QApplication::closingDown()) {
    return;
  }

  // Insert/update the pixmap and adjust running total
  artworkCache.insert(artworkPath, new QPixmap(pixmap), cost);
  QFileInfo fileInfo(artworkPath);
  if (fileInfo.exists()) {
    fileTimestamps[artworkPath] = fileInfo.lastModified().toMSecsSinceEpoch();
    dirtyTimestamps.insert(artworkPath); // Track which timestamps are new/changed
  }
  dirtyArtwork.insert(artworkPath);
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

  QMutexLocker locker(&m_mutex);
  if (QApplication::closingDown()) {
    return;
  }

  artworkCache.insert(artworkPath, new QPixmap(pixmap), cost);
  QFileInfo fileInfo(artworkPath);
  if (fileInfo.exists()) {
    fileTimestamps[artworkPath] = fileInfo.lastModified().toMSecsSinceEpoch();
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
      qint64 totalSize = 0;
      QDirIterator dirIt(cacheDirPath, QDir::Files, QDirIterator::Subdirectories);
      while (dirIt.hasNext()) {
        dirIt.next();
        totalSize += dirIt.fileInfo().size();
      }
      // NOLINTBEGIN(clang-analyzer-core.NullDereference) — analyzer can't
      // see that ~CacheManager waits on m_cacheSizeWalkFuture, so the
      // lambda never outlives `this`. The captured pointer is safe.
      QMutexLocker locker(&m_diskCacheSizeMutex);
      m_cachedDiskCacheSize = totalSize;
      m_lastDiskWalkMs = QDateTime::currentMSecsSinceEpoch();
      m_diskWalkInFlight = false;
      // NOLINTEND(clang-analyzer-core.NullDereference)
    });
  }

  return result;
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
