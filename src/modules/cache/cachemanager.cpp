// Manages in-memory pixmap cache with LRU eviction and optional disk persistence.
#include "cachemanager.h"
#include "errorutils.h"
#include "uiconstants.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QDateTime>
#include <QElapsedTimer>
#include <limits>

namespace {

[[nodiscard]] auto clampToCacheCostBytes(const QPixmap &pixmap) -> int {
  constexpr int DEFAULT_BITS_PER_PIXEL = 32;
  constexpr quint64 BITS_PER_BYTE = 8;

  const int bitsPerPixel = pixmap.depth() > 0 ? pixmap.depth() : DEFAULT_BITS_PER_PIXEL;
  const quint64 pixels = static_cast<quint64>(pixmap.width()) * static_cast<quint64>(pixmap.height());
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
#define debugLog(msg) do { if (lcCacheManager().isDebugEnabled()) { qCDebug(lcCacheManager) << msg; } } while (0)

CacheManager::CacheManager() {
  constexpr qint64 BYTES_PER_KB = 1024;
  const qint64 maxBytes = static_cast<qint64>(UIConstants::Cache::PIXMAP_CACHE_KB) * BYTES_PER_KB;
  const qint64 maxInt = static_cast<qint64>(std::numeric_limits<int>::max());
  artworkCache.setMaxCost(maxBytes > maxInt ? std::numeric_limits<int>::max() : static_cast<int>(maxBytes));

  m_ioThreadPool = new QThreadPool();
  m_ioThreadPool->setMaxThreadCount(1);

  // Timer context lives with CacheManager lifetime.
  // CacheManager is created on the main thread (ApplicationManager::initialize).
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
  
  // Clear queued tasks but DON'T delete the pool - that would block waiting
  // for running tasks. Just abandon it; the process is exiting anyway.
  if (m_ioThreadPool) {
    m_ioThreadPool->clear();
    // Intentionally NOT deleting m_ioThreadPool - ~QThreadPool blocks.
    // The OS will clean up when the process exits.
    m_ioThreadPool = nullptr;
  }

  if (m_debouncedSaveTimer) {
    m_debouncedSaveTimer->stop();
  }
  delete m_timerContext;
  m_timerContext = nullptr;
  m_debouncedSaveTimer = nullptr;
}

auto CacheManager::snapshotTimestampsForShutdown() const
    -> QHash<QString, qint64> {
  QMutexLocker locker(&m_mutex);
  return fileTimestamps;
}

void CacheManager::saveTimestampsSnapshotToDiskForShutdown(
    const QHash<QString, qint64> &timestampsCopy) {
  // Write-only operation: safe to call from a worker thread.
  writeTimestamps(timestampsCopy);
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
  QString cacheDir =
      QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
      "/kartend";
  QDir dir(cacheDir);
  const QString artworkDirPath = cacheDir + "/artwork";
  const QString metadataDirPath = cacheDir + "/metadata";
  static bool loggedMkpathFailure = false;

  if (!dir.exists("artwork") && !dir.mkpath(artworkDirPath)) {
    if (!loggedMkpathFailure) {
      loggedMkpathFailure = true;
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(
              ErrorUtils::ErrorCode::FileWriteError,
              "Failed to create artwork cache directory",
              "CacheManager::getCacheDirectory")
              .withDetails(QString("Path: %1").arg(artworkDirPath)));
    }
  }
  if (!dir.exists("metadata") && !dir.mkpath(metadataDirPath)) {
    if (!loggedMkpathFailure) {
      loggedMkpathFailure = true;
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(
              ErrorUtils::ErrorCode::FileWriteError,
              "Failed to create cache metadata directory",
              "CacheManager::getCacheDirectory")
              .withDetails(QString("Path: %1").arg(metadataDirPath)));
    }
  }
  return cacheDir;
}

// Returns on-disk cache path for a given artwork file path
auto CacheManager::getArtworkCachePath(const QString &artworkPath)
    -> QString {
  QByteArray hash =
      QCryptographicHash::hash(artworkPath.toUtf8(), QCryptographicHash::Md5);
  return CacheManager::getCacheDirectory() + "/artwork/" + hash.toHex() +
         ".png";
}

// Processes timestamps section from JSON
auto CacheManager::readTimestamps(const QJsonObject &root) -> void {
  QJsonObject timestamps = root["timestamps"].toObject();
  for (auto it = timestamps.begin(); it != timestamps.end(); ++it) {
    QDateTime dateTime =
        QDateTime::fromString(it.value().toString(), Qt::ISODate);
    fileTimestamps[it.key()] =
        dateTime.isValid() ? dateTime.toMSecsSinceEpoch() : 0;
  }
}

