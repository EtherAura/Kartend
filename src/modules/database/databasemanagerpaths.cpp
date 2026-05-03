// Sibling TU: path resolution + cached counts for DatabaseManager.
#include <functional>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtGlobal>
#include <QThread>
#include <QTimer>
#include <stdexcept>

#include "collectionutils.h"
#include "databasemanager.h"
#include "dbmigrations.h"
#include "errorutils.h"
#include "historystore.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "pathutils.h"
#include "querymanager.h"
#include "sessionmanager.h"
#include "uiconstants.h"
#include "usagestatsstore.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcDatabaseManager)
#define debugLog(msg) qCDebug(lcDatabaseManager) << msg

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

auto DatabaseManager::countCollectionRecursive(int collectionIndex,
                                               const QList<CollectionConfig> &allCollections) const
    -> qint64 {
  if (!CollectionUtils::isValidIndex(collectionIndex, &allCollections)) {
    return 0;
  }
  QString expandedMediaDir = PathUtils::validateAndExpandPath(
      allCollections[collectionIndex].mediaDirectory, allCollections[collectionIndex].name);
  const QString uuid = CollectionUtils::computeCollectionUuid(allCollections[collectionIndex].name,
                                                              expandedMediaDir);
  qint64 total = countCollectionByUuid(uuid);
  QList<int> descendants =
      CollectionUtils::collectDescendantIndices(collectionIndex, allCollections);
  for (int descendantIndex : descendants) {
    QString descExpandedMediaDir = PathUtils::validateAndExpandPath(
        allCollections[descendantIndex].mediaDirectory, allCollections[descendantIndex].name);
    const QString descendantUuid = CollectionUtils::computeCollectionUuid(
        allCollections[descendantIndex].name, descExpandedMediaDir);
    total += countCollectionByUuid(descendantUuid);
  }
  return total;
}

auto DatabaseManager::loadItemMetadata(const QString &collectionUuid, const QString &path) const
    -> ItemMetadataStore::ItemMetadata {
  // Use the main-thread connection. Reads are tiny single-row lookups; the
  // worker thread continues to own writes for scan/persist.
  auto result = ItemMetadataStore::load(const_cast<QSqlDatabase &>(m_db), collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    ItemMetadataStore::ItemMetadata empty;
    empty.collectionUuid = collectionUuid;
    empty.path = path;
    return empty;
  }
  return result.value();
}

bool DatabaseManager::saveItemMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
  // User-driven edits are infrequent single-row upserts; running on the
  // main-thread connection avoids needing to round-trip through QueryManager
  // signals just to surface success/failure to the dialog.
  auto result = ItemMetadataStore::save(m_db, metadata);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  return true;
}

