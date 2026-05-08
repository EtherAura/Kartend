// Manages in-memory pixmap cache with LRU eviction and optional disk
// persistence.
#include "cachemanager.h"
#include "errorutils.h"
#include "uiconstants.h"

#include <limits>
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>

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

CacheManager::CacheManager() {
  constexpr qint64 BYTES_PER_KB = 1024;
  const qint64 maxBytes = static_cast<qint64>(UIConstants::Cache::PIXMAP_CACHE_KB) * BYTES_PER_KB;
  const qint64 maxInt = static_cast<qint64>(std::numeric_limits<int>::max());
  artworkCache.setMaxCost(maxBytes > maxInt ? std::numeric_limits<int>::max()
                                            : static_cast<int>(maxBytes));

  m_ioThreadPool = new QThreadPool();
  m_ioThreadPool->setMaxThreadCount(1);

  // Timer context lives with CacheManager lifetime.
  // CacheManager is created on the main thread
  // (ApplicationManager::initialize).
  m_timerContext = new QObject();
  m_debouncedSaveTimer = new QTimer(m_timerContext);
  m_debouncedSaveTimer->setSingleShot(true);
  QObject::connect(m_debouncedSaveTimer, &QTimer::timeout, m_timerContext, [this]() {
    if (QApplication::closingDown()) {
      return;
    }
    if (m_cancelIo->load(std::memory_order_acquire)) {
      return;
    }
    saveToDisk();
  });
}

CacheManager::~CacheManager() {
  m_cancelIo->store(true, std::memory_order_release);

  // Cooperative shutdown: signal cancel, drop queued tasks, then wait
  // briefly for any in-flight task to notice the flag (the disk-I/O
  // lambda checks cancelFlag at the start and inside its inner loop,
  // so it returns within milliseconds in the common case). If it
  // doesn't drain in time, fall back to the abandon-the-pool path
  // rather than blocking shutdown.
  if (m_ioThreadPool) {
    m_ioThreadPool->clear();
    constexpr int kShutdownDrainMs = 2000;
    if (m_ioThreadPool->waitForDone(kShutdownDrainMs)) {
      delete m_ioThreadPool;
    } else {
      qCWarning(lcCacheManager) << "CacheManager: I/O thread pool did not drain in"
                                << kShutdownDrainMs
                                << "ms during shutdown; abandoning pool to avoid blocking exit";
      // Intentional leak: deleting now would call ~QThreadPool which
      // blocks until all running tasks finish. The OS reaps memory at
      // process exit. The .lsan_suppressions.txt entry covers this
      // path; remove the suppression once the slow path is no longer
      // possible (e.g. all cache I/O made strictly cancellable).
    }
    m_ioThreadPool = nullptr;
  }

  if (m_debouncedSaveTimer) {
    m_debouncedSaveTimer->stop();
  }
  delete m_timerContext;
  m_timerContext = nullptr;
  m_debouncedSaveTimer = nullptr;
}

auto CacheManager::snapshotTimestampsForShutdown() const -> QHash<QString, qint64> {
  QMutexLocker locker(&m_mutex);
  return fileTimestamps;
}

void CacheManager::saveTimestampsSnapshotToDiskForShutdown(
    const QHash<QString, qint64> &timestampsCopy) {
  // Write-only operation: safe to call from a worker thread.
  // On shutdown, write all timestamps (full save, not incremental).
  QString metadataPath = getCacheDirectory() + QStringLiteral("/metadata/artwork_cache.json");
  writeTimestamps(timestampsCopy, metadataPath);
}

void CacheManager::cancelPendingIo() {
  // Signal cancellation to any in-flight tasks
  m_cancelIo->store(true, std::memory_order_release);

  // Stop the debounced save timer to prevent new tasks from starting
  if (m_debouncedSaveTimer) {
    m_debouncedSaveTimer->stop();
  }

  // Clear queued tasks but DON'T wait for running ones - they check the
  // cancellation flag and will exit quickly. Blocking here can cause
  // multi-minute shutdown delays when a large image write batch is in progress.
  if (m_ioThreadPool) {
    m_ioThreadPool->clear();
  }
  // Note: We intentionally skip waitForDone() to avoid blocking shutdown.
  // The in-flight task will see m_cancelIo and stop writing images.
}

// Releases GUI resources and resets in-memory accounting totals
void CacheManager::releaseGuiResources() {
  QMutexLocker locker(&m_mutex);
  artworkCache.clear();
  dirtyArtwork.clear();
}

// Returns cache directory path, creating subdirs if needed
auto CacheManager::getCacheDirectory() -> QString {
  QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                     QStringLiteral("/kartend");
  QDir dir(cacheDir);
  const QString artworkDirPath = cacheDir + QStringLiteral("/artwork");
  const QString metadataDirPath = cacheDir + QStringLiteral("/metadata");
  static bool loggedMkpathFailure = false;

  if (!dir.exists("artwork") && !dir.mkpath(artworkDirPath)) {
    if (!loggedMkpathFailure) {
      loggedMkpathFailure = true;
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                            "Failed to create artwork cache directory",
                                            "CacheManager::getCacheDirectory")
              .withDetails(QString("Path: %1").arg(artworkDirPath)));
    }
  }
  if (!dir.exists("metadata") && !dir.mkpath(metadataDirPath)) {
    if (!loggedMkpathFailure) {
      loggedMkpathFailure = true;
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                            "Failed to create cache metadata directory",
                                            "CacheManager::getCacheDirectory")
              .withDetails(QString("Path: %1").arg(metadataDirPath)));
    }
  }
  return cacheDir;
}

// Returns on-disk cache path for a given artwork file path
auto CacheManager::getArtworkCachePath(const QString &artworkPath) -> QString {
  QByteArray hash = QCryptographicHash::hash(artworkPath.toUtf8(), QCryptographicHash::Md5);
  return CacheManager::getCacheDirectory() + QStringLiteral("/artwork/") + hash.toHex() +
         QStringLiteral(".png");
}

// Processes timestamps section from JSON
auto CacheManager::readTimestamps(const QJsonObject &root) -> void {
  QJsonObject timestamps = root["timestamps"].toObject();
  for (auto it = timestamps.begin(); it != timestamps.end(); ++it) {
    QDateTime dateTime = QDateTime::fromString(it.value().toString(), Qt::ISODate);
    fileTimestamps[it.key()] = dateTime.isValid() ? dateTime.toMSecsSinceEpoch() : 0;
  }
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
      QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
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

  QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
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
      const QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
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
      const QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
      QFile::remove(cachePath);
      return {};
    }
  }

  const QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
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

// Clears all cached data for a particular collection (currently clears all) and
// resets memory accounting
void CacheManager::clearCollectionCache(int collectionIndex) {
  Q_UNUSED(collectionIndex)
  QMutexLocker locker(&m_mutex);
  artworkCache.clear();
  dirtyArtwork.clear();
}

// Computes total size of cache directory on disk
auto CacheManager::getCacheSize() -> qint64 {
  QDir cacheDir(getCacheDirectory());
  qint64 totalSize = 0;
  QDirIterator dirIt(cacheDir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
  while (dirIt.hasNext()) {
    dirIt.next();
    totalSize += dirIt.fileInfo().size();
  }
  return totalSize;
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