// Initializes persistent cache metadata from disk
void CacheManager::initialize() {
  QMutexLocker locker(&m_mutex);

  QString metadataPath = getCacheDirectory() + "/metadata/artwork_cache.json";
  QFile metadataFile(metadataPath);
  if (!metadataFile.open(QIODevice::ReadOnly)) {
    return;
  }

  QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll());
  QJsonObject root = doc.object();

  fileTimestamps.clear();
  readTimestamps(root);

  metadataFile.close();
}

void CacheManager::scheduleSaveToDisk(int delayMs) {
  if (QApplication::closingDown()) {
    return;
  }
  if (m_cancelIo->load(std::memory_order_acquire)) {
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

  // Ensure the timer is started on its owning thread.
  QMetaObject::invokeMethod(
      m_timerContext,
      [this, effectiveDelay]() {
        if (m_debouncedSaveTimer) {
          m_debouncedSaveTimer->start(effectiveDelay);
        }
      },
      Qt::QueuedConnection);
}

// Writes metadata JSON (collections/global/timestamps) to disk.
void CacheManager::writeTimestamps(const QHash<QString, qint64> &timestampsCopy) {
  QJsonObject root;
  QJsonObject timestamps;
  for (auto timestampIt = timestampsCopy.begin();
       timestampIt != timestampsCopy.end(); ++timestampIt) {
    timestamps[timestampIt.key()] =
        QDateTime::fromMSecsSinceEpoch(timestampIt.value())
            .toString(Qt::ISODate);
  }
  root["timestamps"] = timestamps;

  QString metadataPath = getCacheDirectory() + "/metadata/artwork_cache.json";
  const QString parentDir = QFileInfo(metadataPath).absolutePath();
  if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(
            ErrorUtils::ErrorCode::FileWriteError,
            "Failed to create cache metadata directory",
            "CacheManager::writeTimestamps")
            .withDetails(QString("Path: %1").arg(parentDir)));
    return;
  }

  const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
  QSaveFile metadataFile(metadataPath);
  if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(
            ErrorUtils::ErrorCode::FileWriteError,
            "Failed to open cache metadata for writing",
            "CacheManager::writeTimestamps")
            .withDetails(QString("Path: %1, Error: %2")
                             .arg(metadataPath, metadataFile.errorString())));
    return;
  }

  const qint64 written = metadataFile.write(payload);
  if (written != payload.size()) {
    metadataFile.cancelWriting();
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(
            ErrorUtils::ErrorCode::FileWriteError,
            "Failed to write complete cache metadata payload",
            "CacheManager::writeTimestamps")
            .withDetails(QString("Path: %1, Written: %2, Expected: %3, Error: %4")
                             .arg(metadataPath)
                             .arg(written)
                             .arg(payload.size())
                             .arg(metadataFile.errorString())));
    return;
  }

  if (!metadataFile.commit()) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(
            ErrorUtils::ErrorCode::FileWriteError,
            "Failed to atomically commit cache metadata",
            "CacheManager::writeTimestamps")
            .withDetails(QString("Path: %1, Error: %2")
                             .arg(metadataPath, metadataFile.errorString())));
  }
}
// Flushes dirty artwork pixmaps to the on-disk cache.
void CacheManager::flushDirtyArtwork(
    const QList<QPair<QString, QImage>> &dirtyList) {
  for (const auto &entry : dirtyList) {
    if (QApplication::closingDown()) {
      break;
    }
    const QString &artworkPath = entry.first;
    const QImage &image = entry.second;
    if (image.isNull()) {
      continue;
    }
    QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
    const QString parentDir = QFileInfo(cachePath).absolutePath();
    if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(
              ErrorUtils::ErrorCode::FileWriteError,
              "Failed to create artwork cache directory",
              "CacheManager::flushDirtyArtwork")
              .withDetails(QString("Path: %1").arg(parentDir)));
      continue;
    }
    if (!image.save(cachePath, "PNG")) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(
              ErrorUtils::ErrorCode::FileWriteError,
              "Failed to persist artwork PNG to cache",
              "CacheManager::flushDirtyArtwork")
              .withDetails(QString("Path: %1").arg(cachePath)));
    }
  }
}
// Saves persistent cache to disk with canonical hierarchical keys and without
// leaf aliases
void CacheManager::saveToDisk() {
  if (QApplication::closingDown()) {
    return;
  }
  if (m_cancelIo->load(std::memory_order_acquire)) {
    return;
  }

  bool shouldWriteMetadata = false;
  QHash<QString, qint64> timestampsCopy;
  QList<QPair<QString, QPixmap>> dirtyPixmaps;

  {
    QMutexLocker locker(&m_mutex);
    shouldWriteMetadata = m_metadataDirty;
    if (shouldWriteMetadata) {
      timestampsCopy = fileTimestamps;
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

  // If nothing changed, avoid writing metadata and spawning I/O tasks.
  if (!shouldWriteMetadata && dirtyPixmaps.isEmpty()) {
    return;
  }

  QList<QPair<QString, QImage>> dirtyImages;
  dirtyImages.reserve(dirtyPixmaps.size());
  for (const auto &entry : dirtyPixmaps) {
    dirtyImages.append(qMakePair(entry.first, entry.second.toImage()));
  }

  // Capture shared_ptr to cancellation flag so lambda can safely check it
  // even after CacheManager is destroyed (QThreadPool destructor waits).
  auto cancelFlag = m_cancelIo;

  // Offload PNG encoding and disk writes to avoid UI hitches.
  // Dedicated pool keeps flushes sequential and reduces contention.
  if (!m_ioThreadPool) {
    return;
  }
  m_ioThreadPool->start([cancelFlag, shouldWriteMetadata, timestampsCopy, dirtyImages]() {
    if (cancelFlag->load(std::memory_order_acquire) || QApplication::closingDown()) {
      return;
    }

    QElapsedTimer timer;
    timer.start();

    if (shouldWriteMetadata) {
      CacheManager::writeTimestamps(timestampsCopy);
    }

    for (const auto &entry : dirtyImages) {
      if (cancelFlag->load(std::memory_order_acquire) || QApplication::closingDown()) {
        break;
      }

      const QString &artworkPath = entry.first;
      const QImage &image = entry.second;
      if (image.isNull()) {
        continue;
      }

      const QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
      const QString parentDir = QFileInfo(cachePath).absolutePath();
      if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::warning(
                ErrorUtils::ErrorCode::FileWriteError,
                "Failed to create artwork cache directory",
                "CacheManager::saveToDisk")
                .withDetails(QString("Path: %1").arg(parentDir)));
        continue;
      }
      if (!image.save(cachePath, "PNG")) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::warning(
                ErrorUtils::ErrorCode::FileWriteError,
                "Failed to persist artwork PNG to cache",
                "CacheManager::saveToDisk")
                .withDetails(QString("Path: %1").arg(cachePath)));
      }
    }

    if (lcCacheManager().isDebugEnabled()) {
      qCDebug(lcCacheManager) << "CacheManager saveToDisk flushed"
                              << "metadata=" << (shouldWriteMetadata ? "yes" : "no")
                              << "images=" << dirtyImages.size()
                              << "elapsedMs=" << timer.elapsed();
    }
  });
}

