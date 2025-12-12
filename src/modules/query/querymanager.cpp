// Executes SQLite queries on worker thread for paginated item loading and filtering.
#include "querymanager.h"
#include "collectionutils.h"
#include "errorutils.h"
#include "pathutils.h"
#include "sessionmanager.h"
#include "uiconstants.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutex>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QVector>
#include <QtConcurrent>
#include <random>
#include <stdexcept>
#include <QDebug>
#include <algorithm>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcQueryManager, "kartend.querymanager")
#define debugLog(msg) qCDebug(lcQueryManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

// Forward declarations of static helpers
static auto canonicalKeyPath(const QString &absPath, bool dedup,
                             QHash<QString, QString> *canonicalPathCache) -> QString;
static auto displayNameForBase(const QString &baseName) -> QString;

namespace {
class SynchronousPragmaGuard {
public:
  explicit SynchronousPragmaGuard(QSqlDatabase &db) : m_db(db) {}

  SynchronousPragmaGuard(const SynchronousPragmaGuard &) = delete;
  auto operator=(const SynchronousPragmaGuard &)
      -> SynchronousPragmaGuard & = delete;

  SynchronousPragmaGuard(SynchronousPragmaGuard &&) = delete;
  auto operator=(SynchronousPragmaGuard &&) -> SynchronousPragmaGuard & = delete;

  ~SynchronousPragmaGuard() {
    if (!m_db.isOpen()) {
      return;
    }
    QSqlQuery pragmaOn(m_db);
    pragmaOn.exec("PRAGMA synchronous = NORMAL");
  }

private:
  QSqlDatabase &m_db;
};
} // namespace

QueryManager::QueryManager(SessionManager *sessionManager, QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager) {
  m_connectionName = "kartend_worker";

  const int idealThreads = QThread::idealThreadCount();
  const int base = idealThreads > 0 ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
                                     : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_scanThreadPool.setMaxThreadCount(std::clamp(base,
                                               UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                                               UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));
  
  // Register ErrorContext for queued signal/slot connections
  qRegisterMetaType<ErrorUtils::ErrorContext>("ErrorUtils::ErrorContext");
}

QueryManager::~QueryManager() {
  clearStatementCache();
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }
}

void QueryManager::requestCancelScan() {
  m_scanCancelled.store(true, std::memory_order_release);
}

bool QueryManager::isScanCancelled() const {
  return m_scanCancelled.load(std::memory_order_acquire);
}

void QueryManager::resetScanCancellation() {
  m_scanCancelled.store(false, std::memory_order_release);
}

// Gets or creates a prepared statement for the given SQL
// Caches compiled statements to avoid repeated prepare() overhead
// Uses LRU eviction when cache exceeds MAX_STATEMENT_CACHE_SIZE
auto QueryManager::getPreparedStatement(const QString &sql) -> QSqlQuery & {
  auto it = m_statementCache.find(sql);
  if (it != m_statementCache.end()) {
    // Clear previous bindings before reuse
    it->finish();
    // Update LRU order: move to back (most recently used)
    m_statementAccessOrder.removeOne(sql);
    m_statementAccessOrder.append(sql);
    return *it;
  }
  
  // Evict oldest entry if cache is at capacity
  if (m_statementCache.size() >= MAX_STATEMENT_CACHE_SIZE && !m_statementAccessOrder.isEmpty()) {
    QString oldest = m_statementAccessOrder.takeFirst();
    auto oldIt = m_statementCache.find(oldest);
    if (oldIt != m_statementCache.end()) {
      oldIt->finish();
      m_statementCache.erase(oldIt);
    }
  }
  
  // Create new prepared statement and cache it
  QSqlQuery query(m_db);
  query.prepare(sql);
  m_statementCache.insert(sql, query);
  m_statementAccessOrder.append(sql);
  return m_statementCache[sql];
}

// Clears statement cache - call when database connection changes
void QueryManager::clearStatementCache() {
  for (auto &query : m_statementCache) {
    query.finish();
  }
  m_statementCache.clear();
  m_statementAccessOrder.clear();
}

// Attempts to reconnect to the database if connection was lost
// Used to handle transient SQLite errors (disk full, I/O errors, etc.)
// Returns true if database is now open, false otherwise
// NOTE: Only attempts reconnection if database was previously initialized
//       (has valid driver). Returns false for uninitialized databases.
auto QueryManager::ensureDatabaseConnection() -> bool {
  static constexpr int MAX_RECONNECT_ATTEMPTS = 3;
  static constexpr int RECONNECT_DELAY_MS = 100;
  
  if (m_db.isOpen()) {
    return true;
  }
  
  // Don't attempt reconnection if database was never initialized
  // (no driver means initDatabase() hasn't been called yet)
  if (!m_db.isValid() || m_db.driverName().isEmpty()) {
    return false;
  }
  
  auto logReconnectAttempt = [this](int attempt) {
    auto info = ErrorContext::info(
        ErrorCode::DatabaseConnectionLost,
        QString("Database connection lost, attempting reconnection (%1/%2)")
            .arg(attempt)
            .arg(MAX_RECONNECT_ATTEMPTS),
        "QueryManager::ensureDatabaseConnection");
    ErrorUtils::logError(info);
  };
  
  for (int attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS; ++attempt) {
    logReconnectAttempt(attempt);
    
    // Close and clear the old connection state
    clearStatementCache();
    m_db.close();
    
    // Try to reopen
    if (m_db.open()) {
      auto success = ErrorContext::info(
          ErrorCode::DatabaseConnectionRestored,
          QString("Database reconnection successful on attempt %1").arg(attempt),
          "QueryManager::ensureDatabaseConnection");
      ErrorUtils::logError(success);
      
      // Re-initialize PRAGMAs after reconnection
      QSqlQuery query(m_db);
      query.exec("PRAGMA foreign_keys = ON");
      query.exec("PRAGMA journal_mode = WAL");
      query.exec("PRAGMA synchronous = NORMAL");
      
      return true;
    }
    
    // Wait before next attempt (unless it's the last one)
    if (attempt < MAX_RECONNECT_ATTEMPTS) {
      QThread::msleep(RECONNECT_DELAY_MS);
    }
  }
  
  // All attempts failed
  auto err = ErrorContext::critical(
      ErrorCode::DatabaseConnectionFailed,
      QString("Failed to reconnect to database after %1 attempts")
          .arg(MAX_RECONNECT_ATTEMPTS),
      "QueryManager::ensureDatabaseConnection")
      .withDetails(m_db.lastError().text());
  ErrorUtils::logError(err);
  emit errorOccurred(err);
  
  return false;
}

