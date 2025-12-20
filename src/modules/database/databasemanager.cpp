// Coordinates SQLite database access via worker thread for collection metadata queries.
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <stdexcept>
#include <functional>
#include <QThread>
#include <QTimer>
#include <QtGlobal>

#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "errorutils.h"
#include "dbmigrations.h"
#include "querymanager.h"
#include "pathutils.h"
#include "sessionmanager.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcDatabaseManager, "kartend.databasemanager")
#define debugLog(msg) qCDebug(lcDatabaseManager) << msg

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

// Construct the database manager and initialize the database
DatabaseManager::DatabaseManager(SessionManager *sessionManager, QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager) {
  qRegisterMetaType<CollectionConfig>("CollectionConfig");
  qRegisterMetaType<CollectionContext>("CollectionContext");
  qRegisterMetaType<QList<CollectionConfig>>("QList<CollectionConfig>");
  qRegisterMetaType<QHash<QString, qint64>>("QHash<QString, qint64>");

  m_connectionName = "kartend_main";
  initDatabase();

  m_workerThread = new QThread(this);
  m_worker = new QueryManager(m_sessionManager, QStringLiteral("kartend_query_worker"));
  m_worker->moveToThread(m_workerThread);

  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

  // Dedicated scan worker (separate thread + separate DB connection) so
  // long scans don't block query operations and UI updates.
  m_scanThread = new QThread(this);
  m_scanWorker = new QueryManager(m_sessionManager, QStringLiteral("kartend_scan_worker"));
  m_scanWorker->moveToThread(m_scanThread);
  connect(m_scanThread, &QThread::finished, m_scanWorker, &QObject::deleteLater);
  
  connect(this, &DatabaseManager::requestLoadAllCollections, m_worker, &QueryManager::loadAllCollections);
  connect(this, &DatabaseManager::requestLoadItems, m_worker, &QueryManager::loadItems);
  connect(this, &DatabaseManager::requestLoadItemsWithSubcollections, m_worker, &QueryManager::loadItemsWithSubcollections);
  connect(this, &DatabaseManager::requestFetchItemCount, m_worker, &QueryManager::fetchItemCountWithToken);
  connect(this, &DatabaseManager::requestFetchItemsRange, m_worker, &QueryManager::fetchItemsRange);
  connect(this, &DatabaseManager::requestInvalidateCache, m_worker, &QueryManager::invalidateCollectionCache);
  connect(this, &DatabaseManager::requestUpdateCachedCounts, m_worker, &QueryManager::updateCachedCounts);

    // Background scanning is handled by the scan worker.
    connect(this, &DatabaseManager::requestEnsureScannedForContext,
      m_scanWorker, &QueryManager::ensureScannedForContext);

    // Lazy background FTS backfill is handled by the scan worker.
    connect(this, &DatabaseManager::requestEnsureItemsFtsReady,
      m_scanWorker, &QueryManager::ensureItemsFtsReady);
  
  connect(m_worker, &QueryManager::itemsLoaded, this, &DatabaseManager::onWorkerItemsLoaded);
  connect(m_worker, &QueryManager::itemCountLoaded, this, &DatabaseManager::onWorkerItemCountLoaded);
  connect(m_worker, &QueryManager::itemCountLoadedWithToken, this, &DatabaseManager::onWorkerItemCountLoadedWithToken);
  connect(m_worker, &QueryManager::itemsRangeLoaded, this, &DatabaseManager::onWorkerItemsRangeLoaded);
    connect(m_worker, &QueryManager::cachedCountsComputed, this, &DatabaseManager::onWorkerCachedCountsComputed);
  connect(m_worker, &QueryManager::errorOccurred, this, &DatabaseManager::errorOccurred);
  // Query worker should remain responsive; scan signals come from scan worker.
  connect(m_worker, &QueryManager::cacheInvalidated, this, &DatabaseManager::cacheInvalidated);

  connect(m_scanWorker, &QueryManager::errorOccurred, this, &DatabaseManager::errorOccurred);
  connect(m_scanWorker, &QueryManager::scanProgress, this, &DatabaseManager::scanProgress);
  connect(m_scanWorker, &QueryManager::scanStarting, this, &DatabaseManager::scanStarting);
  connect(m_scanWorker, &QueryManager::scanItemsProgress, this, &DatabaseManager::scanItemsProgress);
  connect(m_scanWorker, &QueryManager::collectionScanCompleted,
          this, &DatabaseManager::collectionScanCompleted);

    m_cachedCountsUpdateTimer = new QTimer(this);
    m_cachedCountsUpdateTimer->setSingleShot(true);
    connect(m_cachedCountsUpdateTimer, &QTimer::timeout,
      this, &DatabaseManager::dispatchCachedCountsUpdate);

  m_workerThread->start();
  m_scanThread->start();

  // Defer FTS backfill until after the event loop starts so startup UI is not
  // blocked by any optional maintenance work.
  QTimer::singleShot(0, this, [this] { emit requestEnsureItemsFtsReady(); });
}

