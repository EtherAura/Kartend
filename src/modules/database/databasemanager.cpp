// Coordinates SQLite database access via worker thread for collection metadata queries.
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <stdexcept>
#include <QThread>

#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "errorutils.h"
#include "querymanager.h"
#include "pathutils.h"
#include "sessionmanager.h"

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcDatabaseManager, "kartend.databasemanager")
#define debugLog(msg) qCDebug(lcDatabaseManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

// Construct the database manager and initialize the database
DatabaseManager::DatabaseManager(SessionManager *sessionManager, QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager) {
  qRegisterMetaType<CollectionConfig>("CollectionConfig");
  qRegisterMetaType<CollectionContext>("CollectionContext");
  qRegisterMetaType<QList<CollectionConfig>>("QList<CollectionConfig>");

  m_connectionName = "kartend_main";
  initDatabase();

  m_workerThread = new QThread(this);
  m_worker = new QueryManager(m_sessionManager);
  m_worker->moveToThread(m_workerThread);

  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
  
  connect(this, &DatabaseManager::requestLoadAllCollections, m_worker, &QueryManager::loadAllCollections);
  connect(this, &DatabaseManager::requestLoadItems, m_worker, &QueryManager::loadItems);
  connect(this, &DatabaseManager::requestLoadItemsWithSubcollections, m_worker, &QueryManager::loadItemsWithSubcollections);
  connect(this, &DatabaseManager::requestFetchItemCount, m_worker, &QueryManager::fetchItemCount);
  connect(this, &DatabaseManager::requestFetchItemsRange, m_worker, &QueryManager::fetchItemsRange);
  connect(this, &DatabaseManager::requestInvalidateCache, m_worker, &QueryManager::invalidateCollectionCache);
  
  connect(m_worker, &QueryManager::itemsLoaded, this, &DatabaseManager::onWorkerItemsLoaded);
  connect(m_worker, &QueryManager::itemCountLoaded, this, &DatabaseManager::onWorkerItemCountLoaded);
  connect(m_worker, &QueryManager::itemsRangeLoaded, this, &DatabaseManager::onWorkerItemsRangeLoaded);
  connect(m_worker, &QueryManager::errorOccurred, this, &DatabaseManager::errorOccurred);
  connect(m_worker, &QueryManager::scanProgress, this, &DatabaseManager::scanProgress);
  connect(m_worker, &QueryManager::scanStarting, this, &DatabaseManager::scanStarting);
  connect(m_worker, &QueryManager::scanItemsProgress, this, &DatabaseManager::scanItemsProgress);
  connect(m_worker, &QueryManager::cacheInvalidated, this, &DatabaseManager::cacheInvalidated);

  m_workerThread->start();
}

// Destroy the database manager and close/remove the connection
DatabaseManager::~DatabaseManager() {
  if (m_workerThread) {
    m_workerThread->quit();
    m_workerThread->wait();
  }

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
  QDir().mkpath(dbPath);
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

void DatabaseManager::loadItems(const CollectionContext &context) {
  emit requestLoadItems(context);
}

void DatabaseManager::fetchItemCount(const CollectionContext &context, const QList<CollectionConfig> &allCollections, const QString &filter) {
  emit requestFetchItemCount(context, allCollections, filter);
}

void DatabaseManager::fetchItemsRange(const CollectionContext &context, const QList<CollectionConfig> &allCollections, int offset, int limit, const QString &filter) {
  emit requestFetchItemsRange(context, allCollections, offset, limit, filter);
}

void DatabaseManager::onWorkerItemsLoaded(const QStringList &filePaths,
                                          const QHash<QString, QString> &fileNames,
                                          const QHash<QString, QString> &fileToArtworkDir,
                                          const QHash<QString, QString> &fileToMediaDir,
                                          const QHash<QString, int> &fileToCollectionIndex) {
  {
    QMutexLocker locker(&m_dataMutex);
    m_fileToArtworkDir = fileToArtworkDir;
    m_fileToMediaDir = fileToMediaDir;
    m_fileToCollectionIndex = fileToCollectionIndex;
  }
  emit itemsLoaded(filePaths, fileNames);
}

void DatabaseManager::onWorkerItemCountLoaded(int count) {
  emit itemCountLoaded(count);
}

void DatabaseManager::onWorkerItemsRangeLoaded(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames) {
  emit itemsRangeLoaded(offset, filePaths, fileNames);
}

void DatabaseManager::cancelScan() {
  if (m_worker) {
    m_worker->requestCancelScan();
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

  for (const auto &config : allCollections) {
    QString expandedMediaDir = PathUtils::validateAndExpandPath(config.mediaDirectory, config.name);
    if (expandedMediaDir.trimmed().isEmpty()) {
      const QString uuid = CollectionUtils::computeCollectionUuid(config.name, expandedMediaDir);
      clearCollectionFromDatabaseByUuid(uuid);
    }
  }

  qint64 global = countGlobal(allCollections);
  m_sessionManager->setGlobalItemCount(global);

  for (int i = 0; i < allCollections.size(); ++i) {
    QString expandedMediaDir = PathUtils::validateAndExpandPath(allCollections[i].mediaDirectory, allCollections[i].name);
    const QString uuid = CollectionUtils::computeCollectionUuid(allCollections[i].name, expandedMediaDir);
    qint64 direct = countCollectionByUuid(uuid);
    qint64 recursive = countCollectionRecursive(i, allCollections);
    m_sessionManager->setCollectionCounts(allCollections[i], allCollections,
                                          direct, recursive);
  }

  m_sessionManager->saveToDisk();
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
  if (context.config.showAllSubcollectionItems) {
    // Absolute paths pass through directly
    if (QDir::isAbsolutePath(rawEntry)) {
      return rawEntry;
    }
    
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
  // Search fileNames for matching full path
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

  m_db.transaction();

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
    delc.exec();

    m_db.commit();
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