void QueryManager::initDatabase() {
  if (QSqlDatabase::contains(m_connectionName)) {
    m_db = QSqlDatabase::database(m_connectionName);
  } else {
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
  }

  QString dbPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dbPath);
  m_db.setDatabaseName(dbPath + "/media.db");

  if (!m_db.open()) {
    auto err = ErrorContext::critical(
        ErrorCode::DatabaseConnectionFailed,
        "Failed to open database",
        "QueryManager::initDatabase")
        .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  QSqlQuery query(m_db);
  if (!query.exec("PRAGMA foreign_keys = ON")) {
    auto err = ErrorContext::warning(
        ErrorCode::DatabaseQueryFailed,
        "Failed to enable foreign keys",
        "QueryManager::initDatabase")
        .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  if (!query.exec("PRAGMA journal_mode = WAL")) {
    auto err = ErrorContext::warning(
        ErrorCode::DatabaseQueryFailed,
        "Failed to enable WAL mode",
        "QueryManager::initDatabase")
        .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  // Use NORMAL synchronous mode for better write performance while maintaining
  // data safety - WAL mode already provides crash recovery guarantees
  if (!query.exec("PRAGMA synchronous = NORMAL")) {
    auto err = ErrorContext::warning(
        ErrorCode::DatabaseQueryFailed,
        "Failed to set synchronous mode",
        "QueryManager::initDatabase")
        .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  // Tables are created by the main DatabaseManager or assumed to exist.
  // But for safety, we can ensure they exist here too, or rely on main thread init.
  // Since main thread runs initDatabase first, we should be fine.
}

void QueryManager::loadAllCollections(const QList<CollectionConfig> &allCollections) {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      return;
    }
  }

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  const int totalCollections = allCollections.size();
  
  for (int collectionIndex = 0; collectionIndex < allCollections.size();
       ++collectionIndex) {
    CollectionConfig collection = allCollections[collectionIndex];
    
    collection.mediaDirectory = PathUtils::validateAndExpandPath(
        collection.mediaDirectory, collection.name);
    collection.artworkDirectory = PathUtils::validateAndExpandPath(
        collection.artworkDirectory, collection.name);

    if (collection.mediaDirectory.trimmed().isEmpty()) {
      continue;
    }

    // Only emit progress signal when a scan is actually needed (not for cached loads)
    if (needsRescan(collectionIndex, collection)) {
      emit scanProgress(collectionIndex + 1, totalCollections, collection.name);
    }

    QHash<QString, QDateTime> timestamps;
    QStringList filePaths =
        loadOrScanCollection(collectionIndex, collection, timestamps);

    appendFileMapsAndListCanonical(
        collectionIndex, collection,
        allCollections[collectionIndex].artworkDirectory, filePaths,
        allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
        fileToCollectionIndex, false);
  }

  sortFiles(allFilePaths);

  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir, fileToCollectionIndex);
}

void QueryManager::loadItems(const CollectionContext &context) {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      return;
    }
  }

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context",
                                   "QueryManager::loadItems");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory = PathUtils::validateAndExpandPath(
      ctx.config.artworkDirectory, ctx.config.name);

  if (ctx.config.mediaDirectory.trimmed().isEmpty()) {
    emit itemsLoaded(QStringList(), QHash<QString, QString>(), QHash<QString, QString>(), QHash<QString, QString>(), QHash<QString, int>());
    return;
  }

  QHash<QString, QDateTime> timestamps;
  QStringList filePaths =
      loadOrScanCollection(ctx.currentIndex, ctx.config, timestamps);

  // Apply subfolder filtering
  const QString &subfolder = ctx.config.currentSubfolder;
  if (!subfolder.isEmpty()) {
    // In a subfolder - show only items whose path starts with subfolder/
    const QString prefix = subfolder + "/";
    QStringList filtered;
    for (const QString &path : filePaths) {
      if (path.startsWith(prefix)) {
        filtered.append(path);
      }
    }
    filePaths = filtered;
  } else if (ctx.config.includeContentSubfolders && !ctx.config.showAllSubfolderItems) {
    // At root with subfolders enabled but NOT showing all items - exclude items in subfolders
    QStringList filtered;
    for (const QString &path : filePaths) {
      if (!path.contains('/')) {
        filtered.append(path);
      }
    }
    filePaths = filtered;
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown mixed together

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  appendFileMapsAndListCanonical(ctx.currentIndex, ctx.config,
                                 ctx.config.artworkDirectory, filePaths,
                                 allFilePaths, allFileNames, fileToArtworkDir,
                                 fileToMediaDir, fileToCollectionIndex, false);

  sortFiles(allFilePaths, ctx.sortMode);

  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir, fileToCollectionIndex);
}

