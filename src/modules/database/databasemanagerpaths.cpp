// Sibling TU: path resolution + cached counts for DatabaseManager.
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtGlobal>
#include <functional>
#include <stdexcept>

#include "collectionutils.h"
#include "databasemanager.h"
#include "dbmigrations.h"
#include "errorutils.h"
#include "pathutils.h"
#include "querymanager.h"
#include "sessionmanager.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcDatabaseManager)
#define debugLog(msg) qCDebug(lcDatabaseManager) << msg

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

auto DatabaseManager::countCollectionRecursive(
    int collectionIndex, const QList<CollectionConfig> &allCollections) const
    -> qint64 {
  if (!CollectionUtils::isValidIndex(collectionIndex, &allCollections)) {
    return 0;
  }
  QString expandedMediaDir = PathUtils::validateAndExpandPath(
      allCollections[collectionIndex].mediaDirectory,
      allCollections[collectionIndex].name);
  const QString uuid = CollectionUtils::computeCollectionUuid(
      allCollections[collectionIndex].name, expandedMediaDir);
  qint64 total = countCollectionByUuid(uuid);
  QList<int> descendants = CollectionUtils::collectDescendantIndices(
      collectionIndex, allCollections);
  for (int descendantIndex : descendants) {
    QString descExpandedMediaDir = PathUtils::validateAndExpandPath(
        allCollections[descendantIndex].mediaDirectory,
        allCollections[descendantIndex].name);
    const QString descendantUuid = CollectionUtils::computeCollectionUuid(
        allCollections[descendantIndex].name, descExpandedMediaDir);
    total += countCollectionByUuid(descendantUuid);
  }
  return total;
}

// Count items globally across all collections
auto DatabaseManager::countGlobal(const QList<CollectionConfig> &allCollections)
    -> qint64 {
  Q_UNUSED(allCollections)
  if (!m_db.isOpen()) {
    return 0;
  }
  QSqlQuery query("SELECT COUNT(*) FROM items", m_db);
  if (!query.next()) {
    return 0;
  }
  return query.value(0).toLongLong();
}

// Recomputes and persists direct and recursive item counts for all collections
void DatabaseManager::updateCachedCounts(
    const QList<CollectionConfig> &allCollections) {
  if (!m_db.isOpen() || !m_sessionManager) {
    return;
  }

  m_sessionManager->clearStaleCollections(allCollections);

  const int collectionCount = allCollections.size();
  QVector<QString> expandedMediaDirs;
  QVector<QString> uuids;
  expandedMediaDirs.resize(collectionCount);
  uuids.resize(collectionCount);

  for (int i = 0; i < collectionCount; ++i) {
    expandedMediaDirs[i] = PathUtils::validateAndExpandPath(
        allCollections[i].mediaDirectory, allCollections[i].name);
    uuids[i] = CollectionUtils::computeCollectionUuid(allCollections[i].name,
                                                      expandedMediaDirs[i]);

    if (expandedMediaDirs[i].trimmed().isEmpty()) {
      clearCollectionFromDatabaseByUuid(uuids[i]);
    }
  }

  m_pendingCountsCollections = allCollections;
  m_pendingCountsUuids.clear();
  m_pendingCountsUuids.reserve(collectionCount);
  for (const QString &uuid : uuids) {
    m_pendingCountsUuids.append(uuid);
  }

  // Debounce count recomputation to avoid redundant work when multiple loads
  // trigger updateCachedCounts() in quick succession (e.g. navigation + filter
  // changes).
  if (m_cachedCountsUpdateTimer) {
    m_cachedCountsUpdateTimer->start(UIConstants::Timing::SHORT_DELAY_MS);
  }
}

void DatabaseManager::dispatchCachedCountsUpdate() {
  if (!m_sessionManager) {
    return;
  }

  m_inFlightCachedCountsGeneration = ++m_cachedCountsGeneration;
  m_inFlightCountsCollections = m_pendingCountsCollections;
  m_inFlightCountsUuids = m_pendingCountsUuids;

  emit requestUpdateCachedCounts(m_inFlightCachedCountsGeneration,
                                 m_inFlightCountsUuids);
}