// Destroy the database manager and close/remove the connection
DatabaseManager::~DatabaseManager() {
  // Signal threads to quit but DON'T wait - they may be blocked on I/O.
  // The process is exiting anyway, so the OS will clean up.
  if (m_workerThread) {
    m_workerThread->quit();
    // Intentionally NOT waiting - can block for minutes during shutdown
  }

  if (m_scanThread) {
    m_scanThread->quit();
    // Intentionally NOT waiting - can block for minutes during shutdown
  }

  // Close database connection - this is fast and safe
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }
}

// Initialize the sqlite database and ensure uuid-based identity with
// de-duplication
void DatabaseManager::initDatabase() {
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }

  m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
  QString dbPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!QDir().mkpath(dbPath)) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to create database directory",
                                      "DatabaseManager::initDatabase")
                   .withDetails(QString("Path: %1").arg(dbPath));
    ErrorUtils::logError(err);
    return;
  }
  m_db.setDatabaseName(dbPath + "/media.db");

  if (!m_db.open()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to open database",
                                      "DatabaseManager::initDatabase")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    return;
  }

  QSqlQuery query(m_db);
  if (!query.exec("PRAGMA foreign_keys = ON")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to enable foreign keys",
                                     "DatabaseManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  if (!query.exec("PRAGMA journal_mode = WAL")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to enable WAL mode, falling back to DELETE mode",
                                     "DatabaseManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
    // Fall back to DELETE journal mode for safer operation on systems
    // that don't support WAL (e.g., network filesystems, older SQLite)
    if (!query.exec("PRAGMA journal_mode = DELETE")) {
      auto fallbackErr = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to set DELETE journal mode",
                                               "DatabaseManager::initDatabase")
                             .withDetails(query.lastError().text());
      ErrorUtils::logError(fallbackErr);
    }
  }
  // Set busy timeout to wait up to 30 seconds for locks to be released -
  // prevents "database is locked" errors during concurrent access
  if (!query.exec("PRAGMA busy_timeout = 30000")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to set busy timeout",
                                     "DatabaseManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }

  QString collectionsTable = "CREATE TABLE IF NOT EXISTS collections ("
                             "id INTEGER PRIMARY KEY, "
                             "name TEXT NOT NULL, "
                             "last_scanned TEXT NOT NULL, "
                             "ext_signature TEXT DEFAULT '', "
                             "uuid TEXT DEFAULT ''"
                             ")";
  if (!query.exec(collectionsTable)) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseQueryFailed,
                                      "Failed to create collections table",
                                      "DatabaseManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }

  QString itemsTable =
      "CREATE TABLE IF NOT EXISTS items ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
      "collection_id INTEGER NOT NULL, "
      "path TEXT NOT NULL, "
      "name TEXT NOT NULL, "
      "artwork_path TEXT, "
      "last_modified TEXT NOT NULL, "
      "play_count INTEGER DEFAULT 0, "
      "last_played TEXT, "
      "rating INTEGER DEFAULT 0, "
      "collection_uuid TEXT DEFAULT '', "
      "UNIQUE(collection_id, path), "
      "FOREIGN KEY(collection_id) REFERENCES collections(id) ON DELETE CASCADE"
      ")";
  if (!query.exec(itemsTable)) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseQueryFailed,
                                      "Failed to create items table",
                                      "DatabaseManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }

  // Ensure any schema upgrades are applied after core tables exist.
  DbMigrations::applySchemaMigrations(m_db, QStringLiteral("DatabaseManager::initDatabase"));

  QSqlQuery idx(m_db);
  idx.prepare("CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)");
  idx.exec();

  idx.prepare("CREATE INDEX IF NOT EXISTS idx_items_collection_uuid ON items(collection_uuid)");
  idx.exec();

  idx.prepare("CREATE UNIQUE INDEX IF NOT EXISTS uniq_items_uuid_path ON items(collection_uuid, path)");
  idx.exec();

  // Index on name for search filtering and ORDER BY name COLLATE NOCASE
  idx.prepare("CREATE INDEX IF NOT EXISTS idx_items_name ON items(name COLLATE NOCASE)");
  idx.exec();

  // Composite index for common query pattern: WHERE collection_uuid IN (...) ORDER BY name
  // This covers filtering by collection UUID and sorting by name in a single index scan
  idx.prepare("CREATE INDEX IF NOT EXISTS idx_items_uuid_name ON items(collection_uuid, name COLLATE NOCASE)");
  idx.exec();
  
  // Covering index for paginated queries: includes path to avoid table lookup
  // Query pattern: SELECT path, collection_uuid FROM items WHERE collection_uuid IN (...)
  //                ORDER BY name COLLATE NOCASE LIMIT ? OFFSET ?
  // This index covers: filtering (uuid), sorting (name), and result columns (path)
  idx.prepare("CREATE INDEX IF NOT EXISTS idx_items_covering ON items(collection_uuid, name COLLATE NOCASE, path)");
  idx.exec();
}