void QueryManager::loadItemsWithSubcollections(const CollectionContext &context,
                                                 const QList<CollectionConfig> &allCollections) {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      return;
    }
  }

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context",
                                   "QueryManager::loadItemsWithSubcollections");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext mainCtx = context;
  mainCtx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      mainCtx.config.mediaDirectory, mainCtx.config.name);
  mainCtx.config.artworkDirectory = PathUtils::validateAndExpandPath(
      mainCtx.config.artworkDirectory, mainCtx.config.name);

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  QSet<QString> seenCanonicalPaths;
  QHash<QString, QString> canonicalPathCache;

  bool hasMainMediaDirectory =
      !mainCtx.config.mediaDirectory.trimmed().isEmpty();
  if (hasMainMediaDirectory) {
    QHash<QString, QDateTime> timestamps;
    QStringList mainFilePaths =
        loadOrScanCollection(mainCtx.currentIndex, mainCtx.config, timestamps);

    // Apply subfolder filtering for the main collection
    const QString &subfolder = mainCtx.config.currentSubfolder;
    if (!subfolder.isEmpty()) {
      // In a subfolder - show only items whose path starts with subfolder/
      const QString prefix = subfolder + "/";
      QStringList filtered;
      for (const QString &path : mainFilePaths) {
        if (path.startsWith(prefix)) {
          filtered.append(path);
        }
      }
      mainFilePaths = filtered;
    } else if (mainCtx.config.includeContentSubfolders && !mainCtx.config.showAllSubfolderItems) {
      // At root with subfolders enabled but NOT showing all items - exclude items in subfolders
      QStringList filtered;
      for (const QString &path : mainFilePaths) {
        if (!path.contains('/')) {
          filtered.append(path);
        }
      }
      mainFilePaths = filtered;
    }

    seenCanonicalPaths.reserve(mainFilePaths.size());
    canonicalPathCache.reserve(mainFilePaths.size());

    appendFileMapsAndListCanonical(
      mainCtx.currentIndex, mainCtx.config, mainCtx.config.artworkDirectory,
      mainFilePaths, allFilePaths, allFileNames, fileToArtworkDir,
      fileToMediaDir, fileToCollectionIndex, true, &seenCanonicalPaths,
      &canonicalPathCache);
  }

  QList<int> rawDescendants =
      CollectionUtils::collectDescendantIndices(mainCtx.currentIndex, allCollections);
  QSet<int> seenDesc;
  QList<int> descendants;
  descendants.reserve(rawDescendants.size());
  for (int descendantIndex : rawDescendants) {
    if (descendantIndex == mainCtx.currentIndex) {
      continue;
    }
    if (descendantIndex < 0 || descendantIndex >= allCollections.size()) {
      continue;
    }
    if (!seenDesc.contains(descendantIndex)) {
      seenDesc.insert(descendantIndex);
      descendants.append(descendantIndex);
    }
  }

  for (int collectionIndex : descendants) {
    CollectionConfig collection = allCollections[collectionIndex];
    collection.mediaDirectory = PathUtils::validateAndExpandPath(
        collection.mediaDirectory, collection.name);
    collection.artworkDirectory = PathUtils::validateAndExpandPath(
        collection.artworkDirectory, collection.name);

    if (collection.mediaDirectory.trimmed().isEmpty()) {
      continue;
    }

    QHash<QString, QDateTime> subTimestamps;
    QStringList subFilePaths =
        loadOrScanCollection(collectionIndex, collection, subTimestamps);

    appendFileMapsAndListCanonical(
        collectionIndex, collection,
        allCollections[collectionIndex].artworkDirectory, subFilePaths,
        allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
        fileToCollectionIndex, true, &seenCanonicalPaths, &canonicalPathCache);
  }

  sortFiles(allFilePaths, context.sortMode);
  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir, fileToCollectionIndex);
}

void QueryManager::updateCachedCounts(quint64 generation,
                                     const QStringList &collectionUuids) {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      emit cachedCountsComputed(generation, 0, {});
      return;
    }
  }

  qint64 globalCount = 0;
  {
    QSqlQuery globalQuery(m_db);
    if (globalQuery.exec("SELECT COUNT(*) FROM items") && globalQuery.next()) {
      globalCount = globalQuery.value(0).toLongLong();
    }
  }

  QHash<QString, qint64> directCountsByUuid;
  directCountsByUuid.reserve(collectionUuids.size());

  if (!collectionUuids.isEmpty()) {
    const QString clause = buildUuidInClause(collectionUuids.size());
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT collection_uuid, COUNT(DISTINCT path) "
        "FROM items WHERE collection_uuid IN " + clause +
        " GROUP BY collection_uuid");
    for (const QString &uuid : collectionUuids) {
      query.addBindValue(uuid);
    }

    if (query.exec()) {
      while (query.next()) {
        directCountsByUuid.insert(query.value(0).toString(),
                                  query.value(1).toLongLong());
      }
    }
  }

  emit cachedCountsComputed(generation, globalCount, directCountsByUuid);
}

// Collects UUIDs for a collection and optionally its descendants
auto QueryManager::collectCollectionUuids(const CollectionContext &ctx,
                                          const QList<CollectionConfig> &allCollections) -> QStringList {
  QStringList uuids;
  uuids << CollectionUtils::computeCollectionUuid(ctx.config.name, ctx.config.mediaDirectory);

  if (ctx.config.showAllSubcollectionItems) {
    QList<int> rawDescendants =
        CollectionUtils::collectDescendantIndices(ctx.currentIndex, allCollections);
    for (int descendantIndex : rawDescendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }
      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory =
          PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      uuids << CollectionUtils::computeCollectionUuid(subCol.name, subCol.mediaDirectory);
    }
  }
  return uuids;
}

// Builds UUID-to-directory mappings for resolving paths from query results
auto QueryManager::buildDirectoryMaps(const CollectionContext &ctx,
                                      const QList<CollectionConfig> &allCollections) -> CollectionDirMaps {
  CollectionDirMaps maps;

  auto addMapping = [&](const CollectionConfig &c) {
    QString uuid = CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
    maps.uuidToMediaDir[uuid] = c.mediaDirectory;
    maps.uuidToArtworkDir[uuid] = c.artworkDirectory;
  };

  addMapping(ctx.config);

  if (ctx.config.showAllSubcollectionItems) {
    QList<int> rawDescendants =
        CollectionUtils::collectDescendantIndices(ctx.currentIndex, allCollections);
    for (int descendantIndex : rawDescendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }
      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory =
          PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      subCol.artworkDirectory =
          PathUtils::validateAndExpandPath(subCol.artworkDirectory, subCol.name);
      addMapping(subCol);
    }
  }
  return maps;
}

// Builds SQL IN clause with placeholders: "(?, ?, ...)"
auto QueryManager::buildUuidInClause(int uuidCount) -> QString {
  QString clause = "(";
  for (int i = 0; i < uuidCount; ++i) {
    clause += (i == 0 ? "?" : ", ?");
  }
  clause += ")";
  return clause;
}

void QueryManager::ensureCollectionScanned(int collectionIndex, const CollectionConfig &collection) {
  if (collection.mediaDirectory.trimmed().isEmpty()) return;
  if (needsRescan(collectionIndex, collection)) {
    // Reset cancellation flag before starting new scan
    resetScanCancellation();
    
    // Notify UI that a scan is starting (estimated items unknown, use -1)
    emit scanStarting(collection.name, -1);
    
    QHash<QString, QDateTime> timestamps;
    QStringList filePaths = scanMediaDirectory(collection, timestamps);
    
    // Only save if we got files and weren't cancelled
    if (!filePaths.isEmpty() && !isScanCancelled()) {
      saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection);
    }
  }
}

