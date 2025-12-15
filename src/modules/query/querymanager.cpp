// Executes SQLite queries on worker thread for paginated item loading and filtering.
#include "querymanager.h"
#include "collectionutils.h"
#include "dbmigrations.h"
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
#include <atomic>
#include <QRunnable>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcQueryManager, "kartend.querymanager")
#define debugLog(msg) qCDebug(lcQueryManager) << msg

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

// Forward declarations of static helpers
static auto canonicalKeyPath(const QString &absPath, bool dedup,
                             QHash<QString, QString> *canonicalPathCache) -> QString;
static auto displayNameForBase(const QString &baseName) -> QString;

namespace {
// Result struct for parallel directory scanning
// Holds files found in a single directory plus their timestamps
struct DirectoryScanResult {
  QStringList relativePaths;
  QHash<QString, QDateTime> timestamps;
};

struct DirSignatureSample {
  QString relPath;   // Relative to collection root. Empty means root.
  qint64 mtimeSec = 0;
};

static auto addDirSignatureSample(QVector<DirSignatureSample> &samples,
                                 const DirSignatureSample &candidate,
                                 int maxSamples) -> void {
  if (maxSamples <= 0) {
    return;
  }
  for (const auto &s : samples) {
    if (s.relPath == candidate.relPath) {
      return;
    }
  }

  if (samples.size() < maxSamples) {
    samples.append(candidate);
  } else {
    int minIndex = 0;
    for (int i = 1; i < samples.size(); ++i) {
      if (samples[i].mtimeSec < samples[minIndex].mtimeSec) {
        minIndex = i;
      }
    }
    if (candidate.mtimeSec <= samples[minIndex].mtimeSec) {
      return;
    }
    samples[minIndex] = candidate;
  }
}

static auto buildFtsPrefixQuery(const QString &raw) -> QString {
  QString trimmed = raw.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  // Sanitize into simple terms to avoid FTS query parser edge-cases.
  // Keep letters/numbers/underscore; replace everything else with spaces.
  QString cleaned = trimmed;
  cleaned.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")),
                  QStringLiteral(" "));
  const QStringList terms = cleaned.split(' ', Qt::SkipEmptyParts);
  if (terms.isEmpty()) {
    return {};
  }

  QStringList tokens;
  tokens.reserve(terms.size());
  for (const QString &t : terms) {
    if (t.isEmpty()) {
      continue;
    }
    tokens.append(t + "*");
  }
  if (tokens.isEmpty()) {
    return {};
  }
  return tokens.join(QStringLiteral(" AND "));
}

static auto buildDirSignatureJson(bool includeSubfolders,
                                 const QVector<DirSignatureSample> &samples) -> QString {
  QJsonObject root;
  root.insert(QStringLiteral("v"), 1);
  root.insert(QStringLiteral("sub"), includeSubfolders);

  QJsonArray arr;
  for (const auto &s : samples) {
    QJsonObject o;
    o.insert(QStringLiteral("p"), s.relPath);
    o.insert(QStringLiteral("t"), static_cast<double>(s.mtimeSec));
    arr.append(o);
  }
  root.insert(QStringLiteral("s"), arr);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

static auto parseDirSignatureJson(const QString &json,
                                 bool &includeSubfoldersOut,
                                 QVector<DirSignatureSample> &samplesOut) -> bool {
  includeSubfoldersOut = false;
  samplesOut.clear();

  if (json.trimmed().isEmpty()) {
    return false;
  }

  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return false;
  }

  const QJsonObject obj = doc.object();
  if (obj.value(QStringLiteral("v")).toInt() != 1) {
    return false;
  }

  includeSubfoldersOut = obj.value(QStringLiteral("sub")).toBool(false);

  const QJsonValue sVal = obj.value(QStringLiteral("s"));
  if (!sVal.isArray()) {
    return false;
  }
  const QJsonArray arr = sVal.toArray();
  samplesOut.reserve(arr.size());
  for (const auto &v : arr) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject o = v.toObject();
    DirSignatureSample s;
    s.relPath = o.value(QStringLiteral("p")).toString();
    s.mtimeSec = static_cast<qint64>(o.value(QStringLiteral("t")).toDouble(0.0));
    samplesOut.append(std::move(s));
  }
  return !samplesOut.isEmpty();
}

static auto seedDirSignatureFromFilesystem(const QString &rootPath,
                                          bool includeSubfolders) -> QString {
  QVector<DirSignatureSample> samples;
  samples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);

  QFileInfo rootInfo(rootPath);
  if (!rootInfo.exists()) {
    return QString();
  }
  addDirSignatureSample(samples,
                        DirSignatureSample{QString(), rootInfo.lastModified().toSecsSinceEpoch()},
                        UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);

  if (includeSubfolders) {
    QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    int inspected = 0;
    while (it.hasNext() && inspected < UIConstants::Database::DIR_SIGNATURE_SEED_MAX_DIRS) {
      it.next();
      const QString absPath = it.filePath();
      const QString relPath = QDir(rootPath).relativeFilePath(absPath);
      const qint64 mtimeSec = QFileInfo(absPath).lastModified().toSecsSinceEpoch();
      addDirSignatureSample(samples, DirSignatureSample{relPath, mtimeSec},
                            UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
      ++inspected;
    }
  }

  return buildDirSignatureJson(includeSubfolders, samples);
}

static auto dirSignatureStillValid(const QString &rootPath,
                                  bool includeSubfolders,
                                  const QString &storedSignature) -> bool {
  bool storedSub = false;
  QVector<DirSignatureSample> samples;
  if (!parseDirSignatureJson(storedSignature, storedSub, samples)) {
    return false;
  }
  if (storedSub != includeSubfolders) {
    return false;
  }
  if (samples.isEmpty()) {
    return false;
  }

  QDir root(rootPath);
  for (const auto &s : samples) {
    const QString absPath = s.relPath.isEmpty() ? rootPath : root.absoluteFilePath(s.relPath);
    QFileInfo info(absPath);
    if (!info.exists()) {
      return false;
    }
    const qint64 currentSec = info.lastModified().toSecsSinceEpoch();
    if (currentSec != s.mtimeSec) {
      return false;
    }
  }

  return true;
}

// Forward declaration used by DirectoryScanTask.
static DirectoryScanResult scanSingleDirectory(
    const QString &dirPath,
    const QString &rootPath,
    const QStringList &nameFilters,
    const std::atomic<bool> &cancelled);

struct ScanCompletionQueue {
  QMutex mutex;
  QWaitCondition hasResult;
  QVector<DirectoryScanResult> ready;
  int inFlight = 0;
};

class DirectoryScanTask final : public QRunnable {
public:
  DirectoryScanTask(QString dirPath, QString rootPath, QStringList nameFilters,
                    std::shared_ptr<std::atomic_bool> cancelToken,
                    ScanCompletionQueue *queue)
      : m_dirPath(std::move(dirPath)),
        m_rootPath(std::move(rootPath)),
        m_nameFilters(std::move(nameFilters)),
        m_cancelToken(std::move(cancelToken)),
        m_queue(queue) {
    setAutoDelete(true);
  }

  void run() override {
    if (!m_queue || !m_cancelToken) {
      return;
    }

    DirectoryScanResult result =
        scanSingleDirectory(m_dirPath, m_rootPath, m_nameFilters, *m_cancelToken);

    QMutexLocker locker(&m_queue->mutex);
    m_queue->ready.append(std::move(result));
    --m_queue->inFlight;
    m_queue->hasResult.wakeOne();
  }

private:
  QString m_dirPath;
  QString m_rootPath;
  QStringList m_nameFilters;
  std::shared_ptr<std::atomic_bool> m_cancelToken;
  ScanCompletionQueue *m_queue = nullptr;
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
    const QString filePath = iterator.filePath();
    const QString relativePath = rootDir.relativeFilePath(filePath);
    const QFileInfo info = iterator.fileInfo();
    result.relativePaths.append(relativePath);
    result.timestamps.insert(relativePath, info.lastModified());
  }

  return result;
}

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

