#include "artworkpathcatalog.h"

#include "artworkutils.h"
#include "collectionutils.h"
#include "extensionutils.h"
#include "loggingcategories.h"

#include <functional>
#include <QDir>
#include <QMutexLocker>
#include <QtConcurrent>
#include <QThreadPool>

void ArtworkPathCatalog::appendDirImagesNoLock(const QString &dirPath, QSet<QString> &processedDirs,
                                               QStringList &out) {
  if (dirPath.isEmpty()) {
    return;
  }
  const QString normalized = QDir(dirPath).absolutePath();
  if (processedDirs.contains(normalized)) {
    return;
  }
  QDir dir(dirPath);
  if (!dir.exists()) {
    processedDirs.insert(normalized);
    return;
  }
  const QStringList exts = ExtensionUtils::imageFilters();
  dir.setNameFilters(exts);
  const QStringList files = dir.entryList(QDir::Files);
  for (const QString &file : files) {
    out.append(dir.absoluteFilePath(file));
  }
  processedDirs.insert(normalized);
}

QStringList ArtworkPathCatalog::collectArtworkDirs(const QList<CollectionConfig> *collections,
                                                   int collectionIndex, bool includeDescendants) {
  if (!collections || collectionIndex < 0 || collectionIndex >= collections->size()) {
    return {};
  }
  QSet<QString> dirs;
  const CollectionConfig &collection = (*collections)[collectionIndex];
  if (!collection.artworkDirectory.isEmpty()) {
    dirs.insert(QDir(collection.artworkDirectory).absolutePath());
  }
  if (includeDescendants) {
    std::function<void(int)> walk = [&](int parentIdx) {
      for (int i = 0; i < collections->size(); ++i) {
        if ((*collections)[i].parentCollectionIndex == parentIdx) {
          const QString artDir = (*collections)[i].artworkDirectory;
          if (!artDir.isEmpty()) {
            dirs.insert(QDir(artDir).absolutePath());
          }
          walk(i);
        }
      }
    };
    walk(collectionIndex);
  }
  return dirs.values();
}

void ArtworkPathCatalog::buildFromCollection(const QList<CollectionConfig> *collections,
                                             int currentIndex) {
  {
    QMutexLocker locker(&m_mutex);
    m_allPaths.clear();
    m_index = 0;
  }

  if (!collections || currentIndex < 0 || currentIndex >= collections->size()) {
    return;
  }
  const CollectionConfig &collection = (*collections)[currentIndex];

  QSet<QString> processedDirectories;
  if (!collection.artworkDirectory.isEmpty()) {
    processedDirectories.insert(QDir(collection.artworkDirectory).absolutePath());
  }

  if (collection.showAllSubcollectionItems) {
    std::function<void(int)> collectDirsRecursive = [&](int parentIdx) {
      for (int i = 0; i < collections->size(); ++i) {
        if ((*collections)[i].parentCollectionIndex == parentIdx) {
          const QString artDir = (*collections)[i].artworkDirectory;
          if (!artDir.isEmpty()) {
            processedDirectories.insert(QDir(artDir).absolutePath());
          }
          collectDirsRecursive(i);
        }
      }
    };
    collectDirsRecursive(currentIndex);

    const QStringList allDirs = processedDirectories.values();
    QThreadPool::globalInstance()->start([allDirs]() {
      auto &cache = ArtworkUtils::DirectoryCache::instance();
      cache.prewarmDirectories(allDirs);
      cache.processQueuedDirectories();
      qCDebug(lcPerfTrace) << "Background dentry warmup complete: dirs=" << allDirs.size();
    });

    QMutex pathsMutex;
    QtConcurrent::blockingMap(allDirs, [this, &pathsMutex](const QString &dirPath) {
      QDir dir(dirPath);
      if (!dir.exists()) {
        return;
      }
      const QStringList exts = ExtensionUtils::imageFilters();
      dir.setNameFilters(exts);
      const QStringList files = dir.entryList(QDir::Files);
      if (files.isEmpty()) {
        return;
      }
      QStringList fullPaths;
      fullPaths.reserve(files.size());
      for (const QString &file : files) {
        fullPaths.append(dir.absoluteFilePath(file));
      }
      QMutexLocker outerLock(&pathsMutex);
      QMutexLocker innerLock(&m_mutex);
      m_allPaths.append(fullPaths);
    });
  } else {
    QStringList scanned;
    appendDirImagesNoLock(collection.artworkDirectory, processedDirectories, scanned);
    if (!scanned.isEmpty()) {
      QMutexLocker locker(&m_mutex);
      m_allPaths.append(scanned);
    }
  }
}

int ArtworkPathCatalog::totalPaths() const {
  QMutexLocker locker(&m_mutex);
  return m_allPaths.size();
}

bool ArtworkPathCatalog::isEmpty() const {
  QMutexLocker locker(&m_mutex);
  return m_allPaths.isEmpty();
}

bool ArtworkPathCatalog::isExhausted() const {
  QMutexLocker locker(&m_mutex);
  return m_index >= m_allPaths.size();
}

QStringList ArtworkPathCatalog::takeNextBatch(int maxCount) {
  if (maxCount <= 0) {
    return {};
  }
  QMutexLocker locker(&m_mutex);
  const int available = m_allPaths.size() - m_index;
  if (available <= 0) {
    return {};
  }
  const int take = qMin(maxCount, available);
  QStringList out = m_allPaths.mid(m_index, take);
  m_index += take;
  return out;
}

QStringList ArtworkPathCatalog::filterAndMarkPending(const QStringList &batch) {
  QStringList out;
  out.reserve(batch.size());
  QMutexLocker locker(&m_mutex);
  for (const QString &path : batch) {
    if (m_silentlyCached.contains(path) || m_silentPending.contains(path)) {
      continue;
    }
    m_silentPending.insert(path);
    out.append(path);
  }
  return out;
}

void ArtworkPathCatalog::markSilentlyCached(const QString &path) {
  QMutexLocker locker(&m_mutex);
  m_silentlyCached.insert(path);
}

void ArtworkPathCatalog::unmarkSilentPending(const QString &path) {
  QMutexLocker locker(&m_mutex);
  m_silentPending.remove(path);
}

void ArtworkPathCatalog::clearSilentPendingOnly() {
  QMutexLocker locker(&m_mutex);
  m_silentPending.clear();
}

void ArtworkPathCatalog::clearPathsAndPending() {
  QMutexLocker locker(&m_mutex);
  m_allPaths.clear();
  m_index = 0;
  m_silentPending.clear();
}

void ArtworkPathCatalog::clearAll() {
  QMutexLocker locker(&m_mutex);
  m_allPaths.clear();
  m_index = 0;
  m_silentlyCached.clear();
  m_silentPending.clear();
}