void QueryManager::fetchItemCount(const CollectionContext &context, const QList<CollectionConfig> &allCollections, const QString &filter) {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      emit itemCountLoaded(0);
      return;
    }
  }

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context",
                                   "QueryManager::fetchItemCount");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);
  
  ensureCollectionScanned(ctx.currentIndex, ctx.config);

  // Ensure subcollections are scanned if showing all items
  if (ctx.config.showAllSubcollectionItems) {
    QList<int> rawDescendants = CollectionUtils::collectDescendantIndices(ctx.currentIndex, allCollections);
    for (int descendantIndex : rawDescendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }
      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      ensureCollectionScanned(descendantIndex, subCol);
    }
  }

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  QString sql = "SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid IN "
                + buildUuidInClause(uuids.size());
  
  // Apply subfolder filtering when browsing subfolders
  const QString &subfolder = ctx.config.currentSubfolder;
  if (!subfolder.isEmpty()) {
    // In a subfolder - show only items whose path starts with subfolder/
    sql += " AND path LIKE ?";
  } else if (ctx.config.includeContentSubfolders && !ctx.config.showAllSubfolderItems) {
    // At root with subfolders enabled but NOT showing all items - exclude items in subfolders
    sql += " AND path NOT LIKE '%/%'";
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown mixed together
  
  if (!filter.isEmpty()) {
    sql += " AND name LIKE ?";
  }
  
  // Use cached prepared statement - dynamic SQL is cached by query string
  QSqlQuery &query = getPreparedStatement(sql);
  for (const QString &uuid : uuids) {
    query.addBindValue(uuid);
  }
  if (!subfolder.isEmpty()) {
    query.addBindValue(subfolder + "/%");
  }
  if (!filter.isEmpty()) {
    query.addBindValue("%" + filter + "%");
  }

  if (query.exec() && query.next()) {
    emit itemCountLoaded(query.value(0).toInt());
  } else {
    emit itemCountLoaded(0);
  }
}

void QueryManager::fetchItemsRange(const CollectionContext &context, const QList<CollectionConfig> &allCollections, int offset, int limit, const QString &filter) {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      emit itemsRangeLoaded(offset, QStringList(), QHash<QString, QString>());
      return;
    }
  }

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context",
                                   "QueryManager::fetchItemsRange");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  // Note: We assume ensureCollectionScanned was called during fetchItemCount.
  // For performance, we don't re-scan here.

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory = PathUtils::validateAndExpandPath(ctx.config.artworkDirectory, ctx.config.name);

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  CollectionDirMaps dirMaps = buildDirectoryMaps(ctx, allCollections);

  QString sql = "SELECT DISTINCT path, collection_uuid FROM items WHERE collection_uuid IN "
                + buildUuidInClause(uuids.size());
  
  // Apply subfolder filtering when browsing subfolders
  const QString &subfolder = ctx.config.currentSubfolder;
  if (!subfolder.isEmpty()) {
    // In a subfolder - show only items whose path starts with subfolder/
    sql += " AND path LIKE ?";
  } else if (ctx.config.includeContentSubfolders && !ctx.config.showAllSubfolderItems) {
    // At root with subfolders enabled but NOT showing all items - exclude items in subfolders
    sql += " AND path NOT LIKE '%/%'";
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown mixed together
  
  if (!filter.isEmpty()) {
    sql += " AND name LIKE ?";
  }
  sql += " ORDER BY name COLLATE NOCASE LIMIT ? OFFSET ?";
  
  // Use cached prepared statement - dynamic SQL is cached by query string
  QSqlQuery &query = getPreparedStatement(sql);
  for (const QString &uuid : uuids) {
    query.addBindValue(uuid);
  }
  if (!subfolder.isEmpty()) {
    query.addBindValue(subfolder + "/%");
  }
  if (!filter.isEmpty()) {
    query.addBindValue("%" + filter + "%");
  }
  query.addBindValue(limit);
  query.addBindValue(offset);

  QStringList filePaths;
  QHash<QString, QString> fileNames;

  if (query.exec()) {
    while (query.next()) {
      QString relPath = query.value(0).toString();
      QString uuid = query.value(1).toString();
      QString mediaDir = dirMaps.uuidToMediaDir.value(uuid);
      
      QString fullPath;
      if (QDir::isAbsolutePath(relPath)) {
        fullPath = relPath;
      } else {
        fullPath = QDir(mediaDir).absoluteFilePath(relPath);
      }
      
      QString keyPath = canonicalKeyPath(fullPath, false, nullptr);
      filePaths.append(keyPath);
      fileNames[keyPath] = displayNameForBase(QFileInfo(keyPath).completeBaseName());
    }
  } else {
    auto err = ErrorContext::error(
        ErrorCode::DatabaseQueryFailed,
        "Fetch items range failed",
        "QueryManager::fetchItemsRange")
        .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
    emit errorOccurred(err);
  }

  emit itemsRangeLoaded(offset, filePaths, fileNames);
}

// ... Helper implementations ...

// SQL constants for prepared statement caching
namespace QuerySQL {
  constexpr const char* COLLECTION_INFO = 
      "SELECT last_scanned, name, ext_signature FROM collections WHERE uuid = ?";
  constexpr const char* ITEM_PATH_CHECK = 
      "SELECT path FROM items WHERE collection_uuid = ? LIMIT 1";
  constexpr const char* ITEMS_MODIFIED_COUNT = 
      "SELECT COUNT(*) FROM items WHERE collection_uuid = ? AND last_modified > ?";
  constexpr const char* ITEMS_COUNT_BY_UUID = 
      "SELECT COUNT(*) FROM items WHERE collection_uuid = ?";
  constexpr const char* DELETE_ITEMS_BY_UUID = 
      "DELETE FROM items WHERE collection_uuid = ?";
  constexpr const char* DELETE_COLLECTION_BY_UUID = 
      "DELETE FROM collections WHERE uuid = ?";
  constexpr const char* LOAD_ITEMS_BY_UUID = 
      "SELECT DISTINCT path FROM items WHERE collection_uuid = ? ORDER BY name COLLATE NOCASE";
}