// Saves cache metadata to disk during shutdown - skips QApplication::closingDown
// check since we're intentionally saving during app close
void CacheManager::saveToDiskForShutdown() {
  // Cancel any queued or in-flight asynchronous cache flushes to avoid
  // out-of-order writes overwriting the final shutdown snapshot.
  m_cancelIo->store(true, std::memory_order_release);
  if (m_ioThreadPool) {
    m_ioThreadPool->clear();
    // DON'T wait - just abandon the pool. Process is exiting anyway.
  }

  QHash<QString, qint64> timestampsCopy;
  {
    QMutexLocker locker(&m_mutex);
    timestampsCopy = fileTimestamps;
    // Skip dirty artwork flush during shutdown - pixmaps may be invalidated
    // and the flush is expensive. Timestamps are the critical metadata.
    dirtyArtwork.clear();
  }

  writeTimestamps(timestampsCopy);
}

// Returns artwork pixmap if valid and up-to-date; otherwise attempts load from
// disk cache
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
        fileTimestamps[artworkPath] !=
            fileInfo.lastModified().toMSecsSinceEpoch()) {
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

      artworkCache.insert(artworkPath, new QPixmap(cachedPixmap), clampToCacheCostBytes(cachedPixmap));
      if (fileInfo.exists()) {
        fileTimestamps[artworkPath] =
            fileInfo.lastModified().toMSecsSinceEpoch();
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

  // If we have a known timestamp and it no longer matches, invalidate the disk entry.
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
void CacheManager::cacheArtwork(const QString &artworkPath,
                                   const QPixmap &pixmap) {
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
  }
  dirtyArtwork.insert(artworkPath);
  m_metadataDirty = true;
  ++m_metrics.inserts;

  locker.unlock();
  scheduleSaveToDisk();
}

void CacheManager::cacheArtworkInMemoryOnly(const QString &artworkPath,
                                           const QPixmap &pixmap) {
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
  QDirIterator dirIt(cacheDir.absolutePath(), QDir::Files,
                     QDirIterator::Subdirectories);
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
                          << "memHits=" << m.memoryHits
                          << "diskHits=" << m.diskHits
                          << "misses=" << m.misses
                          << "inserts=" << m.inserts
                          << "evictions=" << m.evictions
                          << "invalidations=" << m.invalidations
                          << "memHitRate=" << QString::number(m.memoryHitRate() * 100, 'f', 1) << "%"
                          << "totalHitRate=" << QString::number(m.totalHitRate() * 100, 'f', 1) << "%";
}
