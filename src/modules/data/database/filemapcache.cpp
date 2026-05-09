#include "filemapcache.h"

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

#include "loggingcategories.h"

Q_DECLARE_LOGGING_CATEGORY(lcDatabaseManager)

void FileMapCache::replaceFromItemsLoaded(const QHash<QString, QString> &fileToArtworkDir,
                                          const QHash<QString, QString> &fileToMediaDir,
                                          const QHash<QString, int> &fileToCollectionIndex,
                                          const QHash<QString, QString> &filteredNames) {
  QHash<QString, QString> relativeToFullPath;
  relativeToFullPath.reserve(filteredNames.size() * 2);

  // Build a fast lookup cache for resolveRelativeFilePath().
  // Keep "first seen" semantics to match the previous linear scan over
  // fileNames (which returned the first match it encountered).
  for (auto it = filteredNames.constBegin(); it != filteredNames.constEnd(); ++it) {
    const QString &fullPath = it.key();
    if (fullPath.isEmpty()) {
      continue;
    }
    if (!relativeToFullPath.contains(fullPath)) {
      relativeToFullPath.insert(fullPath, fullPath);
    }
    const QString leafName = QFileInfo(fullPath).fileName();
    if (!leafName.isEmpty() && !relativeToFullPath.contains(leafName)) {
      relativeToFullPath.insert(leafName, fullPath);
    }
    const QString mediaDir = fileToMediaDir.value(fullPath);
    if (!mediaDir.trimmed().isEmpty()) {
      const QString relativePath = QDir(mediaDir).relativeFilePath(fullPath);
      if (!relativePath.isEmpty() && !relativeToFullPath.contains(relativePath)) {
        relativeToFullPath.insert(relativePath, fullPath);
      }
    }
  }

  QMutexLocker locker(&m_mutex);
  m_fileToArtworkDir = fileToArtworkDir;
  m_fileToMediaDir = fileToMediaDir;
  m_fileToCollectionIndex = fileToCollectionIndex;
  m_relativeToFullPath = std::move(relativeToFullPath);
}

void FileMapCache::mergeRangeLoaded(const QHash<QString, QString> &fileToArtworkDir,
                                    const QHash<QString, QString> &fileToMediaDir,
                                    const QHash<QString, int> &fileToCollectionIndex) {
  if (fileToArtworkDir.isEmpty() && fileToMediaDir.isEmpty() && fileToCollectionIndex.isEmpty()) {
    return;
  }
  QMutexLocker locker(&m_mutex);
  m_fileToArtworkDir.insert(fileToArtworkDir);
  m_fileToMediaDir.insert(fileToMediaDir);
  m_fileToCollectionIndex.insert(fileToCollectionIndex);
}

int FileMapCache::collectionIndexForFile(const QString &filePath) const {
  QMutexLocker locker(&m_mutex);
  return m_fileToCollectionIndex.value(filePath, -1);
}

QString FileMapCache::artworkDirForFile(const QString &filePath) const {
  QMutexLocker locker(&m_mutex);
  if (m_fileToArtworkDir.contains(filePath)) {
    return m_fileToArtworkDir.value(filePath);
  }
  const QString fileName = QFileInfo(filePath).fileName();
  const QString baseName = QFileInfo(filePath).completeBaseName();
  if (m_fileToArtworkDir.contains(fileName)) {
    return m_fileToArtworkDir.value(fileName);
  }
  if (m_fileToArtworkDir.contains(baseName)) {
    return m_fileToArtworkDir.value(baseName);
  }
  return {};
}

QString FileMapCache::resolveFilePath(const QString &rawEntry,
                                      const CollectionContext &context) const {
  // DB-backed paginated queries (search) can return fully-qualified absolute
  // paths. Those should always pass through unchanged, even when the current
  // view's mediaDirectory is empty (e.g. container collections).
  if (QDir::isAbsolutePath(rawEntry)) {
    return rawEntry;
  }
  if (context.config.showAllSubcollectionItems) {
    return resolveRelativeFilePath(rawEntry, context.fileNames);
  }
  const QString mediaDir = context.config.mediaDirectory.trimmed();
  if (mediaDir.isEmpty()) {
    qCDebug(lcDatabaseManager) << "FileMapCache::resolveFilePath: cannot resolve relative entry"
                               << rawEntry << "-- collection" << context.config.name
                               << "has empty mediaDirectory and showAllSubcollectionItems is false";
    return {};
  }
  return QDir(mediaDir).absoluteFilePath(rawEntry);
}

QString FileMapCache::resolveRelativeFilePath(const QString &rawFileName,
                                              const QHash<QString, QString> &fileNames) const {
  if (rawFileName.trimmed().isEmpty()) {
    return {};
  }
  if (fileNames.contains(rawFileName)) {
    return rawFileName;
  }
  {
    QMutexLocker locker(&m_mutex);
    auto it = m_relativeToFullPath.constFind(rawFileName);
    if (it != m_relativeToFullPath.constEnd()) {
      return it.value();
    }
  }
  // Compatibility fallback: preserve previous suffix-scan behavior.
  for (auto it = fileNames.constBegin(); it != fileNames.constEnd(); ++it) {
    const QString &key = it.key();
    if (key.endsWith("/" + rawFileName) || key.endsWith(QDir::separator() + rawFileName) ||
        key == rawFileName) {
      return key;
    }
  }
  // Fallback: use collection index to find media directory
  QMutexLocker locker(&m_mutex);
  const int ownerIndex = m_fileToCollectionIndex.value(rawFileName, -1);
  if (ownerIndex >= 0) {
    const QString mediaDir = m_fileToMediaDir.value(rawFileName);
    if (!mediaDir.trimmed().isEmpty()) {
      return QDir(mediaDir).absoluteFilePath(rawFileName);
    }
  }
  qCDebug(lcDatabaseManager) << "FileMapCache::resolveRelativeFilePath: failed to resolve"
                             << rawFileName << "-- not found in fileNames map (" << fileNames.size()
                             << "entries), m_relativeToFullPath cache, or collection-index lookup";
  return {};
}
