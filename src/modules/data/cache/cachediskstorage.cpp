// Disk persistence for the CacheManager: path hashing, JSON metadata
// read/write, and the async PNG-encode + write pipeline.
#include "cachediskstorage.h"

#include "errorutils.h"
#include "pathutils.h"
#include "threadpoolutils.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcCacheManager)

CacheDiskStorage::CacheDiskStorage()
    : m_cancelToken(std::make_shared<std::atomic_bool>(false)), m_pool(new QThreadPool()) {
  // Single-thread pool: keep flushes sequential and reduce contention
  // with other QtConcurrent users.
  m_pool->setMaxThreadCount(1);
}

CacheDiskStorage::~CacheDiskStorage() {
  // Cooperative shutdown: m_cancelToken is captured by the dispatched
  // lambda so tasks bail quickly. drainWithBudget() handles the bounded
  // wait + leaked-pool fallback; callers can also invoke it explicitly
  // (CacheManager does this for visibility into the failure mode), so
  // we just clear the queue here.
  if (m_pool) {
    m_pool->clear();
  }
}

QString CacheDiskStorage::cacheDirectory() {
  const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
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
                                            "CacheDiskStorage::cacheDirectory")
              .withDetails(QString("Path: %1").arg(artworkDirPath)));
    }
  }
  if (!dir.exists("metadata") && !dir.mkpath(metadataDirPath)) {
    if (!loggedMkpathFailure) {
      loggedMkpathFailure = true;
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                            "Failed to create cache metadata directory",
                                            "CacheDiskStorage::cacheDirectory")
              .withDetails(QString("Path: %1").arg(metadataDirPath)));
    }
  }
  return cacheDir;
}

QString CacheDiskStorage::artworkCachePath(const QString &artworkPath) {
  const QByteArray hash = QCryptographicHash::hash(artworkPath.toUtf8(), QCryptographicHash::Md5);
  return cacheDirectory() + QStringLiteral("/artwork/") + hash.toHex() + QStringLiteral(".png");
}

QString CacheDiskStorage::metadataPath() {
  return cacheDirectory() + QStringLiteral("/metadata/artwork_cache.json");
}

void CacheDiskStorage::readTimestampsInto(QHash<QString, qint64> &outTimestamps) const {
  // Bail before any disk I/O if the caller already cancelled. Same check
  // repeats after the parse and inside the per-key loop so a long load on
  // a slow disk (or a 50MB+ timestamps file) can short-circuit when
  // shutdown signals via cancel() (Kartend-bu5n).
  if (isCancelled()) {
    return;
  }
  QFile metadataFile(metadataPath());
  if (!metadataFile.open(QIODevice::ReadOnly)) {
    return;
  }

  const QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll());
  metadataFile.close();
  if (isCancelled()) {
    return;
  }
  const QJsonObject root = doc.object();
  const QJsonObject timestamps = root["timestamps"].toObject();
  int sinceCheck = 0;
  for (auto it = timestamps.begin(); it != timestamps.end(); ++it) {
    // Polling cancellation every key would be expensive; sample every 1024
    // entries — fine-grained enough that even a million-key file aborts in
    // ~1ms once cancel is signalled.
    if (++sinceCheck >= 1024) {
      sinceCheck = 0;
      if (isCancelled()) {
        return;
      }
    }
    const QDateTime dateTime = QDateTime::fromString(it.value().toString(), Qt::ISODate);
    outTimestamps[it.key()] = dateTime.isValid() ? dateTime.toMSecsSinceEpoch() : 0;
  }
}