bool QueryManager::needsRescan(int collectionIndex, const CollectionConfig &collection) {
  Q_UNUSED(collectionIndex)

  if (collection.mediaDirectory.trimmed().isEmpty()) {
    if (m_db.isOpen()) {
      const QString uuid = CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);
      clearCollectionFromDatabaseByUuid(uuid);
    }
    return false;
  }

  // Include includeContentSubfolders in the signature - changing it requires rescan
  QString currentSignature = collection.extensions.isEmpty()
                                 ? QString()
                                 : collection.extensions.join('|');
  currentSignature += collection.includeContentSubfolders ? "|subfolders" : "";
  
  const QString uuid = CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Use cached prepared statement for collection info lookup
  QSqlQuery &query = getPreparedStatement(QuerySQL::COLLECTION_INFO);
  query.addBindValue(uuid);

  bool rowPresent = query.exec() && query.next();
  if (!rowPresent) {
    return true;
  }

  QString storedName = query.value(1).toString();
  QString storedSignature = query.value(2).toString();

  // Capture lastScanned before any early returns that might invalidate query state
  QDateTime lastScanned =
      QDateTime::fromString(query.value(0).toString(), Qt::ISODate);

  if (storedName != collection.name) {
    clearCollectionFromDatabaseByUuid(uuid);
    return true;
  }

  if (storedSignature != currentSignature) {
    clearCollectionFromDatabaseByUuid(uuid);
    return true;
  }

  // Use cached prepared statement for item path check
  QSqlQuery &pathQuery = getPreparedStatement(QuerySQL::ITEM_PATH_CHECK);
  pathQuery.addBindValue(uuid);

  if (pathQuery.exec() && pathQuery.next()) {
    QString storedPath = pathQuery.value(0).toString();
    QString storedFullPath =
        QDir(collection.mediaDirectory).absoluteFilePath(storedPath);
    if (!QFile::exists(storedFullPath)) {
      clearCollectionFromDatabaseByUuid(uuid);
      return true;
    }
  }

  QFileInfo dirInfo(collection.mediaDirectory);

  if (!dirInfo.exists() || dirInfo.lastModified() > lastScanned) {
    return true;
  }
  
  // When includeContentSubfolders is enabled, check for subdirectory modifications.
  // For large collections, this check is expensive (iterates all subdirs). 
  // Skip deep check if collection has items in DB - trust cached data on startup.
  // Full validation happens when user navigates into subfolders or forces refresh.
  if (collection.includeContentSubfolders) {
    // Quick check: if we have items in the database, trust the cache
    QSqlQuery &countQuery = getPreparedStatement(QuerySQL::ITEMS_COUNT_BY_UUID);
    countQuery.addBindValue(uuid);
    if (countQuery.exec() && countQuery.next() && countQuery.value(0).toInt() > 0) {
      // Collection has cached items - skip expensive subdirectory scan
      return false;
    }
    
    // No cached items - need to scan (first run or after cache clear)
    // Still limit subdirectory check to avoid startup hang
    constexpr int MAX_SUBDIR_CHECK = 100;
    int checked = 0;
    QDirIterator dirIt(collection.mediaDirectory, QDir::Dirs | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
    while (dirIt.hasNext() && checked < MAX_SUBDIR_CHECK) {
      dirIt.next();
      QFileInfo subDirInfo(dirIt.filePath());
      if (subDirInfo.lastModified() > lastScanned) {
        return true;
      }
      ++checked;
    }
  }

  // Use cached prepared statement for modified items count
  QSqlQuery &newer = getPreparedStatement(QuerySQL::ITEMS_MODIFIED_COUNT);
  newer.addBindValue(uuid);
  newer.addBindValue(lastScanned.toString(Qt::ISODate));
  newer.exec();

  return newer.next() && newer.value(0).toInt() > 0;
}

// Result struct for parallel directory scanning
// Holds files found in a single directory plus their timestamps
struct DirectoryScanResult {
  QStringList relativePaths;
  QHash<QString, QDateTime> timestamps;
};

// Scans a single directory (non-recursively) for matching files
// Thread-safe: operates only on local data structures
static DirectoryScanResult scanSingleDirectory(
    const QString &dirPath,
    const QString &rootPath,
    const QStringList &nameFilters,
    const std::atomic<bool> &cancelled) {
  DirectoryScanResult result;
  
  if (cancelled.load(std::memory_order_acquire)) {
    return result;
  }
  
  QDir rootDir(rootPath);
  QDirIterator iterator(dirPath, nameFilters, QDir::Files, QDirIterator::NoIteratorFlags);
  
  while (iterator.hasNext()) {
    if (cancelled.load(std::memory_order_acquire)) {
      result.relativePaths.clear();
      result.timestamps.clear();
      return result;
    }
    
    iterator.next();
    QString relativePath = rootDir.relativeFilePath(iterator.filePath());
    result.relativePaths.append(relativePath);
    result.timestamps[relativePath] = QFileInfo(iterator.filePath()).lastModified();
  }
  
  return result;
}

// Collects all directories to scan (root + all subdirectories)
static QStringList collectDirectoriesToScan(const QString &rootPath) {
  QStringList directories;
  directories.append(rootPath);
  
  QDirIterator dirIterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot, 
                           QDirIterator::Subdirectories);
  while (dirIterator.hasNext()) {
    dirIterator.next();
    directories.append(dirIterator.filePath());
  }
  
  return directories;
}