QueryManager::QueryManager(SessionManager *sessionManager,
                           const QString &connectionName,
                           QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager) {
  m_connectionName = connectionName;
  m_scanCancellationToken = std::make_shared<std::atomic_bool>(false);

  // Pointer-based cache with automatic LRU eviction.
  m_statementCache.setMaxCost(MAX_STATEMENT_CACHE_SIZE);

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
  if (m_scanCancellationToken) {
    m_scanCancellationToken->store(true, std::memory_order_release);
  }
  // Drop any queued scan work that hasn't started yet.
  m_scanThreadPool.clear();
}

bool QueryManager::isScanCancelled() const {
  return m_scanCancellationToken &&
         m_scanCancellationToken->load(std::memory_order_acquire);
}

void QueryManager::resetScanCancellation() {
  m_scanCancellationToken = std::make_shared<std::atomic_bool>(false);
}

// Gets or creates a prepared statement for the given SQL
// Caches compiled statements to avoid repeated prepare() overhead
// Uses LRU eviction when cache exceeds MAX_STATEMENT_CACHE_SIZE
auto QueryManager::getPreparedStatement(const QString &sql) -> QSqlQuery & {
  if (QSqlQuery *cached = m_statementCache.object(sql)) {
    // Close previous result set before reuse.
    cached->finish();
    return *cached;
  }

  // Create new prepared statement and cache it (QCache takes ownership).
  auto *query = new QSqlQuery(m_db);
  query->prepare(sql);
  m_statementCache.insert(sql, query, 1);
  return *query;
}

// Clears statement cache - call when database connection changes
void QueryManager::clearStatementCache() {
  m_statementCache.clear();
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
  if (!QDir().mkpath(dbPath)) {
    auto err = ErrorContext::critical(
        ErrorCode::DatabaseConnectionFailed,
        "Failed to create database directory",
        "QueryManager::initDatabase")
        .withDetails(QString("Path: %1").arg(dbPath));
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }
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

  // Ensure tables exist when worker opens DB in isolation.
  query.exec("CREATE TABLE IF NOT EXISTS collections ("
             "id INTEGER PRIMARY KEY, "
             "name TEXT NOT NULL, "
             "last_scanned TEXT NOT NULL, "
             "ext_signature TEXT DEFAULT '', "
             "uuid TEXT DEFAULT ''"
             ")");

  query.exec(
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
      ")");

  // Keep core indexes available even in worker-only initialization.
  query.exec("CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)");
  query.exec("CREATE INDEX IF NOT EXISTS idx_items_collection_uuid ON items(collection_uuid)");

  DbMigrations::applySchemaMigrations(m_db, QStringLiteral("QueryManager::initDatabase"));

  refreshSearchCapabilities();
}

void QueryManager::refreshSearchCapabilities() {
  m_itemsFtsAvailable = false;
  m_itemsFtsReady = true;
  if (!m_db.isOpen()) {
    return;
  }

  QSqlQuery q(m_db);
  q.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
  q.addBindValue(QStringLiteral("items_fts"));
  if (q.exec() && q.next()) {
    m_itemsFtsAvailable = true;
  }

  if (m_itemsFtsAvailable) {
    m_itemsFtsReady = isItemsFtsReadyFromDb();
  }
}

namespace {
static auto ensureMetaTable(QSqlDatabase &db) -> void {
  if (!db.isOpen()) {
    return;
  }
  QSqlQuery q(db);
  q.exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
}

static auto tryReadMetaValue(QSqlDatabase &db, const QString &key,
                             QString &valueOut) -> bool {
  QSqlQuery q(db);
  q.prepare("SELECT value FROM meta WHERE key = ?");
  q.addBindValue(key);
  if (!q.exec() || !q.next()) {
    return false;
  }
  valueOut = q.value(0).toString();
  return true;
}

static auto writeMetaValue(QSqlDatabase &db, const QString &key,
                           const QString &value) -> void {
  QSqlQuery q(db);
  q.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)");
  q.addBindValue(key);
  q.addBindValue(value);
  q.exec();
}

static auto tryReadMetaInt(QSqlDatabase &db, const QString &key,
                           qint64 &valueOut) -> bool {
  QString value;
  if (!tryReadMetaValue(db, key, value)) {
    return false;
  }
  bool ok = false;
  const qint64 parsed = value.toLongLong(&ok);
  if (!ok) {
    return false;
  }
  valueOut = parsed;
  return true;
}
} // namespace

bool QueryManager::isItemsFtsReadyFromDb() {
  if (!m_db.isOpen()) {
    return false;
  }

  // Meta table and keys may be missing for databases created by older builds.
  // In that case, prefer treating FTS as ready (those builds performed a full
  // rebuild during migration) and persist the marker to avoid repeated checks.
  ensureMetaTable(m_db);

  QString ready;
  if (!tryReadMetaValue(m_db, QStringLiteral("items_fts_ready"), ready)) {
    writeMetaValue(m_db, QStringLiteral("items_fts_ready"), QStringLiteral("1"));
    return true;
  }
  return ready.trimmed() == QLatin1String("1");
}

void QueryManager::ensureItemsFtsReady() {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      return;
    }
  }

  // Refresh availability and current readiness state.
  refreshSearchCapabilities();
  if (!m_itemsFtsAvailable) {
    return;
  }

  // If already ready, nothing to do.
  if (m_itemsFtsReady) {
    return;
  }

  ensureMetaTable(m_db);

  QElapsedTimer slice;
  slice.start();

  qint64 indexedUpToId = 0;
  (void)tryReadMetaInt(m_db, QStringLiteral("items_fts_indexed_up_to_id"), indexedUpToId);

  while (slice.elapsed() < UIConstants::Database::FTS_BACKFILL_TIME_BUDGET_MS) {
    // Determine the current max id in the content table.
    qint64 maxId = 0;
    {
      QSqlQuery maxQ(m_db);
      if (maxQ.exec("SELECT COALESCE(MAX(id), 0) FROM items") && maxQ.next()) {
        maxId = maxQ.value(0).toLongLong();
      }
    }

    if (maxId <= indexedUpToId) {
      writeMetaValue(m_db, QStringLiteral("items_fts_ready"), QStringLiteral("1"));
      m_itemsFtsReady = true;
      return;
    }

    if (!m_db.transaction()) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseTransactionFailed,
                                       "Failed to start transaction for FTS backfill",
                                       "QueryManager::ensureItemsFtsReady")
                     .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return;
    }

    QSqlQuery insertQ(m_db);
    insertQ.prepare(
        "INSERT INTO items_fts(rowid, name, path, collection_uuid) "
        "SELECT id, name, path, collection_uuid "
        "FROM items "
        "WHERE id > ? "
        "ORDER BY id "
        "LIMIT ?");
    insertQ.addBindValue(indexedUpToId);
    insertQ.addBindValue(UIConstants::Database::FTS_BACKFILL_BATCH_SIZE);

    if (!insertQ.exec()) {
      m_db.rollback();
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "FTS backfill insert failed (search will use LIKE)",
                                       "QueryManager::ensureItemsFtsReady")
                     .withDetails(insertQ.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return;
    }

    qint64 newIndexedUpToId = indexedUpToId;
    {
      // Compute the last id included in this batch.
      // Do NOT use MAX(rowid) from items_fts because triggers may insert higher
      // ids concurrently (e.g., during scans), which would incorrectly skip
      // unindexed rows.
      QSqlQuery lastIdQ(m_db);
      lastIdQ.prepare(
          "SELECT COALESCE(MAX(id), ?) FROM ("
          "  SELECT id FROM items WHERE id > ? ORDER BY id LIMIT ?"
          ")");
      lastIdQ.addBindValue(indexedUpToId);
      lastIdQ.addBindValue(indexedUpToId);
      lastIdQ.addBindValue(UIConstants::Database::FTS_BACKFILL_BATCH_SIZE);
      if (lastIdQ.exec() && lastIdQ.next()) {
        newIndexedUpToId = lastIdQ.value(0).toLongLong();
      }
    }

    (void)m_db.commit();

    if (newIndexedUpToId <= indexedUpToId) {
      // No progress; avoid tight looping.
      break;
    }

    indexedUpToId = newIndexedUpToId;
    writeMetaValue(m_db, QStringLiteral("items_fts_indexed_up_to_id"),
                   QString::number(indexedUpToId));
  }

  // Continue in another slice.
  // Defer to keep the scan worker event loop responsive.
  QTimer::singleShot(0, this, &QueryManager::ensureItemsFtsReady);
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

  if (ctx.queryIncludeAllCollections) {
    QSet<QString> seen;
    uuids.reserve(allCollections.size());
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig c = allCollections[i];
      c.mediaDirectory = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      if (c.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      const QString uuid = CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
      if (!uuid.isEmpty() && !seen.contains(uuid)) {
        seen.insert(uuid);
        uuids.append(uuid);
      }
    }
    return uuids;
  }

  uuids << CollectionUtils::computeCollectionUuid(ctx.config.name, ctx.config.mediaDirectory);

  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
    QList<int> rawDescendants =
        CollectionUtils::collectDescendantIndices(ctx.currentIndex, allCollections);
    uuids.reserve(uuids.size() + rawDescendants.size());
    for (int descendantIndex : rawDescendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }
      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory =
          PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
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
    const QString uuid = CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
    if (uuid.isEmpty()) {
      return;
    }
    if (!maps.uuidToMediaDir.contains(uuid)) {
      maps.uuidToMediaDir[uuid] = c.mediaDirectory;
    }
    if (!maps.uuidToArtworkDir.contains(uuid)) {
      maps.uuidToArtworkDir[uuid] = c.artworkDirectory;
    }
  };

  if (ctx.queryIncludeAllCollections) {
    maps.uuidToMediaDir.reserve(allCollections.size());
    maps.uuidToArtworkDir.reserve(allCollections.size());
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig c = allCollections[i];
      c.mediaDirectory = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      c.artworkDirectory = PathUtils::validateAndExpandPath(c.artworkDirectory, c.name);
      if (c.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      addMapping(c);
    }
    return maps;
  }

  QList<int> rawDescendants;
  int expectedMappings = 1;
  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
    rawDescendants =
        CollectionUtils::collectDescendantIndices(ctx.currentIndex, allCollections);
    expectedMappings += rawDescendants.size();
  }

  maps.uuidToMediaDir.reserve(expectedMappings);
  maps.uuidToArtworkDir.reserve(expectedMappings);

  addMapping(ctx.config);

  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
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
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
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

    // Stream scan results directly into DB inserts to reduce peak memory.
    // (In this call path, we don't need a full in-memory file list.)
    scanAndSaveItemsToDatabase(collectionIndex, collection);
  }
}