void CacheDiskStorage::writeTimestamps(const QHash<QString, qint64> &dirtyTimestamps) {
  if (dirtyTimestamps.isEmpty()) {
    return;
  }

  const QString path = metadataPath();
  const QString parentDir = QFileInfo(path).absolutePath();
  if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to create cache metadata directory",
                                          "CacheDiskStorage::writeTimestamps")
            .withDetails(QString("Path: %1").arg(parentDir)));
    return;
  }

  // Read existing metadata file to merge with dirty timestamps.
  QJsonObject root;
  QJsonObject timestamps;

  QFile existingFile(path);
  if (existingFile.exists() && existingFile.open(QIODevice::ReadOnly)) {
    const QByteArray data = existingFile.readAll();
    existingFile.close();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
      root = doc.object();
      if (root.contains("timestamps") && root["timestamps"].isObject()) {
        timestamps = root["timestamps"].toObject();
      }
    }
  }

  // Merge dirty timestamps into existing — only iterate over changed entries.
  for (auto it = dirtyTimestamps.begin(); it != dirtyTimestamps.end(); ++it) {
    timestamps[it.key()] = QDateTime::fromMSecsSinceEpoch(it.value()).toString(Qt::ISODate);
  }
  root["timestamps"] = timestamps;

  const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
  QSaveFile metadataFile(path);
  if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to open cache metadata for writing",
                                          "CacheDiskStorage::writeTimestamps")
            .withDetails(QString("Path: %1, Error: %2").arg(path, metadataFile.errorString())));
    return;
  }

  const qint64 written = metadataFile.write(payload);
  if (written != payload.size()) {
    metadataFile.cancelWriting();
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to write complete cache metadata payload",
                                          "CacheDiskStorage::writeTimestamps")
            .withDetails(QString("Path: %1, Written: %2, Expected: %3, Error: %4")
                             .arg(path)
                             .arg(written)
                             .arg(payload.size())
                             .arg(metadataFile.errorString())));
    return;
  }

  if (!metadataFile.commit()) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to atomically commit cache metadata",
                                          "CacheDiskStorage::writeTimestamps")
            .withDetails(QString("Path: %1, Error: %2").arg(path, metadataFile.errorString())));
    return;
  }

  // fsync the parent directory so the rename is durable across crash / power
  // loss.
  PathUtils::syncDirectory(QFileInfo(path).absolutePath());
}

void CacheDiskStorage::scheduleAsyncSave(bool shouldWriteMetadata,
                                         const QHash<QString, qint64> &dirtyTimestamps,
                                         const QList<QPair<QString, QImage>> &dirtyImages) {
  if (!m_pool) {
    return;
  }
  if (!shouldWriteMetadata && dirtyImages.isEmpty()) {
    return;
  }
  // Capture shared_ptr to cancellation flag so the lambda can safely
  // check it even after this storage instance is destroyed (the
  // QThreadPool destructor blocks; the bounded-drain helper either
  // cleans up cleanly or abandons the pool and lets the OS reclaim).
  auto cancelToken = m_cancelToken;
  m_pool->start([cancelToken, shouldWriteMetadata, dirtyTimestamps, dirtyImages]() {
    if (cancelToken->load(std::memory_order_acquire) || QApplication::closingDown()) {
      return;
    }

    QElapsedTimer timer;
    timer.start();

    if (shouldWriteMetadata) {
      writeTimestamps(dirtyTimestamps);
    }

    for (const auto &entry : dirtyImages) {
      if (cancelToken->load(std::memory_order_acquire) || QApplication::closingDown()) {
        break;
      }

      const QString &artworkPath = entry.first;
      const QImage &image = entry.second;
      if (image.isNull()) {
        continue;
      }

      const QString cachePath = artworkCachePath(artworkPath);
      const QString parentDir = QFileInfo(cachePath).absolutePath();
      if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                              "Failed to create artwork cache directory",
                                              "CacheDiskStorage::scheduleAsyncSave")
                .withDetails(QString("Path: %1").arg(parentDir)));
        continue;
      }
      if (!image.save(cachePath, "PNG")) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                              "Failed to persist artwork PNG to cache",
                                              "CacheDiskStorage::scheduleAsyncSave")
                .withDetails(QString("Path: %1").arg(cachePath)));
      }
    }

    if (lcCacheManager().isDebugEnabled()) {
      qCDebug(lcCacheManager) << "CacheDiskStorage flushed"
                              << "metadata=" << (shouldWriteMetadata ? "yes" : "no")
                              << "images=" << dirtyImages.size() << "elapsedMs=" << timer.elapsed();
    }
  });
}

void CacheDiskStorage::cancel() {
  m_cancelToken->store(true, std::memory_order_release);
}

bool CacheDiskStorage::isCancelled() const {
  return m_cancelToken->load(std::memory_order_acquire);
}

void CacheDiskStorage::clearQueue() {
  if (m_pool) {
    m_pool->clear();
  }
}

bool CacheDiskStorage::drainWithBudget(int budgetMs) {
  return ThreadPoolUtils::shutdownWithBudget(m_pool, budgetMs);
}
