// Sibling TU: disk-persistence methods for CacheManager.
#include "cachemanager.h"
#include "errorutils.h"
#include "pathutils.h"
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

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcCacheManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcCacheManager().isDebugEnabled()) {                                                       \
      qCDebug(lcCacheManager) << msg;                                                              \
    }                                                                                              \
  } while (0)

void CacheManager::initialize() {
  QMutexLocker locker(&m_mutex);

  QString metadataPath = getCacheDirectory() + QStringLiteral("/metadata/artwork_cache.json");
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
void CacheManager::writeTimestamps(const QHash<QString, qint64> &dirtyTimestamps,
                                   const QString &metadataPath) {
  if (dirtyTimestamps.isEmpty()) {
    return;
  }

  const QString parentDir = QFileInfo(metadataPath).absolutePath();
  if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to create cache metadata directory",
                                          "CacheManager::writeTimestamps")
            .withDetails(QString("Path: %1").arg(parentDir)));
    return;
  }

  // Read existing metadata file to merge with dirty timestamps
  QJsonObject root;
  QJsonObject timestamps;

  QFile existingFile(metadataPath);
  if (existingFile.exists() && existingFile.open(QIODevice::ReadOnly)) {
    const QByteArray data = existingFile.readAll();
    existingFile.close();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
      root = doc.object();
      if (root.contains("timestamps") && root["timestamps"].isObject()) {
        timestamps = root["timestamps"].toObject();
      }
    }
  }

  // Merge dirty timestamps into existing (only iterate over changed entries)
  for (auto it = dirtyTimestamps.begin(); it != dirtyTimestamps.end(); ++it) {
    timestamps[it.key()] = QDateTime::fromMSecsSinceEpoch(it.value()).toString(Qt::ISODate);
  }
  root["timestamps"] = timestamps;

  const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
  QSaveFile metadataFile(metadataPath);
  if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to open cache metadata for writing",
                                          "CacheManager::writeTimestamps")
            .withDetails(
                QString("Path: %1, Error: %2").arg(metadataPath, metadataFile.errorString())));
    return;
  }

  const qint64 written = metadataFile.write(payload);
  if (written != payload.size()) {
    metadataFile.cancelWriting();
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
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
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to atomically commit cache metadata",
                                          "CacheManager::writeTimestamps")
            .withDetails(
                QString("Path: %1, Error: %2").arg(metadataPath, metadataFile.errorString())));
    return;
  }

  // fsync the parent directory so the rename is durable across crash/power loss
  PathUtils::syncDirectory(QFileInfo(metadataPath).absolutePath());
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
  QHash<QString, qint64> dirtyTimestampsCopy;
  QString metadataPath;
  QList<QPair<QString, QPixmap>> dirtyPixmaps;

  {
    QMutexLocker locker(&m_mutex);
    shouldWriteMetadata = m_metadataDirty && !dirtyTimestamps.isEmpty();
    if (shouldWriteMetadata) {
      // Only copy timestamps for paths that actually changed
      for (const QString &path : std::as_const(dirtyTimestamps)) {
        if (fileTimestamps.contains(path)) {
          dirtyTimestampsCopy[path] = fileTimestamps[path];
        }
      }
      dirtyTimestamps.clear();
      m_metadataDirty = false;
      metadataPath = getCacheDirectory() + QStringLiteral("/metadata/artwork_cache.json");
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
  m_ioThreadPool->start([cancelFlag, shouldWriteMetadata, dirtyTimestampsCopy, metadataPath,
                         dirtyImages]() {
    if (cancelFlag->load(std::memory_order_acquire) || QApplication::closingDown()) {
      return;
    }

    QElapsedTimer timer;
    timer.start();

    if (shouldWriteMetadata) {
      CacheManager::writeTimestamps(dirtyTimestampsCopy, metadataPath);
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
            ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                              "Failed to create artwork cache directory",
                                              "CacheManager::saveToDisk")
                .withDetails(QString("Path: %1").arg(parentDir)));
        continue;
      }
      if (!image.save(cachePath, "PNG")) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                              "Failed to persist artwork PNG to cache",
                                              "CacheManager::saveToDisk")
                .withDetails(QString("Path: %1").arg(cachePath)));
      }
    }

    if (lcCacheManager().isDebugEnabled()) {
      qCDebug(lcCacheManager) << "CacheManager saveToDisk flushed"
                              << "metadata=" << (shouldWriteMetadata ? "yes" : "no")
                              << "images=" << dirtyImages.size() << "elapsedMs=" << timer.elapsed();
    }
  });
}

// Saves cache metadata to disk during shutdown - skips
// QApplication::closingDown check since we're intentionally saving during app
// close
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
    dirtyTimestamps.clear();
  }

  // On shutdown, write all timestamps (full save to preserve complete state).
  QString metadataPath = getCacheDirectory() + QStringLiteral("/metadata/artwork_cache.json");
  writeTimestamps(timestampsCopy, metadataPath);
}

// Returns artwork pixmap if valid and up-to-date; otherwise attempts load from
// disk cache