QStringList QueryManager::scanMediaDirectory(const CollectionConfig &collection,
                                         QHash<QString, QDateTime> &timestamps) {
  QStringList filePaths;
  QDir dir(collection.mediaDirectory);

  if (!dir.exists()) {
    return filePaths;
  }

  QStringList nameFilters;
  if (!collection.extensions.isEmpty()) {
    for (const QString &ext : collection.extensions) {
      nameFilters << "*." + ext;
    }
  }

  // For non-recursive scans or small directories, use sequential scanning
  // Parallel scanning has overhead that only pays off with multiple directories
  if (!collection.includeContentSubfolders) {
    // Sequential scan for flat directories (original behavior)
    constexpr int PROGRESS_REPORT_INTERVAL = 500;
    int itemsScanned = 0;
    
    QDirIterator iterator(dir.absolutePath(), nameFilters, QDir::Files, 
                          QDirIterator::NoIteratorFlags);
    while (iterator.hasNext()) {
      if (isScanCancelled()) {
        filePaths.clear();
        timestamps.clear();
        return filePaths;
      }
      
      iterator.next();
      QString relativePath = dir.relativeFilePath(iterator.filePath());
      filePaths.append(relativePath);
      timestamps[relativePath] = QFileInfo(iterator.filePath()).lastModified();
      
      ++itemsScanned;
      if (itemsScanned % PROGRESS_REPORT_INTERVAL == 0) {
        emit scanItemsProgress(itemsScanned, -1);
      }
    }
    
    if (itemsScanned > 0) {
      emit scanItemsProgress(itemsScanned, -1);
    }
    return filePaths;
  }

  // Parallel scanning for recursive directory structures
  // Step 1: Collect all directories to scan
  QStringList directories = collectDirectoriesToScan(dir.absolutePath());
  
  if (isScanCancelled()) {
    return filePaths;
  }
  
  // Step 2: Scan directories in parallel using QtConcurrent
  const QString rootPath = dir.absolutePath();
  const std::atomic<bool> &cancelFlag = m_scanCancelled;

  // Use a dedicated pool to avoid global QtConcurrent threadpool contention.
  QVector<QFuture<DirectoryScanResult>> futures;
  futures.reserve(directories.size());

  for (const QString &dirPath : directories) {
    if (cancelFlag.load(std::memory_order_acquire)) {
      break;
    }
    futures.append(QtConcurrent::run(&m_scanThreadPool, [dirPath, rootPath, nameFilters, &cancelFlag]() {
      return scanSingleDirectory(dirPath, rootPath, nameFilters, cancelFlag);
    }));
  }

  int totalItemsScanned = 0;
  constexpr int PROGRESS_REPORT_INTERVAL = 500;
  int lastReportedCount = 0;

  for (QFuture<DirectoryScanResult> &future : futures) {
    future.waitForFinished();
    if (cancelFlag.load(std::memory_order_acquire)) {
      filePaths.clear();
      timestamps.clear();
      return filePaths;
    }

    const DirectoryScanResult result = future.result();
    if (result.relativePaths.isEmpty()) {
      continue;
    }

    filePaths.append(result.relativePaths);
    for (auto it = result.timestamps.constBegin(); it != result.timestamps.constEnd(); ++it) {
      timestamps.insert(it.key(), it.value());
    }

    totalItemsScanned += result.relativePaths.size();
    if (totalItemsScanned - lastReportedCount >= PROGRESS_REPORT_INTERVAL) {
      lastReportedCount = totalItemsScanned;
      emit scanItemsProgress(totalItemsScanned, -1);
    }
  }
  
  // Check if cancelled during parallel scan
  if (isScanCancelled()) {
    filePaths.clear();
    timestamps.clear();
    return filePaths;
  }
  
  // Emit final progress
  if (totalItemsScanned > 0) {
    emit scanItemsProgress(totalItemsScanned, -1);
  }

  return filePaths;
}

QStringList QueryManager::loadOrScanCollection(
    int collectionIndex, const CollectionConfig &collection,
    QHash<QString, QDateTime> &timestamps) {
  QStringList filePaths;
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return filePaths;
  }

  if (needsRescan(collectionIndex, collection)) {
    filePaths = scanMediaDirectory(collection, timestamps);
    if (!filePaths.isEmpty()) {
      saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection);
    }
  } else {
    const QString uuid = CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);
    filePaths = loadItemsFromDatabaseByUuid(uuid);
  }

  return filePaths;
}