void DatabaseManager::loadAllCollections(const QList<CollectionConfig> &allCollections) {
  emit requestLoadAllCollections(allCollections);
}

void DatabaseManager::loadItemsWithSubcollections(const CollectionContext &context,
                                                  const QList<CollectionConfig> &allCollections) {
  emit requestLoadItemsWithSubcollections(context, allCollections);
}

void DatabaseManager::loadItems(const CollectionContext &context,
                                const QList<CollectionConfig> &allCollections) {
  emit requestLoadItems(context, allCollections);
}

void DatabaseManager::fetchItemCount(const CollectionContext &context,
                                     const QList<CollectionConfig> &allCollections,
                                     const QString &filter,
                                     int requestToken) {
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][DatabaseManager] fetchItemCount: collIndex="
               << context.currentIndex << "filter='" << filter << "'"
               << "token=" << requestToken;
  }
  // Kick off a background scan if needed, but don't block count queries.
  // IMPORTANT: Avoid scheduling scans for every search keystroke.
  // Scans can be expensive and their completion triggers count refreshes that
  // can invalidate in-flight paginated search range loads (causing blank views
  // + high CPU from repeated rebuilds).
  if (filter.trimmed().isEmpty()) {
    emit requestEnsureScannedForContext(context, allCollections);
  }
  emit requestFetchItemCount(context, allCollections, filter, requestToken);
}

void DatabaseManager::fetchItemsRange(const CollectionContext &context, const QList<CollectionConfig> &allCollections, int offset, int limit, const QString &filter) {
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][DatabaseManager] fetchItemsRange: collIndex="
               << context.currentIndex << "offset=" << offset << "limit="
               << limit << "filter='" << filter << "'";
  }
  emit requestFetchItemsRange(context, allCollections, offset, limit, filter);
}

void DatabaseManager::onWorkerItemsLoaded(const QStringList &filePaths,
                                          const QHash<QString, QString> &fileNames,
                                          const QHash<QString, QString> &fileToArtworkDir,
                                          const QHash<QString, QString> &fileToMediaDir,
                                          const QHash<QString, int> &fileToCollectionIndex) {
  QHash<QString, QString> relativeToFullPath;
  relativeToFullPath.reserve(fileNames.size() * 2);

  // Build a fast lookup cache for resolveRelativeFilePath().
  // Keep "first seen" semantics to match the previous linear scan behavior
  // over fileNames (which returns the first match it encounters).
  for (auto it = fileNames.constBegin(); it != fileNames.constEnd(); ++it) {
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

  {
    QMutexLocker locker(&m_dataMutex);
    m_fileToArtworkDir = fileToArtworkDir;
    m_fileToMediaDir = fileToMediaDir;
    m_fileToCollectionIndex = fileToCollectionIndex;
    m_relativeToFullPath = std::move(relativeToFullPath);
  }
  emit itemsLoaded(filePaths, fileNames);
}

void DatabaseManager::onWorkerItemCountLoaded(int count) {
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][DatabaseManager] onWorkerItemCountLoaded:" << count;
  }
  emit itemCountLoaded(count);
}

void DatabaseManager::onWorkerItemCountLoadedWithToken(int count, int requestToken) {
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][DatabaseManager] onWorkerItemCountLoadedWithToken:" << count
               << "token=" << requestToken;
  }
  emit itemCountLoadedWithToken(count, requestToken);

  // Keep legacy listeners working (e.g., MainWindow overlay suppression).
  emit itemCountLoaded(count);
}

void DatabaseManager::onWorkerItemsRangeLoaded(int offset, const QStringList &filePaths, 
                                               const QHash<QString, QString> &fileNames,
                                               const QHash<QString, QString> &fileToArtworkDir,
                                               const QHash<QString, QString> &fileToMediaDir,
                                               const QHash<QString, int> &fileToCollectionIndex) {
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][DatabaseManager] onWorkerItemsRangeLoaded: offset="
               << offset << "paths=" << filePaths.size();
  }
  
  // Merge the directory and collection index mappings from range query into our cache
  // This enables findArtworkDirectoryForFile() and getCollectionIndexForFile() to work for range-loaded items
  if (!fileToArtworkDir.isEmpty() || !fileToMediaDir.isEmpty() || !fileToCollectionIndex.isEmpty()) {
    QMutexLocker locker(&m_dataMutex);
    for (auto it = fileToArtworkDir.constBegin(); it != fileToArtworkDir.constEnd(); ++it) {
      m_fileToArtworkDir.insert(it.key(), it.value());
    }
    for (auto it = fileToMediaDir.constBegin(); it != fileToMediaDir.constEnd(); ++it) {
      m_fileToMediaDir.insert(it.key(), it.value());
    }
    for (auto it = fileToCollectionIndex.constBegin(); it != fileToCollectionIndex.constEnd(); ++it) {
      m_fileToCollectionIndex.insert(it.key(), it.value());
    }
  }
  
  emit itemsRangeLoaded(offset, filePaths, fileNames, fileToArtworkDir, fileToMediaDir, fileToCollectionIndex);
}