void QueryManager::insertItemsBatch(int legacyId, const QString &uuid,
                                   const QStringList &paths,
                                   const QHash<QString, QDateTime> &timestamps) {
  if (paths.isEmpty()) {
    return;
  }

  // Batch upsert for performance.
  // IMPORTANT: preserve user state fields (play_count/last_played/rating/artwork_path).
  // We only update name + last_modified (and collection_id) when a row already exists.
  QString sql = "INSERT INTO items (collection_id, collection_uuid, "
                "path, name, last_modified) VALUES ";
  QStringList valueSets;
  valueSets.reserve(paths.size());
  for (int i = 0; i < paths.size(); ++i) {
    valueSets.append("(?, ?, ?, ?, ?)");
  }
  sql += valueSets.join(", ");
  sql += " ON CONFLICT(collection_uuid, path) DO UPDATE SET "
         "collection_id=excluded.collection_id, "
         "name=excluded.name, "
         "last_modified=excluded.last_modified";

  QSqlQuery ins(m_db);
  ins.prepare(sql);
  for (const QString &filePath : paths) {
    ins.addBindValue(legacyId);
    ins.addBindValue(uuid);
    ins.addBindValue(filePath);
    ins.addBindValue(QFileInfo(filePath).completeBaseName());
    ins.addBindValue(timestamps.value(filePath).toString(Qt::ISODate));
  }

  if (!ins.exec()) {
    auto err = ErrorContext::warning(
        ErrorCode::DatabaseQueryFailed,
        "Failed to insert items batch",
        "QueryManager::insertItemsBatch")
        .withDetails(ins.lastError().text());
    ErrorUtils::logError(err);
  }
}