void QueryManager::saveItemsToDatabase(
    int collectionIndex, const QStringList &filePaths,
    const QHash<QString, QDateTime> &timestamps,
    const CollectionConfig &collection) {
  Q_UNUSED(collectionIndex)

  if (!m_db.isOpen()) {
    auto err = ErrorContext::error(
        ErrorCode::DatabaseNotOpen,
        "Database is not open",
        "QueryManager::saveItemsToDatabase");
    ErrorUtils::logError(err);
    return;
  }

  // Include includeContentSubfolders in the signature to match needsRescan
  QString extSignature = collection.extensions.isEmpty()
                             ? QString()
                             : collection.extensions.join('|');
  extSignature += collection.includeContentSubfolders ? "|subfolders" : "";
  
  const QString uuid = CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Temporarily disable synchronous writes for bulk insert performance
  QSqlQuery pragmaOff(m_db);
  pragmaOff.exec("PRAGMA synchronous = OFF");
  const SynchronousPragmaGuard restoreSynchronous(m_db);

  // Batch insert for performance - SQLite handles up to 999 variables per statement
  // With 5 columns per row, we can insert 199 rows per batch (995 variables)
  constexpr int BATCH_SIZE = 199;
  // Commit every N batches to save incremental progress (~100K items per commit)
  constexpr int COMMIT_INTERVAL_BATCHES = 500;
  constexpr int PROGRESS_REPORT_INTERVAL = 50000;  // Report every 50K items
  
  // Retry constants for lock handling
  constexpr int MAX_RETRIES = 5;
  constexpr int BASE_DELAY_MS = 100;
  
  const int totalItems = filePaths.size();
  int legacyId = -1;

  // First transaction: create/update collection row and clear old items
  // With retry logic for database lock scenarios
  bool prepareSuccess = false;
  for (int attempt = 0; attempt < MAX_RETRIES && !prepareSuccess; ++attempt) {
    if (attempt > 0) {
      // Exponential backoff: 100ms, 200ms, 400ms, 800ms, 1600ms
      QThread::msleep(BASE_DELAY_MS * (1 << (attempt - 1)));
    }
    
    if (!m_db.transaction()) {
      continue;  // Retry if can't start transaction
    }
    
    try {
      QSqlQuery update(m_db);
      update.prepare("UPDATE collections SET name=?, last_scanned=?, "
                     "ext_signature=? WHERE uuid=?");
      update.addBindValue(collection.name);
      update.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
      update.addBindValue(extSignature);
      update.addBindValue(uuid);
      update.exec();

      QSqlQuery check(m_db);
      check.prepare("SELECT COUNT(*) FROM collections WHERE uuid=?");
      check.addBindValue(uuid);
      bool exists = (check.exec() && check.next() && check.value(0).toInt() > 0);

      if (!exists) {
        QSqlQuery insert(m_db);
        insert.prepare("INSERT INTO collections (name, last_scanned, "
                       "ext_signature, uuid) VALUES (?, ?, ?, ?)");
        insert.addBindValue(collection.name);
        insert.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        insert.addBindValue(extSignature);
        insert.addBindValue(uuid);
        if (!insert.exec()) {
          throw std::runtime_error(insert.lastError().text().toStdString());
        }
      }
      QSqlQuery del(m_db);
      del.prepare("DELETE FROM items WHERE collection_uuid = ?");
      del.addBindValue(uuid);
      if (!del.exec()) {
        throw std::runtime_error(del.lastError().text().toStdString());
      }

      {
        QSqlQuery idq(m_db);
        idq.prepare("SELECT id FROM collections WHERE uuid = ?");
        idq.addBindValue(uuid);
        if (idq.exec() && idq.next()) {
          legacyId = idq.value(0).toInt();
        }
      }

      m_db.commit();
      prepareSuccess = true;  // Exit retry loop
    } catch (const std::exception &e) {
      m_db.rollback();
      
      QString errorText = QString::fromStdString(e.what());
      bool isLockError = errorText.contains("locked", Qt::CaseInsensitive);
      
      if (!isLockError || attempt == MAX_RETRIES - 1) {
        // Non-lock error or final attempt - log and give up
        auto err = ErrorContext::critical(
            ErrorCode::DatabaseTransactionFailed,
            "Failed to prepare collection for items",
            "QueryManager::saveItemsToDatabase")
            .withDetails(errorText);
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        return;
      }
      // Lock error - will retry
    }
  }
  
  if (!prepareSuccess) {
    return;  // All retries failed
  }

  // Second phase: insert items in batches with periodic commits
  int batchesSinceCommit = 0;
  bool inTransaction = false;
  int itemsInserted = 0;
  
  for (int batchStart = 0; batchStart < totalItems; batchStart += BATCH_SIZE) {
    // Check for cancellation between batches
    if (isScanCancelled()) {
      if (inTransaction) {
        m_db.commit();  // Save partial progress
      }
      break;
    }
    
    // Start new transaction if needed
    if (!inTransaction) {
      m_db.transaction();
      inTransaction = true;
      batchesSinceCommit = 0;
    }
    
    const int batchEnd = qMin(batchStart + BATCH_SIZE, totalItems);
    const int batchCount = batchEnd - batchStart;
    
    // Build multi-row INSERT statement
    QString sql = "INSERT OR IGNORE INTO items (collection_id, collection_uuid, "
                  "path, name, last_modified) VALUES ";
    QStringList valueSets;
    valueSets.reserve(batchCount);
    for (int i = 0; i < batchCount; ++i) {
      valueSets.append("(?, ?, ?, ?, ?)");
    }
    sql += valueSets.join(", ");
    
    QSqlQuery ins(m_db);
    ins.prepare(sql);
    
    for (int i = batchStart; i < batchEnd; ++i) {
      const QString &filePath = filePaths[i];
      ins.addBindValue(legacyId);
      ins.addBindValue(uuid);
      ins.addBindValue(filePath);
      ins.addBindValue(QFileInfo(filePath).completeBaseName());
      ins.addBindValue(timestamps.value(filePath).toString(Qt::ISODate));
    }
    
    if (!ins.exec()) {
      auto err = ErrorContext::warning(
          ErrorCode::DatabaseQueryFailed,
          QString("Failed to insert batch at %1").arg(batchStart),
          "QueryManager::saveItemsToDatabase")
          .withDetails(ins.lastError().text());
      ErrorUtils::logError(err);
    }
    
    itemsInserted = batchEnd;
    ++batchesSinceCommit;
    
    // Commit periodically to save incremental progress
    if (batchesSinceCommit >= COMMIT_INTERVAL_BATCHES) {
      m_db.commit();
      inTransaction = false;
      
      // Report progress
      emit scanItemsProgress(itemsInserted, totalItems);
    }
    
    // Report progress at intervals even within transaction
    if (itemsInserted % PROGRESS_REPORT_INTERVAL == 0) {
      emit scanItemsProgress(itemsInserted, totalItems);
    }
  }
  
  // Final commit for any remaining items
  if (inTransaction) {
    m_db.commit();
  }
  
  // Final progress report
  if (!isScanCancelled()) {
    emit scanItemsProgress(totalItems, totalItems);
  }
}

QStringList QueryManager::loadItemsFromDatabaseByUuid(const QString &collectionUuid) {
  QStringList filePaths;

  if (!m_db.isOpen()) {
    auto err = ErrorContext::error(
        ErrorCode::DatabaseNotOpen,
        "Database is not open, cannot load items",
        "QueryManager::loadItemsFromDatabaseByUuid");
    ErrorUtils::logError(err);
    return filePaths;
  }

  // Use cached prepared statement for loading items
  QSqlQuery &query = getPreparedStatement(QuerySQL::LOAD_ITEMS_BY_UUID);
  query.addBindValue(collectionUuid);

  if (!query.exec()) {
    auto err = ErrorContext::error(
        ErrorCode::DatabaseQueryFailed,
        "Failed to load items from database",
        "QueryManager::loadItemsFromDatabaseByUuid")
        .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
    return filePaths;
  }

  while (query.next()) {
    filePaths.append(query.value(0).toString());
  }

  return filePaths;
}

void QueryManager::invalidateCollectionCache(const QString &collectionUuid) {
  // Cancel any ongoing scan before clearing cache to prevent lock conflicts
  requestCancelScan();
  
  // Small delay to allow in-progress transaction to complete
  QThread::msleep(50);
  
  clearCollectionFromDatabaseByUuid(collectionUuid);
  emit cacheInvalidated(collectionUuid);
}

void QueryManager::clearCollectionFromDatabaseByUuid(const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return;
  }

  // Retry logic for database lock scenarios
  constexpr int MAX_RETRIES = 5;
  constexpr int BASE_DELAY_MS = 100;
  
  for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
    if (attempt > 0) {
      // Exponential backoff: 100ms, 200ms, 400ms, 800ms, 1600ms
      QThread::msleep(BASE_DELAY_MS * (1 << (attempt - 1)));
    }
    
    if (!m_db.transaction()) {
      continue;  // Retry if can't start transaction
    }

    try {
      // Use cached prepared statement for deleting items
      QSqlQuery &query = getPreparedStatement(QuerySQL::DELETE_ITEMS_BY_UUID);
      query.addBindValue(collectionUuid);
      if (!query.exec()) {
        throw std::runtime_error(query.lastError().text().toStdString());
      }

      // Use cached prepared statement for deleting collection
      QSqlQuery &delc = getPreparedStatement(QuerySQL::DELETE_COLLECTION_BY_UUID);
      delc.addBindValue(collectionUuid);
      delc.exec();

      m_db.commit();
      return;  // Success - exit retry loop
    } catch (const std::exception &e) {
      m_db.rollback();
      
      QString errorText = QString::fromStdString(e.what());
      bool isLockError = errorText.contains("locked", Qt::CaseInsensitive);
      
      if (!isLockError || attempt == MAX_RETRIES - 1) {
        // Non-lock error or final attempt - log and give up
        auto err = ErrorContext::critical(
            ErrorCode::DatabaseTransactionFailed,
            "Failed to clear collection from database",
            "QueryManager::clearCollectionFromDatabaseByUuid")
            .withDetails(errorText);
        ErrorUtils::logError(err);
        return;
      }
      // Lock error - will retry
    }
  }
}