void DatabaseManager::cancelScan() {
  if (m_worker) {
    m_worker->requestCancelScan();
  }
  if (m_scanWorker) {
    m_scanWorker->requestCancelScan();
  }
}

// Count items in collection and descendants using uuid identity
auto DatabaseManager::countCollectionRecursive(
    int collectionIndex, const QList<CollectionConfig> &allCollections)
    -> qint64 {
  if (!CollectionUtils::isValidIndex(collectionIndex, &allCollections)) {
    return 0;
  }
  QString expandedMediaDir = PathUtils::validateAndExpandPath(allCollections[collectionIndex].mediaDirectory, allCollections[collectionIndex].name);
  const QString uuid =
      CollectionUtils::computeCollectionUuid(allCollections[collectionIndex].name, expandedMediaDir);
  qint64 total = countCollectionByUuid(uuid);
  QList<int> descendants =
      CollectionUtils::collectDescendantIndices(collectionIndex, allCollections);
  for (int descendantIndex : descendants) {
    QString descExpandedMediaDir = PathUtils::validateAndExpandPath(allCollections[descendantIndex].mediaDirectory, allCollections[descendantIndex].name);
    const QString descendantUuid =
        CollectionUtils::computeCollectionUuid(allCollections[descendantIndex].name, descExpandedMediaDir);
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
    expandedMediaDirs[i] = PathUtils::validateAndExpandPath(allCollections[i].mediaDirectory,
                                                           allCollections[i].name);
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
  // trigger updateCachedCounts() in quick succession (e.g. navigation + filter changes).
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
    m_sessionManager->setCollectionCounts(collections[i], collections,
                                          directCounts[i], recursiveCounts[i]);
  }
  m_sessionManager->saveToDisk();
  emit cachedCountsUpdated();
}

// Get owning collection index for a file based on built maps
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
    return {};
  }
  return QDir(mediaDir).absoluteFilePath(rawEntry);
}

// Resolve a relative file path by searching fileNames map and falling back
// to collection index lookup for media directory resolution.
auto DatabaseManager::resolveRelativeFilePath(
    const QString &rawFileName,
    const QHash<QString, QString> &fileNames) const -> QString {
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

// Public wrapper to invalidate collection cache asynchronously on worker thread
void DatabaseManager::invalidateCollectionCache(const QString &collectionUuid) {
  emit requestInvalidateCache(collectionUuid);
}

// Clear a collection's data by uuid (main thread - only used for legacy sync operations)
void DatabaseManager::clearCollectionFromDatabaseByUuid(
    const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return;
  }

  if (!m_db.transaction()) {
    auto err = ErrorContext::critical(
        ErrorCode::DatabaseTransactionFailed,
        "Failed to start database transaction",
        "DatabaseManager::clearCollectionFromDatabaseByUuid")
        .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    return;
  }

  try {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM items WHERE collection_uuid = ?");
    query.addBindValue(collectionUuid);
    if (!query.exec()) {
      throw std::runtime_error(query.lastError().text().toStdString());
    }

    QSqlQuery delc(m_db);
    delc.prepare("DELETE FROM collections WHERE uuid = ?");
    delc.addBindValue(collectionUuid);
    if (!delc.exec()) {
      throw std::runtime_error(delc.lastError().text().toStdString());
    }

    if (!m_db.commit()) {
      throw std::runtime_error(m_db.lastError().text().toStdString());
    }
  } catch (const std::exception &e) {
    m_db.rollback();
    auto err = ErrorContext::critical(
        ErrorCode::DatabaseTransactionFailed,
        "Failed to clear collection from database",
        "DatabaseManager::clearCollectionFromDatabaseByUuid")
        .withDetails(QString::fromStdString(e.what()));
    ErrorUtils::logError(err);
  }
}

// Count items in a single collection by uuid
auto DatabaseManager::countCollectionByUuid(const QString &collectionUuid)
    -> qint64 {
  if (!m_db.isOpen()) {
    return 0;
  }
  QSqlQuery countQuery(m_db);
  countQuery.prepare(
      "SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid=?");
  countQuery.addBindValue(collectionUuid);
  if (!countQuery.exec() || !countQuery.next()) {
    return 0;
  }
  return countQuery.value(0).toLongLong();
}