void DatabaseManager::onWorkerCachedCountsComputed(
    quint64 generation, qint64 globalCount,
    const QHash<QString, qint64> &directCountsByUuid) {
  if (!m_sessionManager) {
    return;
  }
  if (generation != m_inFlightCachedCountsGeneration) {
    return;
  }

  const QList<CollectionConfig> &collections = m_inFlightCountsCollections;
  const int collectionCount = collections.size();

  QVector<qint64> directCounts;
  QVector<qint64> recursiveCounts;
  directCounts.resize(collectionCount);
  recursiveCounts.resize(collectionCount);

  for (int i = 0; i < collectionCount; ++i) {
    const QString uuid = (i < m_inFlightCountsUuids.size())
                             ? m_inFlightCountsUuids[i]
                             : QString();
    directCounts[i] = uuid.isEmpty() ? 0 : directCountsByUuid.value(uuid, 0);
  }

  QVector<QList<int>> children;
  children.resize(collectionCount);
  for (int i = 0; i < collectionCount; ++i) {
    const int parent = collections[i].parentCollectionIndex;
    if (parent >= 0 && parent < collectionCount) {
      children[parent].append(i);
    }
  }

  QVector<int> visitState;
  visitState.resize(collectionCount);
  visitState.fill(0);

  std::function<qint64(int)> computeRecursiveCount = [&](int index) -> qint64 {
    if (index < 0 || index >= collectionCount) {
      return 0;
    }
    if (visitState[index] == 2) {
      return recursiveCounts[index];
    }
    if (visitState[index] == 1) {
      // Cycle guard: treat current node as leaf.
      return directCounts[index];
    }

    visitState[index] = 1;
    qint64 total = directCounts[index];
    for (int childIndex : children[index]) {
      total += computeRecursiveCount(childIndex);
    }
    visitState[index] = 2;
    recursiveCounts[index] = total;
    return total;
  };

  for (int i = 0; i < collectionCount; ++i) {
    computeRecursiveCount(i);
  }

  m_sessionManager->setGlobalItemCount(globalCount);
  for (int i = 0; i < collectionCount; ++i) {
    m_sessionManager->setCollectionCounts(collections[i], collections,
                                          directCounts[i], recursiveCounts[i]);
  }
  m_sessionManager->saveToDisk();
  emit cachedCountsUpdated();
}
auto DatabaseManager::getCollectionIndexForFile(const QString &filePath) const
    -> int {
  QMutexLocker locker(&m_dataMutex);
  return m_fileToCollectionIndex.value(filePath, -1);
}

// Resolve a raw file entry to its full absolute path.
// Handles both absolute paths and relative paths that need resolution via
// collection mappings when showAllSubcollectionItems is enabled.
auto DatabaseManager::resolveFilePath(const QString &rawEntry,
                                      const CollectionContext &context) const
    -> QString {
  // DB-backed paginated queries (search) can return fully-qualified absolute
  // paths. Those should always pass through unchanged, even when the current
  // view's mediaDirectory is empty (e.g., container collections).
  if (QDir::isAbsolutePath(rawEntry)) {
    return rawEntry;
  }

  if (context.config.showAllSubcollectionItems) {
    // Try to resolve relative path using fileNames mapping
    return resolveRelativeFilePath(rawEntry, context.fileNames);
  }

  // Simple case: prepend media directory
  const QString mediaDir = context.config.mediaDirectory.trimmed();
  if (mediaDir.isEmpty()) {
    qCDebug(lcDatabaseManager)
        << "resolveFilePath: cannot resolve relative entry" << rawEntry
        << "-- collection" << context.config.name
        << "has empty mediaDirectory and showAllSubcollectionItems is false";
    return {};
  }
  return QDir(mediaDir).absoluteFilePath(rawEntry);
}

// Resolve a relative file path by searching fileNames map and falling back
// to collection index lookup for media directory resolution.
auto DatabaseManager::resolveRelativeFilePath(
    const QString &rawFileName, const QHash<QString, QString> &fileNames) const
    -> QString {
  if (rawFileName.trimmed().isEmpty()) {
    return {};
  }

  // Fast path: exact full-path key lookup.
  if (fileNames.contains(rawFileName)) {
    return rawFileName;
  }

  // Fast path: use precomputed cache from the latest itemsLoaded payload.
  {
    QMutexLocker locker(&m_dataMutex);
    auto it = m_relativeToFullPath.constFind(rawFileName);
    if (it != m_relativeToFullPath.constEnd()) {
      return it.value();
    }
  }

  // Compatibility fallback: preserve previous suffix-scan behavior.
  for (auto it = fileNames.constBegin(); it != fileNames.constEnd(); ++it) {
    const QString &key = it.key();
    if (key.endsWith("/" + rawFileName) ||
        key.endsWith(QDir::separator() + rawFileName) || key == rawFileName) {
      return key;
    }
  }

  // Fallback: use collection index to find media directory
  QMutexLocker locker(&m_dataMutex);
  int ownerIndex = m_fileToCollectionIndex.value(rawFileName, -1);
  if (ownerIndex >= 0) {
    QString mediaDir = m_fileToMediaDir.value(rawFileName);
    if (!mediaDir.trimmed().isEmpty()) {
      return QDir(mediaDir).absoluteFilePath(rawFileName);
    }
  }

  qCDebug(lcDatabaseManager)
      << "resolveRelativeFilePath: failed to resolve" << rawFileName
      << "-- not found in fileNames map (" << fileNames.size()
      << "entries), m_relativeToFullPath cache, or collection-index lookup";
  return {};
}

// Resolve artwork directory for a file using best-available mapping
auto DatabaseManager::findArtworkDirectoryForFile(const QString &filePath) const
    -> QString {
  QMutexLocker locker(&m_dataMutex);
  if (m_fileToArtworkDir.contains(filePath)) {
    return m_fileToArtworkDir.value(filePath);
  }
  QString fileName = QFileInfo(filePath).fileName();
  QString baseName = QFileInfo(filePath).completeBaseName();

  if (m_fileToArtworkDir.contains(fileName)) {
    return m_fileToArtworkDir.value(fileName);
  }
  if (m_fileToArtworkDir.contains(baseName)) {
    return m_fileToArtworkDir.value(baseName);
  }
  return {};
}