// Static helpers
static auto canonicalKeyPath(const QString &absPath, bool dedup,
                             QHash<QString, QString> *canonicalPathCache) -> QString {
  if (!dedup) {
    return absPath;
  }

  if (canonicalPathCache) {
    auto it = canonicalPathCache->constFind(absPath);
    if (it != canonicalPathCache->constEnd()) {
      return it.value();
    }
  }

  QString canon = QFileInfo(absPath).canonicalFilePath();
  if (canon.isEmpty()) {
    canon = QDir::cleanPath(absPath);
  }

  if (canonicalPathCache) {
    canonicalPathCache->insert(absPath, canon);
  }
  return canon;
}

static auto displayNameForBase(const QString &baseName) -> QString {
  return QString(baseName)
      .replace(QLatin1Char('_'), QLatin1Char(' '))
      .simplified();
}

template <typename Map, typename Key, typename Value>
inline void insertIfAbsent(Map &map, const Key &key, const Value &value) {
  if (!map.contains(key)) {
    map.insert(key, value);
  }
}

void QueryManager::appendFileMapsAndListCanonical(
    int collectionIndex, const CollectionConfig &expandedCollection,
    const QString &mappingArtworkDir, const QStringList &filePaths,
    QStringList &allFilePaths, QHash<QString, QString> &allFileNames,
    QHash<QString, QString> &fileToArtworkDir,
    QHash<QString, QString> &fileToMediaDir,
    QHash<QString, int> &fileToCollectionIndex, bool dedup,
    QSet<QString> *seenCanonicalPaths,
    QHash<QString, QString> *canonicalPathCache) {
  const QString mediaDir = expandedCollection.mediaDirectory;
  QDir mediaQDir(mediaDir);

  for (const QString &file : filePaths) {
    const QString absPath = mediaQDir.absoluteFilePath(file);
    const QString keyPath = canonicalKeyPath(absPath, dedup, canonicalPathCache);

    if (dedup) {
      if (seenCanonicalPaths) {
        if (!seenCanonicalPaths->contains(keyPath)) {
          seenCanonicalPaths->insert(keyPath);
          allFilePaths.append(keyPath);
        }
      } else {
        if (!allFilePaths.contains(keyPath)) {
          allFilePaths.append(keyPath);
        }
      }
    } else {
      allFilePaths.append(keyPath);
    }

    const QFileInfo info(file);
    const QString fileName = info.fileName();
    const QString baseName = info.completeBaseName();
    const QString displayName = displayNameForBase(baseName);

    allFileNames[keyPath] = displayName;

    insertIfAbsent(fileToArtworkDir, keyPath, mappingArtworkDir);
    insertIfAbsent(fileToArtworkDir, file, mappingArtworkDir);
    insertIfAbsent(fileToArtworkDir, fileName, mappingArtworkDir);
    insertIfAbsent(fileToArtworkDir, baseName, mappingArtworkDir);

    insertIfAbsent(fileToMediaDir, keyPath, mediaDir);
    insertIfAbsent(fileToMediaDir, file, mediaDir);
    insertIfAbsent(fileToMediaDir, fileName, mediaDir);
    insertIfAbsent(fileToMediaDir, baseName, mediaDir);

    insertIfAbsent(fileToCollectionIndex, keyPath, collectionIndex);
    insertIfAbsent(fileToCollectionIndex, file, collectionIndex);
    insertIfAbsent(fileToCollectionIndex, fileName, collectionIndex);
    insertIfAbsent(fileToCollectionIndex, baseName, collectionIndex);
  }
}

void QueryManager::sortFiles(QStringList &allFilePaths, SortMode mode) {
  if (mode == SortMode::Random) {
    // Fisher-Yates shuffle
    auto seed = static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch());
    std::mt19937 rng(seed);
    for (int i = allFilePaths.size() - 1; i > 0; --i) {
      std::uniform_int_distribution<int> dist(0, i);
      int j = dist(rng);
      allFilePaths.swapItemsAt(i, j);
    }
    return;
  }

  bool descending = (mode == SortMode::NameDescending);
  
  std::ranges::sort(allFilePaths, [&](const QString &lhs, const QString &rhs) {
    QString nameA = QFileInfo(lhs).completeBaseName();
    QString nameB = QFileInfo(rhs).completeBaseName();
    QString sortKeyA = PathUtils::normalizeDisplayName(nameA);
    QString sortKeyB = PathUtils::normalizeDisplayName(nameB);

    if (nameA.startsWith('\'') && nameA.length() > 1 &&
        (nameA[1].isDigit() || nameA[1].isLetter())) {
      sortKeyA = PathUtils::normalizeDisplayName(nameA.mid(1));
    }
    if (nameB.startsWith('\'') && nameB.length() > 1 &&
        (nameB[1].isDigit() || nameB[1].isLetter())) {
      sortKeyB = PathUtils::normalizeDisplayName(nameB.mid(1));
    }

    int priorityA = getCharacterSortPriority(sortKeyA);
    int priorityB = getCharacterSortPriority(sortKeyB);
    if (priorityA != priorityB) {
      return descending ? priorityA > priorityB : priorityA < priorityB;
    }
    int cmp = sortKeyA.compare(sortKeyB, Qt::CaseInsensitive);
    return descending ? cmp > 0 : cmp < 0;
  });
}

int QueryManager::getCharacterSortPriority(const QString &text) {
  if (text.isEmpty()) {
    return 3;
  }

  QChar firstChar = text[0];
  if (firstChar == '[' || firstChar == '(') {
    return 0;
  }
  if (firstChar == '\'' && text.length() > 1 &&
      (text[1].isDigit() || text[1].isLetter())) {
    return text[1].isDigit() ? 2 : 3;
  }
  if (firstChar.isDigit()) {
    return 2;
  }
  if (firstChar.isLetter()) {
    return 3;
  }
  return 1;
}