bool QueryManager::ensureScannedItemsTempTable() {
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  if (!q.exec(
          "CREATE TEMP TABLE IF NOT EXISTS scanned_items ("
          "path TEXT PRIMARY KEY, "
          "name TEXT, "
          "last_modified TEXT"
          ")")) {
    ErrorUtils::logError(
        ErrorContext::warning(
            ErrorCode::DatabaseQueryFailed,
            "Failed to create scanned_items temp table",
            "QueryManager::ensureScannedItemsTempTable")
            .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

void QueryManager::clearScannedItemsTempTable() {
  if (!m_db.isOpen()) {
    return;
  }
  QSqlQuery q(m_db);
  q.exec("DELETE FROM scanned_items");
}

void QueryManager::insertScannedItemsBatch(
    const QStringList &paths, const QHash<QString, QDateTime> &timestamps) {
  if (!m_db.isOpen() || paths.isEmpty()) {
    return;
  }

  // 3 columns per row -> keep under SQLite 999 variable limit.
  QString sql = "INSERT OR REPLACE INTO scanned_items (path, name, last_modified) VALUES ";
  QStringList valueSets;
  valueSets.reserve(paths.size());
  for (int i = 0; i < paths.size(); ++i) {
    valueSets.append("(?, ?, ?)");
  }
  sql += valueSets.join(", ");

  QSqlQuery ins(m_db);
  ins.prepare(sql);
  for (const QString &p : paths) {
    ins.addBindValue(p);
    ins.addBindValue(QFileInfo(p).completeBaseName());
    ins.addBindValue(timestamps.value(p).toString(Qt::ISODate));
  }
  if (!ins.exec()) {
    ErrorUtils::logError(
        ErrorContext::warning(
            ErrorCode::DatabaseQueryFailed,
            "Failed to insert scanned_items batch",
            "QueryManager::insertScannedItemsBatch")
            .withDetails(ins.lastError().text()));
  }
}

bool QueryManager::applyScannedItemsToDatabase(int legacyId,
                                               const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return false;
  }

  QSqlQuery upsert(m_db);
  upsert.prepare(
      "INSERT INTO items (collection_id, collection_uuid, path, name, last_modified) "
      "SELECT ?, ?, path, name, last_modified FROM scanned_items "
      "ON CONFLICT(collection_uuid, path) DO UPDATE SET "
      "collection_id=excluded.collection_id, "
      "name=excluded.name, "
      "last_modified=excluded.last_modified");
  upsert.addBindValue(legacyId);
  upsert.addBindValue(collectionUuid);
  if (!upsert.exec()) {
    ErrorUtils::logError(
        ErrorContext::warning(
            ErrorCode::DatabaseQueryFailed,
            "Failed to apply scanned_items upsert",
            "QueryManager::applyScannedItemsToDatabase")
            .withDetails(upsert.lastError().text()));
    return false;
  }
  return true;
}

bool QueryManager::deleteMissingItemsByUuidUsingScannedItems(
    const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  q.prepare(
      "DELETE FROM items WHERE collection_uuid = ? "
      "AND NOT EXISTS (SELECT 1 FROM scanned_items si WHERE si.path = items.path)");
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    ErrorUtils::logError(
        ErrorContext::warning(
            ErrorCode::DatabaseQueryFailed,
            "Failed to delete missing items using scanned_items",
            "QueryManager::deleteMissingItemsByUuidUsingScannedItems")
            .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

auto QueryManager::prepareCollectionForItemsInsert(const CollectionConfig &collection,
                                                   const QString &uuid,
                                                   const QString &extSignature,
                                                   int &legacyIdOut) -> bool {
  legacyIdOut = -1;

  // Retry constants for lock handling
  constexpr int MAX_RETRIES = 5;
  constexpr int BASE_DELAY_MS = 100;

  bool prepareSuccess = false;
  for (int attempt = 0; attempt < MAX_RETRIES && !prepareSuccess; ++attempt) {
    if (attempt > 0) {
      // Exponential backoff: 100ms, 200ms, 400ms, 800ms, 1600ms
      QThread::msleep(BASE_DELAY_MS * (1 << (attempt - 1)));
    }

    if (!m_db.transaction()) {
      continue;
    }

    try {
      QSqlQuery update(m_db);
      update.prepare("UPDATE collections SET name=?, ext_signature=? WHERE uuid=?");
      update.addBindValue(collection.name);
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
        const QString initialLastScanned =
            QDateTime::fromSecsSinceEpoch(0).toString(Qt::ISODate);
        insert.addBindValue(collection.name);
        insert.addBindValue(initialLastScanned);
        insert.addBindValue(extSignature);
        insert.addBindValue(uuid);
        if (!insert.exec()) {
          throw std::runtime_error(insert.lastError().text().toStdString());
        }
      }

      {
        QSqlQuery idq(m_db);
        idq.prepare("SELECT id FROM collections WHERE uuid = ?");
        idq.addBindValue(uuid);
        if (idq.exec() && idq.next()) {
          legacyIdOut = idq.value(0).toInt();
        }
      }

      if (!m_db.commit()) {
        throw std::runtime_error(m_db.lastError().text().toStdString());
      }
      prepareSuccess = true;
    } catch (const std::exception &e) {
      m_db.rollback();

      QString errorText = QString::fromStdString(e.what());
      bool isLockError = errorText.contains("locked", Qt::CaseInsensitive);

      if (!isLockError || attempt == MAX_RETRIES - 1) {
        auto err = ErrorContext::critical(
            ErrorCode::DatabaseTransactionFailed,
            "Failed to prepare collection for items",
            "QueryManager::prepareCollectionForItemsInsert")
            .withDetails(errorText);
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        return false;
      }
    }
  }

  return prepareSuccess;
}

void QueryManager::scanAndSaveItemsToDatabase(int collectionIndex,
                                              const CollectionConfig &collection) {
  Q_UNUSED(collectionIndex)

  if (!m_db.isOpen()) {
    auto err = ErrorContext::error(
        ErrorCode::DatabaseNotOpen,
        "Database is not open",
        "QueryManager::scanAndSaveItemsToDatabase");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  QDir dir(collection.mediaDirectory);
  if (!dir.exists()) {
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
  if (!pragmaOff.exec("PRAGMA synchronous = OFF")) {
    ErrorUtils::logError(
        ErrorContext::warning(
            ErrorCode::DatabaseQueryFailed,
            "Failed to set synchronous=OFF for bulk insert",
            "QueryManager::scanAndSaveItemsToDatabase")
            .withDetails(pragmaOff.lastError().text()));
  }
  const SynchronousPragmaGuard restoreSynchronous(m_db);

  // Cancellation-safe scans: stage into a TEMP table and only apply to the
  // persistent DB when the scan completes.
  if (!ensureScannedItemsTempTable()) {
    return;
  }

  clearScannedItemsTempTable();

  struct ScannedItemsTempTableCleanup {
    QueryManager *self = nullptr;
    bool enabled = false;
    ~ScannedItemsTempTableCleanup() {
      if (enabled && self) {
        self->clearScannedItemsTempTable();
      }
    }
  } scannedItemsCleanup{this, true};

  QStringList nameFilters;
  if (!collection.extensions.isEmpty()) {
    for (const QString &ext : collection.extensions) {
      nameFilters << "*." + ext;
    }
  }

  constexpr int BATCH_SIZE = 199;
  constexpr int COMMIT_INTERVAL_BATCHES = 500;
  constexpr int PROGRESS_REPORT_INTERVAL = 50000;

  // Throttle scan progress emissions to avoid spamming the UI event loop.
  QElapsedTimer progressTimer;
  progressTimer.start();
  qint64 lastProgressEmitMs = -UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS;

  auto maybeEmitScanProgress = [&](int processed, int total, bool force = false) {
    if (force) {
      emit scanItemsProgress(processed, total);
      lastProgressEmitMs = progressTimer.elapsed();
      return;
    }
    const qint64 nowMs = progressTimer.elapsed();
    if (nowMs - lastProgressEmitMs < UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS) {
      return;
    }
    emit scanItemsProgress(processed, total);
    lastProgressEmitMs = nowMs;
  };

  bool inTransaction = false;
  int batchesSinceCommit = 0;
  int itemsInserted = 0;

  QString dirSignature;

  QStringList batchPaths;
  batchPaths.reserve(BATCH_SIZE);
  QHash<QString, QDateTime> batchTimestamps;
  batchTimestamps.reserve(BATCH_SIZE * 2);

  auto flushBatch = [&]() -> bool {
    if (batchPaths.isEmpty()) {
      return true;
    }
    if (isScanCancelled()) {
      if (inTransaction) {
        m_db.rollback();
        inTransaction = false;
      }
      batchPaths.clear();
      batchTimestamps.clear();
      return false;
    }

    if (!inTransaction) {
      if (!m_db.transaction()) {
        auto err = ErrorContext::critical(
            ErrorCode::DatabaseTransactionFailed,
            "Failed to start transaction for streaming insert",
            "QueryManager::scanAndSaveItemsToDatabase")
            .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        return false;
      }
      inTransaction = true;
      batchesSinceCommit = 0;
    }

    insertScannedItemsBatch(batchPaths, batchTimestamps);
    itemsInserted += batchPaths.size();
    ++batchesSinceCommit;

    batchPaths.clear();
    batchTimestamps.clear();

    if (batchesSinceCommit >= COMMIT_INTERVAL_BATCHES) {
      if (!m_db.commit()) {
        auto err = ErrorContext::critical(
            ErrorCode::DatabaseTransactionFailed,
            "Failed to commit streaming insert transaction",
            "QueryManager::scanAndSaveItemsToDatabase")
            .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        m_db.rollback();
        return false;
      }
      inTransaction = false;
      maybeEmitScanProgress(itemsInserted, -1);
    }
    if (itemsInserted % PROGRESS_REPORT_INTERVAL == 0) {
      maybeEmitScanProgress(itemsInserted, -1);
    }

    return true;
  };

  // Non-recursive scan: stream files directly
  if (!collection.includeContentSubfolders) {
    dirSignature = seedDirSignatureFromFilesystem(dir.absolutePath(), false);

    constexpr int SCAN_PROGRESS_INTERVAL = 500;
    int scanned = 0;

    QDirIterator iterator(dir.absolutePath(), nameFilters, QDir::Files,
                          QDirIterator::NoIteratorFlags);
    while (iterator.hasNext()) {
      if (isScanCancelled()) {
        break;
      }

      iterator.next();
      const QString relativePath = iterator.fileName();
      const QFileInfo info = iterator.fileInfo();
      batchPaths.append(relativePath);
      batchTimestamps[relativePath] = info.lastModified();

      ++scanned;
      if (scanned % SCAN_PROGRESS_INTERVAL == 0) {
        maybeEmitScanProgress(scanned, -1);
      }

      if (batchPaths.size() >= BATCH_SIZE) {
        if (!flushBatch()) {
          break;
        }
      }
    }

    (void)flushBatch();
  } else {
    // Recursive scan: scan directories in parallel and stream results.
    QElapsedTimer scanTimer;
    scanTimer.start();

    const QString rootPath = dir.absolutePath();
    const auto cancelToken = m_scanCancellationToken;
    if (!cancelToken) {
      return;
    }
    const std::atomic<bool> &cancelFlag = *cancelToken;

    const int maxThreads = std::max(1, m_scanThreadPool.maxThreadCount());
    const int maxInFlight = std::max(1, maxThreads * 2);

    ScanCompletionQueue queue;

    QVector<DirSignatureSample> signatureSamples;
    signatureSamples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
    {
      QFileInfo rootInfo(rootPath);
      addDirSignatureSample(signatureSamples,
                            DirSignatureSample{QString(), rootInfo.lastModified().toSecsSinceEpoch()},
                            UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
    }

    auto enqueue = [&](const QString &dirPath) {
      if (cancelFlag.load(std::memory_order_acquire)) {
        return;
      }
      {
        QMutexLocker locker(&queue.mutex);
        ++queue.inFlight;
      }
      m_scanThreadPool.start(new DirectoryScanTask(dirPath, rootPath, nameFilters, cancelToken, &queue));
    };

    // Always scan the root directory.
    enqueue(rootPath);

    QDirIterator dirIterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);

    int totalItemsScanned = 0;
    int lastReportedCount = 0;
    int directoriesEnqueued = 1;  // root
    int directoryResultsConsumed = 0;

    while (!cancelFlag.load(std::memory_order_acquire)) {
      // Keep the number of outstanding tasks bounded.
      while (dirIterator.hasNext() && !cancelFlag.load(std::memory_order_acquire)) {
        int inFlight = 0;
        {
          QMutexLocker locker(&queue.mutex);
          inFlight = queue.inFlight;
        }
        if (inFlight >= maxInFlight) {
          break;
        }
        dirIterator.next();
        const QString dirPath = dirIterator.filePath();
        enqueue(dirPath);
        {
          const QString relPath = QDir(rootPath).relativeFilePath(dirPath);
          const qint64 mtimeSec = QFileInfo(dirPath).lastModified().toSecsSinceEpoch();
          addDirSignatureSample(signatureSamples, DirSignatureSample{relPath, mtimeSec},
                                UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
        }
        ++directoriesEnqueued;
      }

      DirectoryScanResult result;
      bool gotResult = false;
      bool done = false;

      {
        QMutexLocker locker(&queue.mutex);
        while (queue.ready.isEmpty() && queue.inFlight > 0 &&
               !cancelFlag.load(std::memory_order_acquire)) {
          queue.hasResult.wait(&queue.mutex, 50);
        }

        if (!queue.ready.isEmpty()) {
          result = std::move(queue.ready.back());
          queue.ready.removeLast();
          gotResult = true;
        } else if (queue.inFlight == 0 && !dirIterator.hasNext()) {
          done = true;
        }
      }

      if (done) {
        break;
      }
      if (!gotResult) {
        continue;
      }

      ++directoryResultsConsumed;

      if (!result.relativePaths.isEmpty()) {
        for (const QString &p : result.relativePaths) {
          batchPaths.append(p);
          batchTimestamps.insert(p, result.timestamps.value(p));
          if (batchPaths.size() >= BATCH_SIZE) {
            if (!flushBatch()) {
              break;
            }
          }
        }

        totalItemsScanned += result.relativePaths.size();
        if (totalItemsScanned - lastReportedCount >= PROGRESS_REPORT_INTERVAL) {
          lastReportedCount = totalItemsScanned;
          maybeEmitScanProgress(totalItemsScanned, -1);
        }
      }
    }

    // Ensure all worker tasks have finished before destroying the queue.
    // This is critical on cancellation, where we may exit early.
    {
      QMutexLocker locker(&queue.mutex);
      while (queue.inFlight > 0) {
        queue.hasResult.wait(&queue.mutex, 50);
      }
      queue.ready.clear();
    }

    if (lcQueryManager().isDebugEnabled()) {
      qCDebug(lcQueryManager) << "Recursive scan+stream done"
                              << "collectionIndex=" << collectionIndex
                              << "cancelled=" << (cancelFlag.load(std::memory_order_acquire) ? "yes" : "no")
                              << "dirsEnqueued=" << directoriesEnqueued
                              << "dirResults=" << directoryResultsConsumed
                              << "filesFound=" << totalItemsScanned
                              << "elapsedMs=" << scanTimer.elapsed();
    }

                  dirSignature = buildDirSignatureJson(true, signatureSamples);

    (void)flushBatch();
  }

  // Final commit for any remaining items
  if (inTransaction) {
    if (!m_db.commit()) {
      auto err = ErrorContext::critical(
          ErrorCode::DatabaseTransactionFailed,
          "Failed to commit final streaming insert transaction",
          "QueryManager::scanAndSaveItemsToDatabase")
          .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      m_db.rollback();
      return;
    }
  }

  if (itemsInserted > 0 && !isScanCancelled()) {
    maybeEmitScanProgress(itemsInserted, -1, true);
  }

  if (isScanCancelled()) {
    return;
  }

  int legacyId = -1;
  if (!prepareCollectionForItemsInsert(collection, uuid, extSignature, legacyId)) {
    return;
  }

  // Apply staged scan results to persistent DB in one transaction.
  if (!m_db.transaction()) {
    auto err = ErrorContext::critical(
        ErrorCode::DatabaseTransactionFailed,
        "Failed to start transaction to apply scan results",
        "QueryManager::scanAndSaveItemsToDatabase")
        .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }
  const bool upsertOk = applyScannedItemsToDatabase(legacyId, uuid);
  const bool deleteOk = deleteMissingItemsByUuidUsingScannedItems(uuid);

  QSqlQuery &meta = getPreparedStatement(
      "UPDATE collections SET last_scanned = ?, dir_signature = ? WHERE uuid = ?");
  meta.bindValue(0, QDateTime::currentDateTime().toString(Qt::ISODate));
  meta.bindValue(1, dirSignature);
  meta.bindValue(2, uuid);
  const bool metaOk = meta.exec();

  if (upsertOk && deleteOk && metaOk) {
    (void)m_db.commit();
  } else {
    m_db.rollback();
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

  // IMPORTANT: Do not scan synchronously here.
  // Scans are dispatched to a dedicated scan worker so this query worker can
  // return counts immediately and keep the UI responsive.

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  const QString trimmedFilter = filter.trimmed();
  if (m_itemsFtsAvailable && !m_itemsFtsReady) {
    m_itemsFtsReady = isItemsFtsReadyFromDb();
  }
  const QString ftsQuery = (m_itemsFtsAvailable && m_itemsFtsReady && !trimmedFilter.isEmpty())
                               ? buildFtsPrefixQuery(trimmedFilter)
                               : QString();
  const bool useFts = !ftsQuery.isEmpty();

  QString sql;
  if (useFts) {
    sql = "SELECT COUNT(DISTINCT items.path) "
          "FROM items JOIN items_fts ON items_fts.rowid = items.id "
          "WHERE items.collection_uuid IN " + buildUuidInClause(uuids.size()) +
          " AND items_fts MATCH ?";
  } else {
    sql = "SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid IN "
          + buildUuidInClause(uuids.size());
  }
  
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
  
  if (!trimmedFilter.isEmpty()) {
    if (!useFts) {
      sql += " AND name LIKE ?";
    }
  }
  
  // Use cached prepared statement - dynamic SQL is cached by query string
  QSqlQuery &query = getPreparedStatement(sql);
  int bindPos = 0;
  for (const QString &uuid : uuids) {
    query.bindValue(bindPos++, uuid);
  }
  if (useFts) {
    query.bindValue(bindPos++, ftsQuery);
  }
  if (!subfolder.isEmpty()) {
    query.bindValue(bindPos++, subfolder + "/%");
  }
  if (!trimmedFilter.isEmpty() && !useFts) {
    query.bindValue(bindPos++, "%" + trimmedFilter + "%");
  }

  if (query.exec() && query.next()) {
    emit itemCountLoaded(query.value(0).toInt());
  } else {
    emit itemCountLoaded(0);
  }
}

void QueryManager::ensureScannedForContext(const CollectionContext &context,
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
                                   "QueryManager::ensureScannedForContext");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);

  // Scan current collection if needed.
  if (!ctx.config.mediaDirectory.trimmed().isEmpty()) {
    if (needsRescan(ctx.currentIndex, ctx.config)) {
      const QString uuid = CollectionUtils::computeCollectionUuid(
          ctx.config.name, ctx.config.mediaDirectory);
      ensureCollectionScanned(ctx.currentIndex, ctx.config);
      emit collectionScanCompleted(uuid);
    }
  }

  // Scan all collections if the query scope requests it.
  if (ctx.queryIncludeAllCollections) {
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig col = allCollections[i];
      col.mediaDirectory = PathUtils::validateAndExpandPath(col.mediaDirectory, col.name);
      if (col.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      if (needsRescan(i, col)) {
        const QString uuid = CollectionUtils::computeCollectionUuid(col.name, col.mediaDirectory);
        ensureCollectionScanned(i, col);
        emit collectionScanCompleted(uuid);
      }
    }
    return;
  }

  // Scan descendants if requested.
  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
    QList<int> rawDescendants = CollectionUtils::collectDescendantIndices(
        ctx.currentIndex, allCollections);
    for (int descendantIndex : rawDescendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }

      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory = PathUtils::validateAndExpandPath(
          subCol.mediaDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }

      if (needsRescan(descendantIndex, subCol)) {
        const QString uuid = CollectionUtils::computeCollectionUuid(
            subCol.name, subCol.mediaDirectory);
        ensureCollectionScanned(descendantIndex, subCol);
        emit collectionScanCompleted(uuid);
      }
    }
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

  const QString trimmedFilter = filter.trimmed();
  if (m_itemsFtsAvailable && !m_itemsFtsReady) {
    m_itemsFtsReady = isItemsFtsReadyFromDb();
  }
  const QString ftsQuery = (m_itemsFtsAvailable && m_itemsFtsReady && !trimmedFilter.isEmpty())
                               ? buildFtsPrefixQuery(trimmedFilter)
                               : QString();
  const bool useFts = !ftsQuery.isEmpty();

  QString sql;
  if (useFts) {
    sql = "SELECT DISTINCT items.path, items.collection_uuid "
          "FROM items JOIN items_fts ON items_fts.rowid = items.id "
          "WHERE items.collection_uuid IN " + buildUuidInClause(uuids.size()) +
          " AND items_fts MATCH ?";
  } else {
    sql = "SELECT DISTINCT path, collection_uuid FROM items WHERE collection_uuid IN "
          + buildUuidInClause(uuids.size());
  }
  
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
  
  if (!trimmedFilter.isEmpty()) {
    if (!useFts) {
      sql += " AND name LIKE ?";
    }
  }
  sql += " ORDER BY name COLLATE NOCASE LIMIT ? OFFSET ?";
  
  // Use cached prepared statement - dynamic SQL is cached by query string
  QSqlQuery &query = getPreparedStatement(sql);
  int bindPos = 0;
  for (const QString &uuid : uuids) {
    query.bindValue(bindPos++, uuid);
  }
  if (useFts) {
    query.bindValue(bindPos++, ftsQuery);
  }
  if (!subfolder.isEmpty()) {
    query.bindValue(bindPos++, subfolder + "/%");
  }
  if (!trimmedFilter.isEmpty() && !useFts) {
    query.bindValue(bindPos++, "%" + trimmedFilter + "%");
  }
  query.bindValue(bindPos++, limit);
  query.bindValue(bindPos++, offset);

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
  "SELECT last_scanned, name, ext_signature, dir_signature FROM collections WHERE uuid = ?";
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
  constexpr const char* UPDATE_COLLECTION_SCAN_METADATA =
      "UPDATE collections SET last_scanned = ?, dir_signature = ? WHERE uuid = ?";
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
  query.bindValue(0, uuid);

  bool rowPresent = query.exec() && query.next();
  if (!rowPresent) {
    return true;
  }

  QString storedName = query.value(1).toString();
  QString storedSignature = query.value(2).toString();
  QString storedDirSignature = query.value(3).toString();

  // Capture lastScanned before any early returns that might invalidate query state
  QDateTime lastScanned =
      QDateTime::fromString(query.value(0).toString(), Qt::ISODate);

  if (storedName != collection.name) {
    return true;
  }

  // If an older DB is missing ext_signature metadata, don't force a full rescan.
  // Seed it from the current config when items already exist.
  if (storedSignature != currentSignature) {
    QSqlQuery &countQuery = getPreparedStatement(QuerySQL::ITEMS_COUNT_BY_UUID);
    countQuery.bindValue(0, uuid);
    const bool hasItems =
        (countQuery.exec() && countQuery.next() && countQuery.value(0).toInt() > 0);
    if (hasItems && storedSignature.trimmed().isEmpty()) {
      QSqlQuery update(m_db);
      update.prepare("UPDATE collections SET ext_signature = ? WHERE uuid = ?");
      update.addBindValue(currentSignature);
      update.addBindValue(uuid);
      (void)update.exec();
    } else {
      return true;
    }
  }

  // Use cached prepared statement for item path check
  QSqlQuery &pathQuery = getPreparedStatement(QuerySQL::ITEM_PATH_CHECK);
  pathQuery.bindValue(0, uuid);

  if (pathQuery.exec() && pathQuery.next()) {
    QString storedPath = pathQuery.value(0).toString();
    QString storedFullPath =
        QDir(collection.mediaDirectory).absoluteFilePath(storedPath);
    if (!QFile::exists(storedFullPath)) {
      return true;
    }
  }

  QFileInfo dirInfo(collection.mediaDirectory);

  if (!dirInfo.exists()) {
    return true;
  }
  
  // When includeContentSubfolders is enabled, check for subdirectory modifications.
  // For large collections, this check is expensive (iterates all subdirs). 
  // Skip deep check if collection has items in DB - trust cached data on startup.
  // Full validation happens when user navigates into subfolders or forces refresh.
  if (collection.includeContentSubfolders) {
    // If we have items in the database, validate the stored directory signature
    // by checking a bounded set of sampled directories (cheap, avoids deep scans).
    QSqlQuery &countQuery = getPreparedStatement(QuerySQL::ITEMS_COUNT_BY_UUID);
    countQuery.bindValue(0, uuid);
    const bool hasItems =
        (countQuery.exec() && countQuery.next() && countQuery.value(0).toInt() > 0);
    if (!hasItems) {
      return true;
    }

    if (!storedDirSignature.trimmed().isEmpty()) {
      if (!dirSignatureStillValid(collection.mediaDirectory, true, storedDirSignature)) {
        return true;
      }
    } else {
      // Older DBs may have no dir_signature. Avoid forcing a full rescan when
      // items already exist; seed a bounded signature from the filesystem.
      const QString seeded = seedDirSignatureFromFilesystem(collection.mediaDirectory, true);
      if (seeded.trimmed().isEmpty()) {
        return true;
      }

      // Preserve the existing last_scanned value while recording the signature.
      QSqlQuery &meta = getPreparedStatement(QuerySQL::UPDATE_COLLECTION_SCAN_METADATA);
      const QString lastScannedIso = lastScanned.isValid()
                                        ? lastScanned.toString(Qt::ISODate)
                                        : QDateTime::currentDateTime().toString(Qt::ISODate);
      meta.bindValue(0, lastScannedIso);
      meta.bindValue(1, seeded);
      meta.bindValue(2, uuid);
      (void)meta.exec();
    }
  } else {
    // Flat collections: directory mtime is a sufficient cheap proxy for new/deleted files.
    if (dirInfo.lastModified() > lastScanned) {
      return true;
    }
  }

  // Use cached prepared statement for modified items count
  QSqlQuery &newer = getPreparedStatement(QuerySQL::ITEMS_MODIFIED_COUNT);
  newer.bindValue(0, uuid);
  newer.bindValue(1, lastScanned.toString(Qt::ISODate));
  newer.exec();

  return newer.next() && newer.value(0).toInt() > 0;
}

QStringList QueryManager::scanMediaDirectory(const CollectionConfig &collection,
                                         QHash<QString, QDateTime> &timestamps,
                                         QString *dirSignatureOut) {
  QStringList filePaths;
  QDir dir(collection.mediaDirectory);

  if (!dir.exists()) {
    return filePaths;
  }

  if (dirSignatureOut) {
    *dirSignatureOut = QString();
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
    if (dirSignatureOut) {
      *dirSignatureOut = seedDirSignatureFromFilesystem(dir.absolutePath(), false);
    }

    // Throttle scan progress emissions to avoid spamming the UI event loop.
    QElapsedTimer progressTimer;
    progressTimer.start();
    qint64 lastProgressEmitMs = -UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS;
    auto maybeEmitScanProgress = [&](int processed, int total, bool force = false) {
      if (force) {
        emit scanItemsProgress(processed, total);
        lastProgressEmitMs = progressTimer.elapsed();
        return;
      }
      const qint64 nowMs = progressTimer.elapsed();
      if (nowMs - lastProgressEmitMs < UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS) {
        return;
      }
      emit scanItemsProgress(processed, total);
      lastProgressEmitMs = nowMs;
    };

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
      const QString relativePath = iterator.fileName();
      const QFileInfo info = iterator.fileInfo();
      filePaths.append(relativePath);
      timestamps[relativePath] = info.lastModified();
      
      ++itemsScanned;
      if (itemsScanned % PROGRESS_REPORT_INTERVAL == 0) {
        maybeEmitScanProgress(itemsScanned, -1);
      }
    }
    
    if (itemsScanned > 0) {
      maybeEmitScanProgress(itemsScanned, -1, true);
    }
    return filePaths;
  }

  // Parallel scanning for recursive directory structures
  // Scan directories in parallel with bounded in-flight tasks and consume
  // results as they complete to avoid head-of-line blocking.
  QElapsedTimer scanTimer;
  scanTimer.start();

  const QString rootPath = dir.absolutePath();
  const QDir rootDir(rootPath);
  const auto cancelToken = m_scanCancellationToken;
  if (!cancelToken) {
    return filePaths;
  }
  const std::atomic<bool> &cancelFlag = *cancelToken;

  const int maxThreads = std::max(1, m_scanThreadPool.maxThreadCount());
  const int maxInFlight = std::max(1, maxThreads * 2);
  ScanCompletionQueue queue;

  QVector<DirSignatureSample> signatureSamples;
  signatureSamples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
  {
    QFileInfo rootInfo(rootPath);
    addDirSignatureSample(signatureSamples,
                          DirSignatureSample{QString(), rootInfo.lastModified().toSecsSinceEpoch()},
                          UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
  }

  auto enqueue = [&](const QString &dirPath) {
    if (cancelFlag.load(std::memory_order_acquire)) {
      return;
    }
    {
      QMutexLocker locker(&queue.mutex);
      ++queue.inFlight;
    }
    m_scanThreadPool.start(new DirectoryScanTask(dirPath, rootPath, nameFilters, cancelToken, &queue));
  };

  // Always scan root.
  enqueue(rootPath);

  QDirIterator dirIterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);

  int totalItemsScanned = 0;
  constexpr int PROGRESS_REPORT_INTERVAL = 500;
  int lastReportedCount = 0;
  int directoriesEnqueued = 1;  // root
  int directoryResultsConsumed = 0;

  // Throttle scan progress emissions to avoid spamming the UI event loop.
  QElapsedTimer progressTimer;
  progressTimer.start();
  qint64 lastProgressEmitMs = -UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS;
  auto maybeEmitScanProgress = [&](int processed, int total, bool force = false) {
    if (force) {
      emit scanItemsProgress(processed, total);
      lastProgressEmitMs = progressTimer.elapsed();
      return;
    }
    const qint64 nowMs = progressTimer.elapsed();
    if (nowMs - lastProgressEmitMs < UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS) {
      return;
    }
    emit scanItemsProgress(processed, total);
    lastProgressEmitMs = nowMs;
  };

  while (!cancelFlag.load(std::memory_order_acquire)) {
    // Fill in-flight queue.
    while (dirIterator.hasNext() && !cancelFlag.load(std::memory_order_acquire)) {
      int inFlight = 0;
      {
        QMutexLocker locker(&queue.mutex);
        inFlight = queue.inFlight;
      }
      if (inFlight >= maxInFlight) {
        break;
      }
      dirIterator.next();
      const QFileInfo dirInfo = dirIterator.fileInfo();
      const QString dirPath = dirIterator.filePath();
      enqueue(dirPath);
      {
        const QString relPath = rootDir.relativeFilePath(dirPath);
        const qint64 mtimeSec = dirInfo.lastModified().toSecsSinceEpoch();
        addDirSignatureSample(signatureSamples, DirSignatureSample{relPath, mtimeSec},
                              UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
      }
      ++directoriesEnqueued;
    }

    DirectoryScanResult result;
    bool gotResult = false;
    bool done = false;

    {
      QMutexLocker locker(&queue.mutex);
      while (queue.ready.isEmpty() && queue.inFlight > 0 &&
             !cancelFlag.load(std::memory_order_acquire)) {
        queue.hasResult.wait(&queue.mutex, 50);
      }

      if (!queue.ready.isEmpty()) {
        result = std::move(queue.ready.back());
        queue.ready.removeLast();
        gotResult = true;
      } else if (queue.inFlight == 0 && !dirIterator.hasNext()) {
        done = true;
      }
    }

    if (done) {
      break;
    }
    if (!gotResult) {
      continue;
    }

    ++directoryResultsConsumed;

    if (result.relativePaths.isEmpty()) {
      continue;
    }

    filePaths.reserve(filePaths.size() + result.relativePaths.size());
    filePaths.append(result.relativePaths);

    timestamps.reserve(timestamps.size() + result.timestamps.size());
    for (auto it = result.timestamps.constBegin(); it != result.timestamps.constEnd(); ++it) {
      timestamps.insert(it.key(), it.value());
    }

    totalItemsScanned += result.relativePaths.size();
    if (totalItemsScanned - lastReportedCount >= PROGRESS_REPORT_INTERVAL) {
      lastReportedCount = totalItemsScanned;
      maybeEmitScanProgress(totalItemsScanned, -1);
    }
  }

  // Ensure all worker tasks have finished before destroying the queue.
  // This is critical on cancellation, where we may exit early.
  {
    QMutexLocker locker(&queue.mutex);
    while (queue.inFlight > 0) {
      queue.hasResult.wait(&queue.mutex, 50);
    }
    queue.ready.clear();
  }

  if (lcQueryManager().isDebugEnabled()) {
    qCDebug(lcQueryManager) << "Recursive scan done"
                            << "collection=" << collection.name
                            << "cancelled=" << (cancelFlag.load(std::memory_order_acquire) ? "yes" : "no")
                            << "dirsEnqueued=" << directoriesEnqueued
                            << "dirResults=" << directoryResultsConsumed
                            << "filesFound=" << totalItemsScanned
                            << "elapsedMs=" << scanTimer.elapsed();
  }
  
  // Check if cancelled during parallel scan
  if (isScanCancelled()) {
    filePaths.clear();
    timestamps.clear();
    return filePaths;
  }

  if (dirSignatureOut) {
    *dirSignatureOut = buildDirSignatureJson(true, signatureSamples);
  }
  
  // Emit final progress
  if (totalItemsScanned > 0) {
    maybeEmitScanProgress(totalItemsScanned, -1, true);
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
    QString dirSignature;
    filePaths = scanMediaDirectory(collection, timestamps, &dirSignature);
    if (!filePaths.isEmpty()) {
      saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection, dirSignature);
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
  const CollectionConfig &collection,
  const QString &dirSignature) {
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
  if (!pragmaOff.exec("PRAGMA synchronous = OFF")) {
    ErrorUtils::logError(
        ErrorContext::warning(
            ErrorCode::DatabaseQueryFailed,
            "Failed to set synchronous=OFF for bulk insert",
            "QueryManager::saveItemsToDatabase")
            .withDetails(pragmaOff.lastError().text()));
  }
  const SynchronousPragmaGuard restoreSynchronous(m_db);

  // Batch insert for performance - SQLite handles up to 999 variables per statement
  // With 5 columns per row, we can insert 199 rows per batch (995 variables)
  constexpr int BATCH_SIZE = 199;
  // Commit every N batches to save incremental progress (~100K items per commit)
  constexpr int COMMIT_INTERVAL_BATCHES = 500;
  constexpr int PROGRESS_REPORT_INTERVAL = 50000;  // Report every 50K items
  
  const int totalItems = filePaths.size();

  // Cancellation-safe writes: stage into TEMP table and only apply to
  // persistent DB on success.
  if (!ensureScannedItemsTempTable()) {
    return;
  }

  clearScannedItemsTempTable();

  struct ScannedItemsTempTableCleanup {
    QueryManager *self = nullptr;
    bool enabled = false;
    ~ScannedItemsTempTableCleanup() {
      if (enabled && self) {
        self->clearScannedItemsTempTable();
      }
    }
  } scannedItemsCleanup{this, true};

  // Second phase: insert items in batches with periodic commits
  int batchesSinceCommit = 0;
  bool inTransaction = false;
  int itemsInserted = 0;
  
  for (int batchStart = 0; batchStart < totalItems; batchStart += BATCH_SIZE) {
    // Check for cancellation between batches
    if (isScanCancelled()) {
      if (inTransaction) {
        m_db.rollback();
        inTransaction = false;
      }
      break;
    }
    
    // Start new transaction if needed
    if (!inTransaction) {
      if (!m_db.transaction()) {
        auto err = ErrorContext::critical(
            ErrorCode::DatabaseTransactionFailed,
            "Failed to start transaction for bulk insert",
            "QueryManager::saveItemsToDatabase")
            .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        return;
      }
      inTransaction = true;
      batchesSinceCommit = 0;
    }
    
    const int batchEnd = qMin(batchStart + BATCH_SIZE, totalItems);
    const int batchCount = batchEnd - batchStart;
    
    // Stage scanned items into TEMP table.
    QStringList batchPaths;
    batchPaths.reserve(batchCount);
    for (int i = batchStart; i < batchEnd; ++i) {
      batchPaths.append(filePaths[i]);
    }
    insertScannedItemsBatch(batchPaths, timestamps);
    
    itemsInserted = batchEnd;
    ++batchesSinceCommit;
    
    // Commit periodically to save incremental progress
    if (batchesSinceCommit >= COMMIT_INTERVAL_BATCHES) {
      if (!m_db.commit()) {
        auto err = ErrorContext::critical(
            ErrorCode::DatabaseTransactionFailed,
            "Failed to commit bulk insert transaction",
            "QueryManager::saveItemsToDatabase")
            .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        m_db.rollback();
        return;
      }
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
    if (!m_db.commit()) {
      auto err = ErrorContext::critical(
          ErrorCode::DatabaseTransactionFailed,
          "Failed to commit final bulk insert transaction",
          "QueryManager::saveItemsToDatabase")
          .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      m_db.rollback();
      return;
    }
  }
  
  // Final progress report
  if (!isScanCancelled()) {
    emit scanItemsProgress(totalItems, totalItems);

    int legacyId = -1;
    if (!prepareCollectionForItemsInsert(collection, uuid, extSignature, legacyId)) {
      return;
    }

    // Apply staged results atomically.
    if (!m_db.transaction()) {
      auto err = ErrorContext::critical(
          ErrorCode::DatabaseTransactionFailed,
          "Failed to start transaction to apply staged scan results",
          "QueryManager::saveItemsToDatabase")
          .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return;
    }

    const bool upsertOk = applyScannedItemsToDatabase(legacyId, uuid);
    const bool deleteOk = deleteMissingItemsByUuidUsingScannedItems(uuid);

    QSqlQuery &meta = getPreparedStatement(QuerySQL::UPDATE_COLLECTION_SCAN_METADATA);
    meta.bindValue(0, QDateTime::currentDateTime().toString(Qt::ISODate));
    meta.bindValue(1, dirSignature);
    meta.bindValue(2, uuid);
    const bool metaOk = meta.exec();

    if (upsertOk && deleteOk && metaOk) {
      (void)m_db.commit();
    } else {
      m_db.rollback();
    }
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
  query.bindValue(0, collectionUuid);

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
      query.bindValue(0, collectionUuid);
      if (!query.exec()) {
        throw std::runtime_error(query.lastError().text().toStdString());
      }

      // Use cached prepared statement for deleting collection
      QSqlQuery &delc = getPreparedStatement(QuerySQL::DELETE_COLLECTION_BY_UUID);
      delc.bindValue(0, collectionUuid);
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
  if (map.find(key) == map.end()) {
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

  QSet<QString> localSeenCanonicalPaths;
  QSet<QString> *effectiveSeenCanonicalPaths = seenCanonicalPaths;
  if (dedup && !effectiveSeenCanonicalPaths) {
    // Ensure dedup stays O(n) even when the caller doesn't provide a set.
    // Preserve ordering by only using the set for membership checks.
    localSeenCanonicalPaths.reserve(allFilePaths.size() + filePaths.size());
    for (const QString &existing : allFilePaths) {
      localSeenCanonicalPaths.insert(existing);
    }
    effectiveSeenCanonicalPaths = &localSeenCanonicalPaths;
  }

  if (!filePaths.isEmpty()) {
    // Pre-reserve to reduce rehashing and reallocations in hot paths.
    // Worst-case we insert 4 keys per file for the dir/index maps.
    const int incoming = filePaths.size();
    allFilePaths.reserve(allFilePaths.size() + incoming);
    allFileNames.reserve(allFileNames.size() + incoming);
    fileToArtworkDir.reserve(fileToArtworkDir.size() + incoming * 4);
    fileToMediaDir.reserve(fileToMediaDir.size() + incoming * 4);
    fileToCollectionIndex.reserve(fileToCollectionIndex.size() + incoming * 4);
    if (seenCanonicalPaths) {
      seenCanonicalPaths->reserve(seenCanonicalPaths->size() + incoming);
    }
    if (canonicalPathCache) {
      canonicalPathCache->reserve(canonicalPathCache->size() + incoming);
    }
  }

  for (const QString &file : filePaths) {
    const QString absPath = mediaQDir.absoluteFilePath(file);
    const QString keyPath = canonicalKeyPath(absPath, dedup, canonicalPathCache);

    if (dedup) {
      if (effectiveSeenCanonicalPaths && !effectiveSeenCanonicalPaths->contains(keyPath)) {
        effectiveSeenCanonicalPaths->insert(keyPath);
        allFilePaths.append(keyPath);
      }
    } else {
      allFilePaths.append(keyPath);
    }

    const int lastSeparator = std::max(file.lastIndexOf('/'), file.lastIndexOf('\\'));
    const QString fileName = (lastSeparator >= 0) ? file.mid(lastSeparator + 1) : file;
    const int lastDot = fileName.lastIndexOf('.');
    const QString baseName = (lastDot > 0) ? fileName.left(lastDot) : fileName;
    const QString displayName = displayNameForBase(baseName);

    allFileNames[keyPath] = displayName;

    insertIfAbsent(fileToArtworkDir, keyPath, mappingArtworkDir);
    insertIfAbsent(fileToArtworkDir, file, mappingArtworkDir);
    if (fileName != file) {
      insertIfAbsent(fileToArtworkDir, fileName, mappingArtworkDir);
    }
    if (baseName != file && baseName != fileName) {
      insertIfAbsent(fileToArtworkDir, baseName, mappingArtworkDir);
    }

    insertIfAbsent(fileToMediaDir, keyPath, mediaDir);
    insertIfAbsent(fileToMediaDir, file, mediaDir);
    if (fileName != file) {
      insertIfAbsent(fileToMediaDir, fileName, mediaDir);
    }
    if (baseName != file && baseName != fileName) {
      insertIfAbsent(fileToMediaDir, baseName, mediaDir);
    }

    insertIfAbsent(fileToCollectionIndex, keyPath, collectionIndex);
    insertIfAbsent(fileToCollectionIndex, file, collectionIndex);
    if (fileName != file) {
      insertIfAbsent(fileToCollectionIndex, fileName, collectionIndex);
    }
    if (baseName != file && baseName != fileName) {
      insertIfAbsent(fileToCollectionIndex, baseName, collectionIndex);
    }
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

  struct SortEntry {
    QString path;
    QString sortKey;
    int priority = 0;
  };

  QVector<SortEntry> entries;
  entries.reserve(allFilePaths.size());
  for (const QString &path : allFilePaths) {
    const QString baseName = QFileInfo(path).completeBaseName();
    QString sortKey = PathUtils::normalizeDisplayName(baseName);
    if (baseName.startsWith('\'') && baseName.length() > 1 &&
        (baseName[1].isDigit() || baseName[1].isLetter())) {
      sortKey = PathUtils::normalizeDisplayName(baseName.mid(1));
    }
    entries.append(SortEntry{path, sortKey, getCharacterSortPriority(sortKey)});
  }

  std::ranges::sort(entries, [&](const SortEntry &lhs, const SortEntry &rhs) {
    if (lhs.priority != rhs.priority) {
      return descending ? lhs.priority > rhs.priority : lhs.priority < rhs.priority;
    }
    const int cmp = lhs.sortKey.compare(rhs.sortKey, Qt::CaseInsensitive);
    return descending ? cmp > 0 : cmp < 0;
  });

  allFilePaths.clear();
  allFilePaths.reserve(entries.size());
  for (const SortEntry &entry : entries) {
    allFilePaths.append(entry.path);
  }
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