auto DatabaseManager::loadItemArtwork(const QString &collectionUuid, const QString &path) const
    -> QList<ItemArtworkStore::ItemArtwork> {
  // Main-thread connection: the sidebar fetches at most a few rows per item
  // when the selection changes, so a worker round-trip would be overhead for
  // no real concurrency benefit.
  auto result =
      ItemArtworkStore::loadAllForItem(const_cast<QSqlDatabase &>(m_db), collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

bool DatabaseManager::saveItemArtwork(const ItemArtworkStore::ItemArtwork &artwork) {
  // Mirrors saveItemMetadata: user-driven single-row upsert on the main-thread
  // connection so the per-item link dialog can react to success/failure
  // without rerouting through the worker thread.
  auto result = ItemArtworkStore::save(m_db, artwork);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  return true;
}

bool DatabaseManager::removeItemArtwork(const QString &collectionUuid, const QString &path,
                                        const QString &artworkType) {
  auto result = ItemArtworkStore::remove(m_db, collectionUuid, path, artworkType);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  return true;
}

// ─── Usage stats (Kartend-7vi) ───────────────────────────────────────────────
// All paths use the main-thread connection: writes are tiny single-row updates
// triggered by user actions (launch, runtime exit), and reads are dialog-time
// aggregates that don't need worker concurrency.

auto DatabaseManager::loadItemUsageStats(const QString &collectionUuid, const QString &path) const
    -> UsageStatsStore::ItemUsageStats {
  auto result =
      UsageStatsStore::loadForItem(const_cast<QSqlDatabase &>(m_db), collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    UsageStatsStore::ItemUsageStats empty;
    empty.collectionUuid = collectionUuid;
    empty.path = path;
    return empty;
  }
  return result.value();
}

void DatabaseManager::recordItemLaunch(const QString &collectionUuid, const QString &path) {
  auto result = UsageStatsStore::recordLaunch(m_db, collectionUuid, path);
  if (result.isError()) {
    // Tracking is best-effort — log but never block the launch path.
    ErrorUtils::logError(result.error());
  }
}

void DatabaseManager::recordItemPlaySession(const QString &collectionUuid, const QString &path,
                                            qint64 seconds) {
  auto result = UsageStatsStore::recordPlaySession(m_db, collectionUuid, path, seconds);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
  }
}

auto DatabaseManager::loadAggregateUsageStats() const -> UsageStatsStore::AggregateStats {
  auto result = UsageStatsStore::loadAggregate(const_cast<QSqlDatabase &>(m_db));
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

auto DatabaseManager::loadTopPlayedItems(int limit) const -> QList<UsageStatsStore::ItemUsageRow> {
  auto result = UsageStatsStore::loadTopPlayed(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

auto DatabaseManager::loadRecentlyPlayedItems(int limit) const
    -> QList<UsageStatsStore::ItemUsageRow> {
  auto result = UsageStatsStore::loadRecentlyPlayed(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

auto DatabaseManager::loadUsageByCollection() const
    -> QHash<QString, UsageStatsStore::CollectionUsage> {
  auto result = UsageStatsStore::loadCollectionBreakdown(const_cast<QSqlDatabase &>(m_db));
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

bool DatabaseManager::resetAllUsageStats() {
  auto result = UsageStatsStore::resetAll(m_db);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  return true;
}

// ─── Launch history (Kartend-fse) ────────────────────────────────────────────
// Same main-thread connection rationale as the usage-stats helpers above:
// inserts/trims are tiny and triggered by user actions, reads are dialog-time
// scans bounded by the user-configured cap.

void DatabaseManager::recordHistoryEntry(const QString &collectionUuid, const QString &path,
                                         const QString &name, int maxEntries) {
  auto result = HistoryStore::recordLaunch(m_db, collectionUuid, path, name);
  if (result.isError()) {
    // Best-effort tracking — log but never block the launch path.
    ErrorUtils::logError(result.error());
    return;
  }
  if (maxEntries > 0) {
    auto trim = HistoryStore::trimToMaxEntries(m_db, maxEntries);
    if (trim.isError()) {
      ErrorUtils::logError(trim.error());
    }
  }
}

auto DatabaseManager::loadRecentHistory(int limit) const -> QList<HistoryStore::HistoryEntry> {
  auto result = HistoryStore::loadRecent(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

auto DatabaseManager::historyEntryCount() const -> qint64 {
  auto result = HistoryStore::count(const_cast<QSqlDatabase &>(m_db));
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return 0;
  }
  return result.value();
}

bool DatabaseManager::clearHistory() {
  auto result = HistoryStore::clearAll(m_db);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  return true;
}

// Count items globally across all collections
auto DatabaseManager::countGlobal(const QList<CollectionConfig> &allCollections) -> qint64 {
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
void DatabaseManager::updateCachedCounts(const QList<CollectionConfig> &allCollections) {
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
    expandedMediaDirs[i] =
        PathUtils::validateAndExpandPath(allCollections[i].mediaDirectory, allCollections[i].name);
    uuids[i] = CollectionUtils::computeCollectionUuid(allCollections[i].name, expandedMediaDirs[i]);

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

  emit requestUpdateCachedCounts(m_inFlightCachedCountsGeneration, m_inFlightCountsUuids);
}

void DatabaseManager::onWorkerCachedCountsComputed(
    quint64 generation, qint64 globalCount, const QHash<QString, qint64> &directCountsByUuid) {
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
    const QString uuid = (i < m_inFlightCountsUuids.size()) ? m_inFlightCountsUuids[i] : QString();
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
    m_sessionManager->setCollectionCounts(collections[i], collections, directCounts[i],
                                          recursiveCounts[i]);
  }
  m_sessionManager->saveToDisk();
  emit cachedCountsUpdated();
}
auto DatabaseManager::getCollectionIndexForFile(const QString &filePath) const -> int {
  QMutexLocker locker(&m_dataMutex);
  return m_fileToCollectionIndex.value(filePath, -1);
}

// Resolve a raw file entry to its full absolute path.
// Handles both absolute paths and relative paths that need resolution via
// collection mappings when showAllSubcollectionItems is enabled.
auto DatabaseManager::resolveFilePath(const QString &rawEntry,
                                      const CollectionContext &context) const -> QString {
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
    qCDebug(lcDatabaseManager) << "resolveFilePath: cannot resolve relative entry" << rawEntry
                               << "-- collection" << context.config.name
                               << "has empty mediaDirectory and showAllSubcollectionItems is false";
    return {};
  }
  return QDir(mediaDir).absoluteFilePath(rawEntry);
}

// Resolve a relative file path by searching fileNames map and falling back
// to collection index lookup for media directory resolution.
auto DatabaseManager::resolveRelativeFilePath(const QString &rawFileName,
                                              const QHash<QString, QString> &fileNames) const
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
    if (key.endsWith("/" + rawFileName) || key.endsWith(QDir::separator() + rawFileName) ||
        key == rawFileName) {
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

  qCDebug(lcDatabaseManager) << "resolveRelativeFilePath: failed to resolve" << rawFileName
                             << "-- not found in fileNames map (" << fileNames.size()
                             << "entries), m_relativeToFullPath cache, or collection-index lookup";
  return {};
}

// Resolve artwork directory for a file using best-available mapping
auto DatabaseManager::findArtworkDirectoryForFile(const QString &filePath) const -> QString {
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
