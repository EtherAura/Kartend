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

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcCacheManager, "kartend.cachemanager")
#define debugLog(msg) qCDebug(lcCacheManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

CacheManager::CacheManager() {
  constexpr qint64 BYTES_PER_KB = 1024;
  const qint64 maxBytes = static_cast<qint64>(UIConstants::Cache::PIXMAP_CACHE_KB) * BYTES_PER_KB;
  const qint64 maxInt = static_cast<qint64>(std::numeric_limits<int>::max());
  artworkCache.setMaxCost(maxBytes > maxInt ? std::numeric_limits<int>::max() : static_cast<int>(maxBytes));

  m_ioThreadPool.setMaxThreadCount(1);
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
  if (!dir.exists()) {
    dir.mkpath(cacheDir + "/artwork");
    dir.mkpath(cacheDir + "/metadata");
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
  QDir().mkpath(QFileInfo(metadataPath).absolutePath());

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
    QDir().mkpath(QFileInfo(cachePath).absolutePath());
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
  if (m_cancelIo.load(std::memory_order_acquire)) {
    return;
  }

  QHash<QString, qint64> timestampsCopy;
  QList<QPair<QString, QPixmap>> dirtyPixmaps;

  {
    QMutexLocker locker(&m_mutex);
    timestampsCopy = fileTimestamps;
    for (const QString &path : std::as_const(dirtyArtwork)) {
      if (QPixmap *pix = artworkCache.object(path)) {
        dirtyPixmaps.append(qMakePair(path, *pix));
      }
    }
    dirtyArtwork.clear();
  }

  QList<QPair<QString, QImage>> dirtyImages;
  dirtyImages.reserve(dirtyPixmaps.size());
  for (const auto &entry : dirtyPixmaps) {
    dirtyImages.append(qMakePair(entry.first, entry.second.toImage()));
  }

  // Offload PNG encoding and disk writes to avoid UI hitches.
  // Dedicated pool keeps flushes sequential and reduces contention.
  m_ioThreadPool.start([this, timestampsCopy, dirtyImages]() {
    if (m_cancelIo.load(std::memory_order_acquire) || QApplication::closingDown()) {
      return;
    }
    CacheManager::writeTimestamps(timestampsCopy);

    for (const auto &entry : dirtyImages) {
      if (m_cancelIo.load(std::memory_order_acquire) || QApplication::closingDown()) {
        break;
      }

      const QString &artworkPath = entry.first;
      const QImage &image = entry.second;
      if (image.isNull()) {
        continue;
      }

      const QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
      QDir().mkpath(QFileInfo(cachePath).absolutePath());
      if (!image.save(cachePath, "PNG")) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::warning(
                ErrorUtils::ErrorCode::FileWriteError,
                "Failed to persist artwork PNG to cache",
                "CacheManager::saveToDisk")
                .withDetails(QString("Path: %1").arg(cachePath)));
      }
    }
  });
}

// Saves cache metadata to disk during shutdown - skips QApplication::closingDown
// check since we're intentionally saving during app close
void CacheManager::saveToDiskForShutdown() {
  // Cancel any queued or in-flight asynchronous cache flushes to avoid
  // out-of-order writes overwriting the final shutdown snapshot.
  m_cancelIo.store(true, std::memory_order_release);
  m_ioThreadPool.clear();
  m_ioThreadPool.waitForDone();

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
#ifdef KARTEND_DEBUG_LOGGING
  CacheMetrics m = metrics();
  qDebug() << "CacheManager metrics:"
           << "memHits=" << m.memoryHits
           << "diskHits=" << m.diskHits
           << "misses=" << m.misses
           << "inserts=" << m.inserts
           << "evictions=" << m.evictions
           << "invalidations=" << m.invalidations
           << "memHitRate=" << QString::number(m.memoryHitRate() * 100, 'f', 1) << "%"
           << "totalHitRate=" << QString::number(m.totalHitRate() * 100, 'f', 1) << "%";
#endif
}
