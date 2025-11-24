#include "cachemanager.h"
#include "uiconstants.h"


#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDateTime>

CacheManager::CacheManager() = default;

// Releases GUI resources and resets in-memory accounting totals
void CacheManager::releaseGuiResources() {
  QMutexLocker locker(&m_mutex);

  QHash<QString, QPixmap>::iterator cacheIt = artworkCache.begin();
  while (cacheIt != artworkCache.end()) {
    cacheIt.value() = QPixmap();
    ++cacheIt;
  }

  artworkCache.clear();
  dirtyArtwork.clear();
  m_totalPixmapBytes = 0;
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
  QFile metadataFile(metadataPath);
  if (metadataFile.open(QIODevice::WriteOnly)) {
    metadataFile.write(QJsonDocument(root).toJson());
    metadataFile.close();
  }
}
// Flushes dirty artwork pixmaps to the on-disk cache.
void CacheManager::flushDirtyArtwork(
    const QList<QPair<QString, QPixmap>> &dirtyList) {
  for (const auto &entry : dirtyList) {
    if (QApplication::closingDown()) {
      break;
    }
    const QString &artworkPath = entry.first;
    const QPixmap &pixmap = entry.second;
    if (pixmap.isNull()) {
      continue;
    }
    QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
    QDir().mkpath(QFileInfo(cachePath).absolutePath());
    pixmap.save(cachePath, "PNG");
  }
}
// Saves persistent cache to disk with canonical hierarchical keys and without
// leaf aliases
void CacheManager::saveToDisk() {
  if (QApplication::closingDown()) {
    return;
  }

  QHash<QString, qint64> timestampsCopy;
  QList<QPair<QString, QPixmap>> dirtyList;

  {
    QMutexLocker locker(&m_mutex);
    timestampsCopy = fileTimestamps;
    for (const QString &path : std::as_const(dirtyArtwork)) {
      auto cacheIterator = artworkCache.constFind(path);
      if (cacheIterator != artworkCache.constEnd()) {
        dirtyList.append(qMakePair(path, cacheIterator.value()));
      }
    }
    dirtyArtwork.clear();
  }

  writeTimestamps(timestampsCopy);
  flushDirtyArtwork(dirtyList);
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
  if (artworkCache.contains(artworkPath)) {
    if (fileInfo.exists() && fileTimestamps.contains(artworkPath) &&
        fileTimestamps[artworkPath] !=
            fileInfo.lastModified().toMSecsSinceEpoch()) {
      QString cachePath = CacheManager::getArtworkCachePath(artworkPath);
      QFile::remove(cachePath);
      artworkCache.remove(artworkPath);
      fileTimestamps.remove(artworkPath);
    } else {
      return artworkCache[artworkPath];
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
      QMutexLocker relocker(&m_mutex);
      artworkCache[artworkPath] = cachedPixmap;
      if (fileInfo.exists()) {
        fileTimestamps[artworkPath] =
            fileInfo.lastModified().toMSecsSinceEpoch();
      }
      return cachedPixmap;
    }
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
  if (pixmap.width() < UIConstants::MIN_PIXMAP_SIZE ||
      pixmap.height() < UIConstants::MIN_PIXMAP_SIZE) {
    return;
  }

  constexpr int DEFAULT_BITS_PER_PIXEL = 32;
  constexpr int BITS_PER_BYTE = 8;
  auto approximatePixmapBytes = [](const QPixmap &pixmap) -> quint64 {
    int bitsPerPixel =
        pixmap.depth() > 0 ? pixmap.depth() : DEFAULT_BITS_PER_PIXEL;
    return static_cast<quint64>(pixmap.width()) *
           static_cast<quint64>(pixmap.height()) *
           static_cast<quint64>(bitsPerPixel) / BITS_PER_BYTE;
  };

  const quint64 byteLimit =
      static_cast<quint64>(UIConstants::PIXMAP_CACHE_KB) * 1024;

  QMutexLocker locker(&m_mutex);
  if (QApplication::closingDown()) {
    return;
  }

  // If replacing an existing entry, subtract its previous size from the running
  // total
  if (artworkCache.contains(artworkPath)) {
    auto previousIt = artworkCache.find(artworkPath);
    m_totalPixmapBytes -= approximatePixmapBytes(previousIt.value());
  }

  // Insert/update the pixmap and adjust running total
  artworkCache[artworkPath] = pixmap;
  QFileInfo fileInfo(artworkPath);
  if (fileInfo.exists()) {
    fileTimestamps[artworkPath] = fileInfo.lastModified().toMSecsSinceEpoch();
  }
  dirtyArtwork.insert(artworkPath);

  m_totalPixmapBytes += approximatePixmapBytes(pixmap);

  // Evict arbitrary entries until within limit (preserves existing non-LRU
  // behavior)
  if (byteLimit > 0 && m_totalPixmapBytes > byteLimit) {
    QList<QString> keys = artworkCache.keys();
    int keyIndex = 0;
    while (m_totalPixmapBytes > byteLimit && keyIndex < keys.size()) {
      const QString &key = keys[keyIndex++];
      if (!artworkCache.contains(key)) {
        continue;
      }
      auto iterator = artworkCache.find(key);
      quint64 evictedBytes = approximatePixmapBytes(iterator.value());
      fileTimestamps.remove(key);
      dirtyArtwork.remove(key);
      artworkCache.erase(iterator);
      m_totalPixmapBytes = (m_totalPixmapBytes >= evictedBytes)
                               ? (m_totalPixmapBytes - evictedBytes)
                               : 0;
    }
    m_totalPixmapBytes = qMin(m_totalPixmapBytes, byteLimit);
  }
}

// Clears all cached data for a particular collection (currently clears all) and
// resets memory accounting
void CacheManager::clearCollectionCache(int collectionIndex) {
  Q_UNUSED(collectionIndex)
  QMutexLocker locker(&m_mutex);

  QStringList artworkKeysToRemove;
  for (auto cacheIt = artworkCache.begin(); cacheIt != artworkCache.end();
       ++cacheIt) {
    artworkKeysToRemove.append(cacheIt.key());
  }
  for (const QString &key : artworkKeysToRemove) {
    artworkCache.remove(key);
    dirtyArtwork.remove(key);
  }
  m_totalPixmapBytes = 0;
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
