// Executes SQLite queries on worker thread for paginated item loading and
// filtering.
#include "querymanager.h"
#include "collectionutils.h"
#include "dbmigrations.h"
#include "errorutils.h"
#include "pathutils.h"
#include "sessionmanager.h"
#include "uiconstants.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QRunnable>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QVector>
#include <QWaitCondition>
#include <QtConcurrent>
#include <QtGlobal>
#include <algorithm>
#include <atomic>
#include <random>
#include <stdexcept>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcQueryManager, "kartend.querymanager")
#define debugLog(msg) qCDebug(lcQueryManager) << msg

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

// Forward declarations of static helpers
static auto canonicalKeyPath(const QString &absPath, bool dedup,
                             QHash<QString, QString> *canonicalPathCache)
    -> QString;
static auto displayNameForBase(const QString &baseName) -> QString;

namespace {
// Result struct for parallel directory scanning
// Holds files found in a single directory plus their timestamps
struct DirectoryScanResult {
  QStringList relativePaths;
  QHash<QString, QDateTime> timestamps;
};

struct DirSignatureSample {
  QString relPath; // Relative to collection root. Empty means root.
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
                                  const QVector<DirSignatureSample> &samples)
    -> QString {
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
                                  QVector<DirSignatureSample> &samplesOut)
    -> bool {
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
    s.mtimeSec =
        static_cast<qint64>(o.value(QStringLiteral("t")).toDouble(0.0));
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
  addDirSignatureSample(
      samples,
      DirSignatureSample{QString(), rootInfo.lastModified().toSecsSinceEpoch()},
      UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);

  if (includeSubfolders) {
    QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    int inspected = 0;
    while (it.hasNext() &&
           inspected < UIConstants::Database::DIR_SIGNATURE_SEED_MAX_DIRS) {
      it.next();
      const QString absPath = it.filePath();
      const QString relPath = QDir(rootPath).relativeFilePath(absPath);
      const qint64 mtimeSec =
          QFileInfo(absPath).lastModified().toSecsSinceEpoch();
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
    const QString absPath =
        s.relPath.isEmpty() ? rootPath : root.absoluteFilePath(s.relPath);
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

struct ScanCompletionQueue {
  QMutex mutex;
  QWaitCondition hasResult;
  QWaitCondition hasSpace;
  QVector<DirectoryScanResult> ready;
  int inFlight = 0;
};

class DirectoryScanTask final : public QRunnable {
public:
  DirectoryScanTask(QString dirPath, QString rootPath, QStringList nameFilters,
                    std::shared_ptr<std::atomic_bool> cancelToken,
                    ScanCompletionQueue *queue)
      : m_dirPath(std::move(dirPath)), m_rootPath(std::move(rootPath)),
        m_nameFilters(std::move(nameFilters)),
        m_cancelToken(std::move(cancelToken)), m_queue(queue) {
    setAutoDelete(true);
  }

  void run() override {
    if (!m_queue || !m_cancelToken) {
      return;
    }

    // Scan this directory (non-recursively) but emit bounded chunks so a single
    // huge folder cannot allocate an unbounded QStringList/QHash in memory.
    QDir rootDir(m_rootPath);
    QDirIterator iterator(m_dirPath, m_nameFilters, QDir::Files,
                          QDirIterator::NoIteratorFlags);

    auto pushChunk = [&](DirectoryScanResult &&chunk) {
      if (!m_queue) {
        return;
      }

      // Backpressure: block (with timeout) if the queue is full, so memory
      // stays bounded even when directory scans outpace the consumer.
      QMutexLocker locker(&m_queue->mutex);
      while (m_queue->ready.size() >=
                 UIConstants::Database::SCAN_READY_MAX_RESULTS &&
             !m_cancelToken->load(std::memory_order_acquire)) {
        m_queue->hasSpace.wait(&m_queue->mutex, 50);
      }

      if (m_cancelToken->load(std::memory_order_acquire)) {
        return;
      }

      m_queue->ready.append(std::move(chunk));
      m_queue->hasResult.wakeOne();
    };

    DirectoryScanResult chunk;
    chunk.relativePaths.reserve(
        UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE);
    chunk.timestamps.reserve(UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE *
                             2);

    while (iterator.hasNext()) {
      if (m_cancelToken->load(std::memory_order_acquire)) {
        break;
      }

      iterator.next();
      const QString filePath = iterator.filePath();
      const QString relativePath = rootDir.relativeFilePath(filePath);
      const QFileInfo info = iterator.fileInfo();

      chunk.relativePaths.append(relativePath);
      chunk.timestamps.insert(relativePath, info.lastModified());

      if (chunk.relativePaths.size() >=
          UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE) {
        pushChunk(std::move(chunk));
        chunk = DirectoryScanResult{};
        chunk.relativePaths.reserve(
            UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE);
        chunk.timestamps.reserve(
            UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE * 2);
      }
    }

    if (!m_cancelToken->load(std::memory_order_acquire) &&
        !chunk.relativePaths.isEmpty()) {
      pushChunk(std::move(chunk));
    }

    // Mark this directory task as complete.
    {
      QMutexLocker locker(&m_queue->mutex);
      --m_queue->inFlight;
      m_queue->hasResult.wakeOne();
    }
  }

private:
  QString m_dirPath;
  QString m_rootPath;
  QStringList m_nameFilters;
  std::shared_ptr<std::atomic_bool> m_cancelToken;
  ScanCompletionQueue *m_queue = nullptr;
};

class SynchronousPragmaGuard {
public:
  explicit SynchronousPragmaGuard(QSqlDatabase &db) : m_db(db) {}

  SynchronousPragmaGuard(const SynchronousPragmaGuard &) = delete;
  auto operator=(const SynchronousPragmaGuard &)
      -> SynchronousPragmaGuard & = delete;

  SynchronousPragmaGuard(SynchronousPragmaGuard &&) = delete;
  auto operator=(SynchronousPragmaGuard &&)
      -> SynchronousPragmaGuard & = delete;

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
                           const QString &connectionName, QObject *parent)
    : QObject(parent),
      m_scanCancellationToken(std::make_shared<std::atomic_bool>(false)),
      m_sessionManager(sessionManager), m_connectionName(connectionName) {
  // Pointer-based cache with automatic LRU eviction.
  m_statementCache.setMaxCost(MAX_STATEMENT_CACHE_SIZE);

  const int idealThreads = QThread::idealThreadCount();
  const int base =
      idealThreads > 0
          ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
          : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_scanThreadPool = new QThreadPool();
  m_scanThreadPool->setMaxThreadCount(
      std::clamp(base, UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                 UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));

  // Register ErrorContext for queued signal/slot connections
  qRegisterMetaType<ErrorUtils::ErrorContext>("ErrorUtils::ErrorContext");
}

QueryManager::~QueryManager() {
  // Abandon the thread pool without waiting - process is exiting anyway.
  if (m_scanThreadPool) {
    m_scanThreadPool->clear();
    // Intentionally NOT deleting - ~QThreadPool blocks. Let OS clean up.
    m_scanThreadPool = nullptr;
  }

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
  if (m_scanThreadPool) {
    m_scanThreadPool->clear();
  }
}

bool QueryManager::isScanCancelled() const {
  return m_scanCancellationToken &&
         m_scanCancellationToken->load(std::memory_order_acquire);
}

void QueryManager::resetScanCancellation() {
  m_scanCancellationToken = std::make_shared<std::atomic_bool>(false);
}

// Forces the connection to see the latest WAL commits from other connections.
// In SQLite WAL mode, a connection can hold onto a stale read snapshot if it
// has an open transaction or cached statements. This method ensures we see
// data committed by the scan worker running on a separate connection.
void QueryManager::refreshWalView() {
  // Starting and immediately committing a deferred transaction forces SQLite
  // to acquire a fresh read snapshot that includes all prior commits.
  // This is lighter than wal_checkpoint and doesn't interfere with writes.
  QSqlQuery query(m_db);
  query.exec("BEGIN");
  query.exec("COMMIT");

  // Clear statement cache to prevent stale bound values from interfering
  // with subsequent queries that need fresh data.
  m_statementCache.clear();
}

// Gets or creates a prepared statement for the given SQL
// Caches compiled statements to avoid repeated prepare() overhead
// Uses LRU eviction when cache exceeds MAX_STATEMENT_CACHE_SIZE
auto QueryManager::getPreparedStatement(const QString &sql) -> QSqlQuery & {
  if (QSqlQuery *cached = m_statementCache.object(sql)) {
    // Fully reset before reuse.
    // QSqlQuery may retain bound values / internal state across exec() calls.
    // If the cached instance is reused for dynamic search SQL, SQLite can
    // report "Parameter count mismatch" unless we reinitialize it.
    cached->finish();
    *cached = QSqlQuery(m_db);
    cached->prepare(sql);
    return *cached;
  }

  // Create new prepared statement and cache it (QCache takes ownership).
  auto *query = new QSqlQuery(m_db);
  query->prepare(sql);
  m_statementCache.insert(sql, query, 1);
  return *query;
}

// Clears statement cache - call when database connection changes
void QueryManager::clearStatementCache() { m_statementCache.clear(); }

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
          QString("Database reconnection successful on attempt %1")
              .arg(attempt),
          "QueryManager::ensureDatabaseConnection");
      ErrorUtils::logError(success);

      // Re-initialize PRAGMAs after reconnection
      QSqlQuery query(m_db);
      query.exec("PRAGMA foreign_keys = ON");
      query.exec("PRAGMA journal_mode = WAL");
      query.exec("PRAGMA busy_timeout = 500");
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
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to create database directory",
                                      "QueryManager::initDatabase")
                   .withDetails(QString("Path: %1").arg(dbPath));
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }
  m_db.setDatabaseName(dbPath + "/media.db");

  if (!m_db.open()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to open database",
                                      "QueryManager::initDatabase")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  QSqlQuery query(m_db);
  if (!query.exec("PRAGMA foreign_keys = ON")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to enable foreign keys",
                                     "QueryManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  if (!query.exec("PRAGMA journal_mode = WAL")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to enable WAL mode",
                                     "QueryManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  // Set busy timeout to prevent indefinite blocking when another connection
  // holds a lock (e.g., FTS backfill on scan worker). Queries will fail with
  // SQLITE_BUSY after the timeout, allowing graceful fallback.
  if (!query.exec("PRAGMA busy_timeout = 500")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to set busy timeout",
                                     "QueryManager::initDatabase")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  // Use NORMAL synchronous mode for better write performance while maintaining
  // data safety - WAL mode already provides crash recovery guarantees
  if (!query.exec("PRAGMA synchronous = NORMAL")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
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
  query.exec(
      "CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)");
  query.exec("CREATE INDEX IF NOT EXISTS idx_items_collection_uuid ON "
             "items(collection_uuid)");
  // Composite index for sorted range queries - enables efficient ORDER BY name
  // when filtering by collection_uuid (common in showAllSubcollectionItems
  // mode)
  query.exec("CREATE INDEX IF NOT EXISTS idx_items_uuid_name ON "
             "items(collection_uuid, name COLLATE NOCASE)");

  DbMigrations::applySchemaMigrations(
      m_db, QStringLiteral("QueryManager::initDatabase"));

  refreshSearchCapabilities();
}

void QueryManager::refreshSearchCapabilities() {
  m_itemsFtsAvailable = false;
  m_itemsFtsReady = false; // Conservative default - will be set true by
                           // isItemsFtsReadyFromDb()
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
  q.exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT "
         "NOT NULL)");
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

  // Read-only check: if the meta table or key doesn't exist, assume FTS is NOT
  // ready. The scan worker will set the "items_fts_ready" marker when backfill
  // completes. This avoids write operations that could block on the scan
  // worker's FTS backfill transaction.
  QString ready;
  if (!tryReadMetaValue(m_db, QStringLiteral("items_fts_ready"), ready)) {
    // Key not found - FTS backfill hasn't completed yet
    return false;
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
  (void)tryReadMetaInt(m_db, QStringLiteral("items_fts_indexed_up_to_id"),
                       indexedUpToId);

  while (slice.elapsed() < UIConstants::Database::FTS_BACKFILL_TIME_BUDGET_MS) {
    // Determine the current max id in the content table.
    qint64 maxId = 0;
    {
      QSqlQuery maxQ(m_db);
      if (maxQ.exec("SELECT COALESCE(MAX(id), 0) FROM items") && maxQ.next()) {
        maxId = maxQ.value(0).toLongLong();
      }
    }

    if (maxId == 0) {
      // No items in database yet - don't mark FTS ready since there's nothing
      // to index. Items will be populated by a scan; we'll be called again
      // after the scan completes to do the actual backfill.
      return;
    }

    if (maxId <= indexedUpToId) {
      writeMetaValue(m_db, QStringLiteral("items_fts_ready"),
                     QStringLiteral("1"));
      m_itemsFtsReady = true;
      return;
    }

    if (!m_db.transaction()) {
      auto err =
          ErrorContext::warning(ErrorCode::DatabaseTransactionFailed,
                                "Failed to start transaction for FTS backfill",
                                "QueryManager::ensureItemsFtsReady")
              .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return;
    }

    QSqlQuery insertQ(m_db);
    insertQ.prepare("INSERT INTO items_fts(rowid, name, path, collection_uuid) "
                    "SELECT id, name, path, collection_uuid "
                    "FROM items "
                    "WHERE id > ? "
                    "ORDER BY id "
                    "LIMIT ?");
    insertQ.addBindValue(indexedUpToId);
    insertQ.addBindValue(UIConstants::Database::FTS_BACKFILL_BATCH_SIZE);

    if (!insertQ.exec()) {
      m_db.rollback();
      auto err = ErrorContext::warning(
                     ErrorCode::DatabaseQueryFailed,
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
      lastIdQ.prepare("SELECT COALESCE(MAX(id), ?) FROM ("
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
  // Defer between slices to keep the scan worker event loop responsive AND
  // avoid a tight 0ms loop that can peg a CPU core on very large databases.
  QTimer::singleShot(UIConstants::Database::FTS_BACKFILL_SLICE_DELAY_MS, this,
                     &QueryManager::ensureItemsFtsReady);
}

void QueryManager::loadAllCollections(
    const QList<CollectionConfig> &allCollections) {
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

    // Only emit progress signal when a scan is actually needed (not for cached
    // loads)
    if (needsRescan(collectionIndex, collection)) {
      emit scanProgress(collectionIndex + 1, totalCollections, collection.name);
    }

    QHash<QString, QDateTime> timestamps;
    QStringList filePaths =
        loadOrScanCollection(collectionIndex, collection, timestamps);

    appendFileMapsAndListCanonical(collectionIndex, collection,
                                   CollectionUtils::resolveArtworkDirectory(
                                       collectionIndex, allCollections),
                                   filePaths, allFilePaths, allFileNames,
                                   fileToArtworkDir, fileToMediaDir,
                                   fileToCollectionIndex, false);
  }

  sortFiles(allFilePaths);

  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
                   fileToCollectionIndex);
}

void QueryManager::loadItems(const CollectionContext &context,
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
    emit itemsLoaded(QStringList(), QHash<QString, QString>(),
                     QHash<QString, QString>(), QHash<QString, QString>(),
                     QHash<QString, int>());
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
  } else if (ctx.config.includeContentSubfolders &&
             !ctx.config.showAllSubfolderItems) {
    // At root with subfolders enabled but NOT showing all items - exclude items
    // in subfolders
    QStringList filtered;
    for (const QString &path : filePaths) {
      if (!path.contains('/')) {
        filtered.append(path);
      }
    }
    filePaths = filtered;
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown
  // mixed together

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  // Resolve artwork directory with parent fallback for subcollections
  QString resolvedArtworkDir = CollectionUtils::resolveArtworkDirectory(
      ctx.currentIndex, allCollections);

  appendFileMapsAndListCanonical(ctx.currentIndex, ctx.config,
                                 resolvedArtworkDir, filePaths, allFilePaths,
                                 allFileNames, fileToArtworkDir, fileToMediaDir,
                                 fileToCollectionIndex, false);

  sortFiles(allFilePaths, ctx.sortMode);

  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
                   fileToCollectionIndex);
}

void QueryManager::loadItemsWithSubcollections(
    const CollectionContext &context,
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
    } else if (mainCtx.config.includeContentSubfolders &&
               !mainCtx.config.showAllSubfolderItems) {
      // At root with subfolders enabled but NOT showing all items - exclude
      // items in subfolders
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

    appendFileMapsAndListCanonical(mainCtx.currentIndex, mainCtx.config,
                                   CollectionUtils::resolveArtworkDirectory(
                                       mainCtx.currentIndex, allCollections),
                                   mainFilePaths, allFilePaths, allFileNames,
                                   fileToArtworkDir, fileToMediaDir,
                                   fileToCollectionIndex, true,
                                   &seenCanonicalPaths, &canonicalPathCache);
  }

  // Use pre-computed descendants if available (O(1) from cache), otherwise
  // fall back to O(n²) tree traversal for backward compatibility
  const QList<int> &rawDescendants =
      mainCtx.precomputedDescendants.isEmpty()
          ? CollectionUtils::collectDescendantIndices(mainCtx.currentIndex,
                                                      allCollections)
          : mainCtx.precomputedDescendants;
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

    appendFileMapsAndListCanonical(collectionIndex, collection,
                                   CollectionUtils::resolveArtworkDirectory(
                                       collectionIndex, allCollections),
                                   subFilePaths, allFilePaths, allFileNames,
                                   fileToArtworkDir, fileToMediaDir,
                                   fileToCollectionIndex, true,
                                   &seenCanonicalPaths, &canonicalPathCache);
  }

  sortFiles(allFilePaths, context.sortMode);
  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
                   fileToCollectionIndex);
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
    // Check if we need to use temp table for large UUID lists
    bool useTempTable = false;
    const QString clause = buildUuidFilterClause(collectionUuids, useTempTable);

    if (useTempTable) {
      if (!ensureQueryUuidsPopulated(collectionUuids)) {
        emit cachedCountsComputed(generation, globalCount, directCountsByUuid);
        return;
      }
    }

    QSqlQuery query(m_db);
    QString sql;
    if (useTempTable) {
      sql = "SELECT collection_uuid, COUNT(DISTINCT path) "
            "FROM items WHERE EXISTS " +
            clause + " GROUP BY collection_uuid";
    } else {
      sql = "SELECT collection_uuid, COUNT(DISTINCT path) "
            "FROM items WHERE collection_uuid IN " +
            clause + " GROUP BY collection_uuid";
    }
    query.prepare(sql);

    // Only bind UUIDs when not using temp table
    if (!useTempTable) {
      for (const QString &uuid : collectionUuids) {
        query.addBindValue(uuid);
      }
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
auto QueryManager::collectCollectionUuids(
    const CollectionContext &ctx, const QList<CollectionConfig> &allCollections)
    -> QStringList {
  QStringList uuids;

  if (ctx.queryIncludeAllCollections) {
    QSet<QString> seen;
    uuids.reserve(allCollections.size());
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig c = allCollections[i];
      c.mediaDirectory =
          PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      if (c.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      const QString uuid =
          CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
      if (!uuid.isEmpty() && !seen.contains(uuid)) {
        seen.insert(uuid);
        uuids.append(uuid);
      }
    }
    return uuids;
  }

  // Check if we need descendants (for subcollection search modes)
  const bool needsDescendants =
      ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants;

  // Use pre-computed UUIDs if available and we need descendants
  // This provides major performance improvement for large hierarchies (3000+
  // subcollections) by eliminating repeated filesystem exists() checks and SHA1
  // hash computations
  if (needsDescendants && !ctx.precomputedDescendantUuids.isEmpty()) {
    return ctx.precomputedDescendantUuids;
  }

  uuids << CollectionUtils::computeCollectionUuid(ctx.config.name,
                                                  ctx.config.mediaDirectory);

  if (needsDescendants) {
    // Use pre-computed descendants if available (O(1) from cache), otherwise
    // fall back to O(n²) tree traversal for backward compatibility
    const QList<int> &descendants =
        ctx.precomputedDescendants.isEmpty()
            ? CollectionUtils::collectDescendantIndices(ctx.currentIndex,
                                                        allCollections)
            : ctx.precomputedDescendants;
    uuids.reserve(uuids.size() + descendants.size());
    for (int descendantIndex : descendants) {
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
      uuids << CollectionUtils::computeCollectionUuid(subCol.name,
                                                      subCol.mediaDirectory);
    }
  }
  return uuids;
}

// Builds UUID-to-directory mappings for resolving paths from query results
auto QueryManager::buildDirectoryMaps(
    const CollectionContext &ctx, const QList<CollectionConfig> &allCollections)
    -> CollectionDirMaps {
  CollectionDirMaps maps;

  // Check if we need descendants (for subcollection search modes)
  const bool needsDescendants =
      ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants;

  // Use pre-computed directory maps if available and we need descendants
  // This provides major performance improvement for large hierarchies (3000+
  // subcollections) by eliminating repeated path expansions and artwork
  // resolution during range loading
  if (needsDescendants && !ctx.precomputedUuidToMediaDir.isEmpty()) {
    maps.uuidToMediaDir = ctx.precomputedUuidToMediaDir;
    maps.uuidToArtworkDir = ctx.precomputedUuidToArtworkDir;
    maps.uuidToCollectionIndex = ctx.precomputedUuidToCollectionIndex;
    return maps;
  }

  // Helper to add mapping for a collection at a given index, resolving artwork
  // directory from parent chain if not set on the collection itself
  auto addMapping = [&](int collectionIndex, const CollectionConfig &c) {
    const QString uuid =
        CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
    if (uuid.isEmpty()) {
      return;
    }
    if (!maps.uuidToMediaDir.contains(uuid)) {
      maps.uuidToMediaDir[uuid] = c.mediaDirectory;
    }
    if (!maps.uuidToArtworkDir.contains(uuid)) {
      // Resolve artwork directory with parent fallback - if this collection
      // has no artwork directory, walk up the parent chain to find one
      QString resolvedArtwork = CollectionUtils::resolveArtworkDirectory(
          collectionIndex, allCollections);
      maps.uuidToArtworkDir[uuid] = resolvedArtwork;
    }
    if (!maps.uuidToCollectionIndex.contains(uuid)) {
      maps.uuidToCollectionIndex[uuid] = collectionIndex;
    }
  };

  if (ctx.queryIncludeAllCollections) {
    maps.uuidToMediaDir.reserve(allCollections.size());
    maps.uuidToArtworkDir.reserve(allCollections.size());
    maps.uuidToCollectionIndex.reserve(allCollections.size());
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig c = allCollections[i];
      c.mediaDirectory =
          PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      c.artworkDirectory =
          PathUtils::validateAndExpandPath(c.artworkDirectory, c.name);
      if (c.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      addMapping(i, c);
    }
    return maps;
  }

  QList<int> descendants;
  int expectedMappings = 1;
  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
    // Use pre-computed descendants if available (O(1) from cache), otherwise
    // fall back to O(n²) tree traversal for backward compatibility
    descendants = ctx.precomputedDescendants.isEmpty()
                      ? CollectionUtils::collectDescendantIndices(
                            ctx.currentIndex, allCollections)
                      : ctx.precomputedDescendants;
    expectedMappings += descendants.size();
  }

  maps.uuidToMediaDir.reserve(expectedMappings);
  maps.uuidToArtworkDir.reserve(expectedMappings);
  maps.uuidToCollectionIndex.reserve(expectedMappings);

  addMapping(ctx.currentIndex, ctx.config);

  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
    for (int descendantIndex : descendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }
      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory =
          PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      subCol.artworkDirectory = PathUtils::validateAndExpandPath(
          subCol.artworkDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      addMapping(descendantIndex, subCol);
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

bool QueryManager::ensureCollectionScanned(int collectionIndex,
                                           const CollectionConfig &collection) {
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return false;
  }

  // If the directory isn't present, don't emit scan-starting UI or attempt a
  // scan. This avoids rapid "scan" loops when a collection points to a missing
  // mount.
  if (!QFileInfo(collection.mediaDirectory).exists()) {
    return false;
  }

  if (!needsRescan(collectionIndex, collection)) {
    return false;
  }

  // Reset cancellation flag before starting new scan
  resetScanCancellation();

  // Compute UUID for completion signal
  const QString uuid = CollectionUtils::computeCollectionUuid(
      collection.name, collection.mediaDirectory);

  // Notify UI that a scan is starting (estimated items unknown, use -1)
  emit scanStarting(collection.name, -1);

  // Stream scan results directly into DB inserts to reduce peak memory.
  const bool success = scanAndSaveItemsToDatabase(collectionIndex, collection);

  // Always emit collectionScanCompleted when we emitted scanStarting, even if
  // the scan failed. This ensures MainWindow's m_activeScanCount is decremented
  // properly and the overlay is hidden. The caller can still check the return
  // value to know if the scan was successful.
  emit collectionScanCompleted(uuid);

  return success;
}

void QueryManager::insertItemsBatch(
    int legacyId, const QString &uuid, const QStringList &paths,
    const QHash<QString, QDateTime> &timestamps) {
  if (paths.isEmpty()) {
    return;
  }

  // Batch upsert for performance.
  // IMPORTANT: preserve user state fields
  // (play_count/last_played/rating/artwork_path). We only update name +
  // last_modified (and collection_id) when a row already exists.
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
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
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
  if (!q.exec("CREATE TEMP TABLE IF NOT EXISTS scanned_items ("
              "path TEXT PRIMARY KEY, "
              "name TEXT, "
              "last_modified TEXT"
              ")")) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
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

// ============================================================================
// Query UUIDs temp table - used when UUID count exceeds SQLite variable limit
// ============================================================================

bool QueryManager::ensureQueryUuidsTempTable() {
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  if (!q.exec("CREATE TEMP TABLE IF NOT EXISTS query_uuids (uuid TEXT PRIMARY "
              "KEY)")) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to create query_uuids temp table",
                              "QueryManager::ensureQueryUuidsTempTable")
            .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

void QueryManager::clearQueryUuidsTempTable() {
  if (!m_db.isOpen()) {
    return;
  }
  QSqlQuery q(m_db);
  q.exec("DELETE FROM query_uuids");
}

bool QueryManager::populateQueryUuidsTempTable(const QStringList &uuids) {
  if (!m_db.isOpen() || uuids.isEmpty()) {
    return false;
  }

  if (!ensureQueryUuidsTempTable()) {
    return false;
  }
  clearQueryUuidsTempTable();

  // Wrap in transaction for much faster batch inserts
  m_db.transaction();

  // Insert UUIDs in batches to stay under SQLite variable limit
  // Each row has 1 column, so batch size can be up to 999
  constexpr int BATCH_SIZE = 500;

  for (int batchStart = 0; batchStart < uuids.size();
       batchStart += BATCH_SIZE) {
    const int batchEnd = qMin(batchStart + BATCH_SIZE, uuids.size());
    const int batchCount = batchEnd - batchStart;

    QString sql = "INSERT OR IGNORE INTO query_uuids (uuid) VALUES ";
    QStringList placeholders;
    placeholders.reserve(batchCount);
    for (int i = 0; i < batchCount; ++i) {
      placeholders.append("(?)");
    }
    sql += placeholders.join(", ");

    QSqlQuery ins(m_db);
    ins.prepare(sql);
    for (int i = batchStart; i < batchEnd; ++i) {
      ins.addBindValue(uuids[i]);
    }

    if (!ins.exec()) {
      m_db.rollback();
      ErrorUtils::logError(
          ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                "Failed to populate query_uuids batch",
                                "QueryManager::populateQueryUuidsTempTable")
              .withDetails(ins.lastError().text()));
      return false;
    }
  }

  m_db.commit();
  return true;
}

QString QueryManager::buildUuidFilterClause(const QStringList &uuids,
                                            bool &useTempTable) {
  if (uuids.size() <= MAX_UUIDS_FOR_IN_CLAUSE) {
    // Small enough - use standard IN clause with placeholders
    useTempTable = false;
    return buildUuidInClause(uuids.size());
  }

  // Too many UUIDs - use temp table. Use EXISTS which is often faster than IN
  // for subqueries
  useTempTable = true;
  // For items table: collection_uuid; for items_fts: collection_uuid
  // The caller must use this in an EXISTS clause context
  return QStringLiteral(
      "(SELECT 1 FROM query_uuids WHERE query_uuids.uuid = collection_uuid)");
}

QByteArray QueryManager::computeUuidListHash(const QStringList &uuids) {
  // Fast hash of UUID list - concatenate sizes and first/last UUIDs
  // Full hash would be expensive for 3000+ UUIDs on every call
  QByteArray data;
  data.reserve(128);
  data.append(QByteArray::number(uuids.size()));
  if (!uuids.isEmpty()) {
    data.append(uuids.first().toUtf8());
    if (uuids.size() > 1) {
      data.append(uuids.last().toUtf8());
    }
    // Include a middle element for extra collision resistance
    if (uuids.size() > 2) {
      data.append(uuids[uuids.size() / 2].toUtf8());
    }
  }
  return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

bool QueryManager::ensureQueryUuidsPopulated(const QStringList &uuids) {
  const QByteArray newHash = computeUuidListHash(uuids);

  // Skip repopulation if hash matches (same UUIDs as last query)
  if (newHash == m_cachedQueryUuidsHash) {
    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qWarning() << "[RangeDiag] UUID temp table cache HIT, uuids="
                 << uuids.size();
    }
    return true;
  }

  QElapsedTimer timer;
  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    timer.start();
    qWarning() << "[RangeDiag] UUID temp table cache MISS, populating"
               << uuids.size() << "uuids...";
  }

  if (!populateQueryUuidsTempTable(uuids)) {
    return false;
  }

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qWarning() << "[RangeDiag] UUID temp table populated in" << timer.elapsed()
               << "ms";
  }

  m_cachedQueryUuidsHash = newHash;
  return true;
}

// ============================================================================
// Sorted Items Cache - precomputed sort order for O(1) range lookups
// ============================================================================
// For large collections (>10k items), ORDER BY + OFFSET is extremely slow for
// high offsets. Instead, we precompute the sorted order once into a temp table
// with sequential position numbers, then use position-based range queries.

bool QueryManager::ensureSortedItemsCacheTable() {
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  // position is the 0-based index in sorted order, used for instant BETWEEN
  // queries
  if (!q.exec("CREATE TEMP TABLE IF NOT EXISTS sorted_items_cache ("
              "position INTEGER PRIMARY KEY, "
              "path TEXT NOT NULL, "
              "uuid TEXT NOT NULL)")) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to create sorted_items_cache temp table",
                              "QueryManager::ensureSortedItemsCacheTable")
            .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

void QueryManager::clearSortedItemsCache() {
  m_sortedItemsCacheValid = false;
  m_sortedItemsCacheHash.clear();
  if (!m_db.isOpen()) {
    return;
  }
  QSqlQuery q(m_db);
  q.exec("DELETE FROM sorted_items_cache");
}

QByteArray QueryManager::computeSortCacheHash(const QStringList &uuids,
                                              const QString &filter,
                                              SortMode sortMode) {
  // Hash of UUIDs + filter + sortMode to detect when cache needs rebuilding
  QByteArray data;
  data.reserve(256);
  data.append(QByteArray::number(uuids.size()));
  if (!uuids.isEmpty()) {
    data.append(uuids.first().toUtf8());
    if (uuids.size() > 1) {
      data.append(uuids.last().toUtf8());
    }
    if (uuids.size() > 2) {
      data.append(uuids[uuids.size() / 2].toUtf8());
    }
  }
  data.append(filter.toUtf8());
  data.append(QByteArray::number(static_cast<int>(sortMode)));
  return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

void QueryManager::scheduleDeferredCacheBuild(const QStringList &uuids,
                                              const QString &filter,
                                              SortMode sortMode) {
  // Schedule cache build to run after current event processing completes.
  // This allows the slow-path query to return immediately while the cache
  // builds in the background. Subsequent queries will use the cache once ready.
  if (m_sortCacheBuildPending) {
    return; // Already scheduled
  }

  m_sortCacheBuildPending = true;
  m_pendingCacheUuids = uuids;
  m_pendingCacheFilter = filter;
  m_pendingCacheSortMode = sortMode;

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qWarning() << "[RangeDiag] Deferred cache build scheduled for"
               << uuids.size() << "uuids";
  }

  // Use queued invocation so this runs after the current function returns
  QMetaObject::invokeMethod(this, &QueryManager::performDeferredCacheBuild,
                            Qt::QueuedConnection);
}

void QueryManager::performDeferredCacheBuild() {
  if (!m_sortCacheBuildPending) {
    return;
  }

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qWarning() << "[RangeDiag] Starting deferred cache build...";
  }

  (void)populateSortedItemsCache(m_pendingCacheUuids, m_pendingCacheFilter,
                                 m_pendingCacheSortMode);

  m_sortCacheBuildPending = false;
  m_pendingCacheUuids.clear();
  m_pendingCacheFilter.clear();
}

bool QueryManager::populateSortedItemsCache(const QStringList &uuids,
                                            const QString &filter,
                                            SortMode sortMode) {
  if (!m_db.isOpen() || uuids.isEmpty()) {
    return false;
  }

  const QByteArray newHash = computeSortCacheHash(uuids, filter, sortMode);

  // Skip if cache is valid and hash matches
  if (m_sortedItemsCacheValid && newHash == m_sortedItemsCacheHash) {
    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qWarning() << "[RangeDiag] Sorted items cache HIT";
    }
    return true;
  }

  if (!ensureSortedItemsCacheTable()) {
    return false;
  }

  QElapsedTimer timer;
  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    timer.start();
    qWarning() << "[RangeDiag] Building sorted items cache for" << uuids.size()
               << "uuids, filter='" << filter
               << "', sortMode=" << static_cast<int>(sortMode);
  }

  m_db.transaction();

  // Clear existing cache
  {
    QSqlQuery clear(m_db);
    if (!clear.exec("DELETE FROM sorted_items_cache")) {
      m_db.rollback();
      return false;
    }
  }

  // Build the sorted result set once and insert with position numbers
  // This is the expensive operation, but we only do it once per collection load
  const QString trimmedFilter = filter.trimmed();

  bool useTempTable = uuids.size() > MAX_UUIDS_FOR_IN_CLAUSE;
  if (useTempTable && !ensureQueryUuidsPopulated(uuids)) {
    m_db.rollback();
    return false;
  }

  // For collection sorting, we need to join with collections table
  bool needsCollectionJoin = (sortMode == SortMode::CollectionAscending ||
                              sortMode == SortMode::CollectionDescending);

  QString sql;
  QString filterClause;
  if (!trimmedFilter.isEmpty()) {
    filterClause =
        needsCollectionJoin ? " AND i.name LIKE ?" : " AND name LIKE ?";
  }

  if (needsCollectionJoin) {
    // Join with collections to get collection name for sorting.
    // Use GROUP BY path to deduplicate paths that appear in multiple
    // collections (e.g., when showAllSubcollectionItems=true).
    // MIN(collection_uuid) picks one arbitrarily. This ensures cache size
    // matches COUNT(DISTINCT path).
    if (useTempTable) {
      sql = "SELECT i.path, MIN(i.collection_uuid) as collection_uuid FROM "
            "items i "
            "LEFT JOIN collections c ON i.collection_uuid = c.uuid "
            "WHERE EXISTS (SELECT 1 FROM query_uuids WHERE query_uuids.uuid = "
            "i.collection_uuid)" +
            filterClause + " GROUP BY i.path";
    } else {
      sql = "SELECT i.path, MIN(i.collection_uuid) as collection_uuid FROM "
            "items i "
            "LEFT JOIN collections c ON i.collection_uuid = c.uuid "
            "WHERE i.collection_uuid IN " +
            buildUuidInClause(uuids.size()) + filterClause + " GROUP BY i.path";
    }
  } else {
    // Use GROUP BY path to deduplicate - MIN picks one collection_uuid per
    // unique path
    if (useTempTable) {
      sql = "SELECT path, MIN(collection_uuid) as collection_uuid FROM items "
            "WHERE EXISTS (SELECT 1 FROM query_uuids WHERE query_uuids.uuid = "
            "collection_uuid)" +
            filterClause + " GROUP BY path";
    } else {
      sql = "SELECT path, MIN(collection_uuid) as collection_uuid FROM items "
            "WHERE collection_uuid IN " +
            buildUuidInClause(uuids.size()) + filterClause + " GROUP BY path";
    }
  }

  // Apply sort order based on sortMode
  // For name sorting, we can sort by path directly (same filename)
  // For collection sorting, we use MIN(c.name) since we're grouping
  // For random sorting, we skip ORDER BY and shuffle in memory
  const bool isRandomSort = (sortMode == SortMode::Random);
  if (!isRandomSort) {
    switch (sortMode) {
    case SortMode::NameDescending:
      sql += " ORDER BY path COLLATE NOCASE DESC";
      break;
    case SortMode::CollectionAscending:
      sql += " ORDER BY MIN(c.name) COLLATE NOCASE, path COLLATE NOCASE";
      break;
    case SortMode::CollectionDescending:
      sql += " ORDER BY MIN(c.name) COLLATE NOCASE DESC, path COLLATE NOCASE";
      break;
    default:
      sql += " ORDER BY path COLLATE NOCASE";
      break;
    }
  }
  // For random sort, no ORDER BY - we'll shuffle in memory after fetching

  QSqlQuery selectQuery(m_db);
  selectQuery.prepare(sql);

  int bindPos = 0;
  if (!useTempTable) {
    for (const QString &uuid : uuids) {
      selectQuery.bindValue(bindPos++, uuid);
    }
  }
  if (!trimmedFilter.isEmpty()) {
    selectQuery.bindValue(bindPos++, "%" + trimmedFilter + "%");
  }

  if (!selectQuery.exec()) {
    m_db.rollback();
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to fetch sorted items for cache",
                              "QueryManager::populateSortedItemsCache")
            .withDetails(selectQuery.lastError().text()));
    return false;
  }

  // For random sort: collect all items, shuffle, then insert
  // For other sorts: stream directly into cache (already ordered by SQL)
  struct PathUuidPair {
    QString path;
    QString uuid;
  };

  QVector<PathUuidPair> allItems;
  if (isRandomSort) {
    // Collect all items for shuffling
    while (selectQuery.next()) {
      allItems.append({selectQuery.value(0).toString(),
                       selectQuery.value(1).toString()});
    }

    // Fisher-Yates shuffle
    auto seed = static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch());
    std::mt19937 rng(seed);
    for (int i = allItems.size() - 1; i > 0; --i) {
      std::uniform_int_distribution<int> dist(0, i);
      int j = dist(rng);
      std::swap(allItems[i], allItems[j]);
    }

    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qWarning() << "[RangeDiag] Random sort: shuffled" << allItems.size()
                 << "items";
    }
  }

  // Insert in batches for efficiency
  constexpr int INSERT_BATCH_SIZE = 500;
  QStringList paths;
  QStringList pathUuids;
  paths.reserve(INSERT_BATCH_SIZE);
  pathUuids.reserve(INSERT_BATCH_SIZE);
  int position = 0;

  auto flushBatch = [&]() -> bool {
    if (paths.isEmpty())
      return true;

    QString insertSql =
        "INSERT INTO sorted_items_cache (position, path, uuid) VALUES ";
    QStringList placeholders;
    placeholders.reserve(paths.size());
    for (int i = 0; i < paths.size(); ++i) {
      placeholders.append("(?, ?, ?)");
    }
    insertSql += placeholders.join(", ");

    QSqlQuery ins(m_db);
    ins.prepare(insertSql);
    int startPos = position - paths.size();
    for (int i = 0; i < paths.size(); ++i) {
      ins.addBindValue(startPos + i);
      ins.addBindValue(paths[i]);
      ins.addBindValue(pathUuids[i]);
    }

    if (!ins.exec()) {
      ErrorUtils::logError(
          ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                "Failed to insert sorted items cache batch",
                                "QueryManager::populateSortedItemsCache")
              .withDetails(ins.lastError().text()));
      return false;
    }

    paths.clear();
    pathUuids.clear();
    return true;
  };

  if (isRandomSort) {
    // Insert from shuffled vector
    for (const auto &item : allItems) {
      paths.append(item.path);
      pathUuids.append(item.uuid);
      ++position;

      if (paths.size() >= INSERT_BATCH_SIZE) {
        if (!flushBatch()) {
          m_db.rollback();
          return false;
        }
      }
    }
  } else {
    // Stream from query result (already ordered)
    while (selectQuery.next()) {
      paths.append(selectQuery.value(0).toString());
      pathUuids.append(selectQuery.value(1).toString());
      ++position;

      if (paths.size() >= INSERT_BATCH_SIZE) {
        if (!flushBatch()) {
          m_db.rollback();
          return false;
        }
      }
    }
  }

  // Flush remaining
  if (!flushBatch()) {
    m_db.rollback();
    return false;
  }

  m_db.commit();

  m_sortedItemsCacheValid = true;
  m_sortedItemsCacheHash = newHash;

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qWarning() << "[RangeDiag] Sorted items cache built:" << position
               << "items in" << timer.elapsed() << "ms";
  }

  return true;
}

void QueryManager::insertScannedItemsBatch(
    const QStringList &paths, const QHash<QString, QDateTime> &timestamps) {
  if (!m_db.isOpen() || paths.isEmpty()) {
    return;
  }

  // 3 columns per row -> keep under SQLite 999 variable limit.
  QString sql = "INSERT OR REPLACE INTO scanned_items (path, name, "
                "last_modified) VALUES ";
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
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
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
  upsert.prepare("INSERT INTO items (collection_id, collection_uuid, path, "
                 "name, last_modified) "
                 "SELECT ?, ?, path, name, last_modified FROM scanned_items "
                 "ON CONFLICT(collection_uuid, path) DO UPDATE SET "
                 "collection_id=excluded.collection_id, "
                 "name=excluded.name, "
                 "last_modified=excluded.last_modified");
  upsert.addBindValue(legacyId);
  upsert.addBindValue(collectionUuid);
  if (!upsert.exec()) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
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
  q.prepare("DELETE FROM items WHERE collection_uuid = ? "
            "AND NOT EXISTS (SELECT 1 FROM scanned_items si WHERE si.path = "
            "items.path)");
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

auto QueryManager::prepareCollectionForItemsInsert(
    const CollectionConfig &collection, const QString &uuid,
    const QString &extSignature, int &legacyIdOut) -> bool {
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
      update.prepare(
          "UPDATE collections SET name=?, ext_signature=? WHERE uuid=?");
      update.addBindValue(collection.name);
      update.addBindValue(extSignature);
      update.addBindValue(uuid);
      update.exec();

      QSqlQuery check(m_db);
      check.prepare("SELECT COUNT(*) FROM collections WHERE uuid=?");
      check.addBindValue(uuid);
      bool exists =
          (check.exec() && check.next() && check.value(0).toInt() > 0);

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

bool QueryManager::scanAndSaveItemsToDatabase(
    int collectionIndex, const CollectionConfig &collection) {
  Q_UNUSED(collectionIndex)

  if (!m_db.isOpen()) {
    auto err =
        ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database is not open",
                            "QueryManager::scanAndSaveItemsToDatabase");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return false;
  }

  QDir dir(collection.mediaDirectory);
  if (!dir.exists()) {
    // Avoid treating this as a successful scan; otherwise the UI may refresh
    // and immediately retrigger scans.
    auto err = ErrorContext::warning(ErrorCode::MediaDirectoryNotFound,
                                     "Media directory does not exist",
                                     "QueryManager::scanAndSaveItemsToDatabase")
                   .withDetails(collection.mediaDirectory);
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return false;
  }

  // Include includeContentSubfolders in the signature to match needsRescan
  QString extSignature = collection.extensions.isEmpty()
                             ? QString()
                             : collection.extensions.join('|');
  extSignature += collection.includeContentSubfolders ? "|subfolders" : "";

  const QString uuid = CollectionUtils::computeCollectionUuid(
      collection.name, collection.mediaDirectory);

  // Temporarily disable synchronous writes for bulk insert performance
  QSqlQuery pragmaOff(m_db);
  if (!pragmaOff.exec("PRAGMA synchronous = OFF")) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to set synchronous=OFF for bulk insert",
                              "QueryManager::scanAndSaveItemsToDatabase")
            .withDetails(pragmaOff.lastError().text()));
  }
  const SynchronousPragmaGuard restoreSynchronous(m_db);

  // Cancellation-safe scans: stage into a TEMP table and only apply to the
  // persistent DB when the scan completes.
  if (!ensureScannedItemsTempTable()) {
    return false;
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
  qint64 lastProgressEmitMs =
      -UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS;

  auto maybeEmitScanProgress = [&](int processed, int total,
                                   bool force = false) {
    if (force) {
      emit scanItemsProgress(processed, total);
      lastProgressEmitMs = progressTimer.elapsed();
      return;
    }
    const qint64 nowMs = progressTimer.elapsed();
    if (nowMs - lastProgressEmitMs <
        UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS) {
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
      return false;
    }
    const std::atomic<bool> &cancelFlag = *cancelToken;

    const int maxThreads =
        m_scanThreadPool ? std::max(1, m_scanThreadPool->maxThreadCount()) : 1;
    const int maxInFlight = std::max(1, maxThreads * 2);

    ScanCompletionQueue queue;

    QVector<DirSignatureSample> signatureSamples;
    signatureSamples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
    {
      QFileInfo rootInfo(rootPath);
      addDirSignatureSample(
          signatureSamples,
          DirSignatureSample{QString(),
                             rootInfo.lastModified().toSecsSinceEpoch()},
          UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
    }

    auto enqueue = [&](const QString &dirPath) {
      if (cancelFlag.load(std::memory_order_acquire)) {
        return;
      }
      if (!m_scanThreadPool) {
        return;
      }
      {
        QMutexLocker locker(&queue.mutex);
        ++queue.inFlight;
      }
      m_scanThreadPool->start(new DirectoryScanTask(
          dirPath, rootPath, nameFilters, cancelToken, &queue));
    };

    // Always scan the root directory.
    enqueue(rootPath);

    QDirIterator dirIterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);

    int totalItemsScanned = 0;
    int lastReportedCount = 0;
    int directoriesEnqueued = 1; // root
    int directoryResultsConsumed = 0;

    while (!cancelFlag.load(std::memory_order_acquire)) {
      // Keep the number of outstanding tasks bounded.
      while (dirIterator.hasNext() &&
             !cancelFlag.load(std::memory_order_acquire)) {
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
          const qint64 mtimeSec =
              QFileInfo(dirPath).lastModified().toSecsSinceEpoch();
          addDirSignatureSample(
              signatureSamples, DirSignatureSample{relPath, mtimeSec},
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
          queue.hasSpace.wakeOne();
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
      qCDebug(lcQueryManager)
          << "Recursive scan+stream done"
          << "collectionIndex=" << collectionIndex << "cancelled="
          << (cancelFlag.load(std::memory_order_acquire) ? "yes" : "no")
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
      return false;
    }
  }

  if (itemsInserted > 0 && !isScanCancelled()) {
    maybeEmitScanProgress(itemsInserted, -1, true);
  }

  if (isScanCancelled()) {
    return false;
  }

  int legacyId = -1;
  if (!prepareCollectionForItemsInsert(collection, uuid, extSignature,
                                       legacyId)) {
    return false;
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
    return false;
  }

  // Indexing/apply phase: upsert staged results into the persistent items
  // table. We do this in batches so the UI can show a real "Indexing X of Y"
  // progress (totalItems > 0) instead of appearing stuck after scanning.
  qint64 totalToApply = 0;
  {
    QSqlQuery count(m_db);
    if (count.exec("SELECT COUNT(*) FROM scanned_items") && count.next()) {
      totalToApply = count.value(0).toLongLong();
    } else {
      auto err =
          ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                "Failed to count staged scan results",
                                "QueryManager::scanAndSaveItemsToDatabase")
              .withDetails(count.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      m_db.rollback();
      return false;
    }
  }

  // Force the overlay into "Indexing" mode (total known) immediately.
  maybeEmitScanProgress(0,
                        static_cast<int>(std::min<qint64>(
                            totalToApply, std::numeric_limits<int>::max())),
                        true);

  bool upsertOk = true;
  if (totalToApply > 0) {
    constexpr int APPLY_BATCH_SIZE =
        199; // 5 cols/row -> stays under SQLite 999 bind limit
    qint64 applied = 0;
    qint64 lastRowId = 0;

    while (applied < totalToApply) {
      if (isScanCancelled()) {
        upsertOk = false;
        break;
      }

      QSqlQuery sel(m_db);
      sel.prepare("SELECT rowid, path, name, last_modified FROM scanned_items "
                  "WHERE rowid > ? ORDER BY rowid LIMIT ?");
      sel.addBindValue(lastRowId);
      sel.addBindValue(APPLY_BATCH_SIZE);
      if (!sel.exec()) {
        auto err =
            ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                  "Failed to read staged scan results",
                                  "QueryManager::scanAndSaveItemsToDatabase")
                .withDetails(sel.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        upsertOk = false;
        break;
      }

      QStringList paths;
      QStringList names;
      QStringList lastModified;
      paths.reserve(APPLY_BATCH_SIZE);
      names.reserve(APPLY_BATCH_SIZE);
      lastModified.reserve(APPLY_BATCH_SIZE);

      qint64 batchMaxRowId = lastRowId;
      while (sel.next()) {
        const qint64 rowId = sel.value(0).toLongLong();
        batchMaxRowId = std::max(batchMaxRowId, rowId);
        paths.append(sel.value(1).toString());
        names.append(sel.value(2).toString());
        lastModified.append(sel.value(3).toString());
      }

      if (paths.isEmpty()) {
        break;
      }

      QString sql = "INSERT INTO items (collection_id, collection_uuid, path, "
                    "name, last_modified) VALUES ";
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
      for (int i = 0; i < paths.size(); ++i) {
        ins.addBindValue(legacyId);
        ins.addBindValue(uuid);
        ins.addBindValue(paths[i]);
        ins.addBindValue(names[i]);
        ins.addBindValue(lastModified[i]);
      }
      if (!ins.exec()) {
        auto err =
            ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                  "Failed to apply staged scan results",
                                  "QueryManager::scanAndSaveItemsToDatabase")
                .withDetails(ins.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        upsertOk = false;
        break;
      }

      lastRowId = batchMaxRowId;
      applied += paths.size();

      const int clampedApplied = static_cast<int>(
          std::min<qint64>(applied, std::numeric_limits<int>::max()));
      const int clampedTotal = static_cast<int>(
          std::min<qint64>(totalToApply, std::numeric_limits<int>::max()));
      maybeEmitScanProgress(clampedApplied, clampedTotal);
    }

    // Force a final progress update so the overlay reaches 100% for indexing.
    const int clampedTotal = static_cast<int>(
        std::min<qint64>(totalToApply, std::numeric_limits<int>::max()));
    maybeEmitScanProgress(clampedTotal, clampedTotal, true);
  }

  const bool deleteOk = deleteMissingItemsByUuidUsingScannedItems(uuid);

  QSqlQuery &meta =
      getPreparedStatement("UPDATE collections SET last_scanned = ?, "
                           "dir_signature = ? WHERE uuid = ?");
  meta.bindValue(0, QDateTime::currentDateTime().toString(Qt::ISODate));
  meta.bindValue(1, dirSignature);
  meta.bindValue(2, uuid);
  const bool metaOk = meta.exec();

  bool committed = false;
  if (upsertOk && deleteOk && metaOk) {
    committed = m_db.commit();
    if (!committed) {
      auto err =
          ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                 "Failed to commit scan results",
                                 "QueryManager::scanAndSaveItemsToDatabase")
              .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      m_db.rollback();
    }
  } else {
    m_db.rollback();
  }

  return upsertOk && deleteOk && metaOk && committed;
}

int QueryManager::fetchItemCountImpl(
    const CollectionContext &context,
    const QList<CollectionConfig> &allCollections, const QString &filter) {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      return 0;
    }
  }

  // Ensure we see the latest data committed by the scan worker.
  // Without this, our connection can return stale counts from a cached
  // WAL snapshot, causing the UI to show old item counts after scans.
  refreshWalView();

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context",
                                   "QueryManager::fetchItemCount");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return 0;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);

  // IMPORTANT: Do not scan synchronously here.
  // Scans are dispatched to a dedicated scan worker so this query worker can
  // return counts immediately and keep the UI responsive.

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  if (uuids.isEmpty()) {
    auto err =
        ErrorContext::warning(ErrorCode::InvalidArgument,
                              "No valid collection UUIDs for item count query",
                              "QueryManager::fetchItemCount");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return 0;
  }
  const QString trimmedFilter = filter.trimmed();

  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][QueryManager] fetchItemCount: uuidCount="
               << uuids.size() << "showAllSubcollectionItems="
               << ctx.config.showAllSubcollectionItems
               << "queryIncludeDescendants=" << ctx.queryIncludeDescendants;
  }

  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][QueryManager] fetchItemCount: collIndex="
               << context.currentIndex << "filter='" << trimmedFilter
               << "' includeSubfolders="
               << context.config.includeContentSubfolders
               << " showAllSubfolderItems="
               << context.config.showAllSubfolderItems << " currentSubfolder='"
               << context.config.currentSubfolder << "'";
  }
  if (m_itemsFtsAvailable && !m_itemsFtsReady) {
    if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
      qWarning() << "[SearchDiag][QueryManager] fetchItemCount: checking FTS "
                    "readiness from DB...";
    }
    m_itemsFtsReady = isItemsFtsReadyFromDb();
    if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
      qWarning() << "[SearchDiag][QueryManager] fetchItemCount: FTS ready ="
                 << m_itemsFtsReady;
    }
  }
  const QString ftsQuery =
      (m_itemsFtsAvailable && m_itemsFtsReady && !trimmedFilter.isEmpty())
          ? buildFtsPrefixQuery(trimmedFilter)
          : QString();
  const bool useFts = !ftsQuery.isEmpty();

  // Check if we need to use temp table for large UUID lists
  // SQLite has a default limit of 999 bind variables
  bool useTempTable = false;
  const QString uuidClause = buildUuidFilterClause(uuids, useTempTable);

  if (useTempTable) {
    if (!ensureQueryUuidsPopulated(uuids)) {
      auto err = ErrorContext::warning(
          ErrorCode::DatabaseQueryFailed,
          "Failed to populate UUID temp table for item count query",
          "QueryManager::fetchItemCount");
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return 0;
    }
  }

  QString sql;
  if (useFts) {
    // Query FTS table directly - the FTS table contains name,
    // path, and collection_uuid so we can filter directly.
    if (useTempTable) {
      sql = "SELECT COUNT(*) FROM items_fts "
            "WHERE items_fts MATCH ? AND EXISTS " +
            uuidClause;
    } else {
      sql = "SELECT COUNT(*) FROM items_fts "
            "WHERE items_fts MATCH ? AND collection_uuid IN " +
            uuidClause;
    }
  } else {
    if (useTempTable) {
      sql = "SELECT COUNT(DISTINCT path) FROM items WHERE EXISTS " + uuidClause;
    } else {
      sql = "SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid IN " +
            uuidClause;
    }
  }

  // Apply subfolder filtering when browsing subfolders
  const QString &subfolder = ctx.config.currentSubfolder;
  if (!subfolder.isEmpty()) {
    // In a subfolder - show only items whose path starts with subfolder/
    sql += " AND path LIKE ?";
  } else if (ctx.config.includeContentSubfolders &&
             !ctx.config.showAllSubfolderItems && trimmedFilter.isEmpty()) {
    // At root with subfolders enabled but NOT showing all items, we normally
    // exclude items in subfolders so the UI can present folder tiles.
    //
    // When a search filter is active, include subfolder items so search can
    // find matches even in \"virtual folders only\" collections.
    sql += " AND path NOT LIKE '%/%'";
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown
  // mixed together

  if (!trimmedFilter.isEmpty()) {
    if (!useFts) {
      sql += " AND name LIKE ?";
    }
  }

  // Use cached prepared statement - dynamic SQL is cached by query string
  QSqlQuery &query = getPreparedStatement(sql);
  int bindPos = 0;

  // Bind FTS MATCH first (it appears first in the FTS SQL)
  if (useFts) {
    query.bindValue(bindPos++, ftsQuery);
  }
  // Then bind collection UUIDs (only when not using temp table)
  if (!useTempTable) {
    for (const QString &uuid : uuids) {
      query.bindValue(bindPos++, uuid);
    }
  }
  if (!subfolder.isEmpty()) {
    query.bindValue(bindPos++, subfolder + "/%");
  }
  if (!trimmedFilter.isEmpty() && !useFts) {
    query.bindValue(bindPos++, "%" + trimmedFilter + "%");
  }

  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning()
        << "[SearchDiag][QueryManager] fetchItemCount: executing SQL, useFts="
        << useFts << "useTempTable=" << useTempTable
        << "uuidCount=" << uuids.size();
  }
  if (query.exec() && query.next()) {
    const int count = query.value(0).toInt();
    if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
      qWarning() << "[SearchDiag][QueryManager] fetchItemCount: result="
                 << count;
    }

    // For large collections, schedule deferred cache build for O(1) range
    // lookups. This avoids expensive ORDER BY + OFFSET on every fetchItemsRange
    // call. We don't block here - the cache builds after this function returns,
    // so the UI can start displaying items immediately using the slow path.
    //
    // Random sort mode ALWAYS requires a cache because SQL ORDER BY RANDOM()
    // with OFFSET cannot provide consistent results across paginated requests.
    const bool isRandomSort = (ctx.sortMode == SortMode::Random);
    if (isRandomSort ||
        count >= UIConstants::Database::PRECOMPUTE_SORT_THRESHOLD) {
      if (!hasSortedItemsCache() && !m_sortCacheBuildPending) {
        if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
          qWarning() << "[RangeDiag] fetchItemCount: count=" << count
                     << (isRandomSort ? " (random mode)" : " >= threshold")
                     << ", scheduling deferred cache build...";
        }
        scheduleDeferredCacheBuild(uuids, trimmedFilter, ctx.sortMode);
      }
    } else {
      // Small collection with non-random sort - clear any stale cache and use
      // standard ORDER BY
      if (m_sortedItemsCacheValid) {
        clearSortedItemsCache();
      }
    }

    return count;
  }

  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][QueryManager] fetchItemCount: query FAILED"
               << query.lastError().text();
  }
  auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                   "Fetch item count failed",
                                   "QueryManager::fetchItemCount")
                 .withDetails(query.lastError().text());
  ErrorUtils::logError(err);
  emit errorOccurred(err);
  return 0;
}

void QueryManager::fetchItemCount(const CollectionContext &context,
                                  const QList<CollectionConfig> &allCollections,
                                  const QString &filter) {
  emit itemCountLoaded(fetchItemCountImpl(context, allCollections, filter));
}

void QueryManager::fetchItemCountWithToken(
    const CollectionContext &context,
    const QList<CollectionConfig> &allCollections, const QString &filter,
    int requestToken) {
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning()
        << "[SearchDiag][QueryManager] fetchItemCountWithToken: ENTRY token="
        << requestToken << "filter='" << filter << "'";
  }
  const int count = fetchItemCountImpl(context, allCollections, filter);
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning()
        << "[SearchDiag][QueryManager] fetchItemCountWithToken: EMIT token="
        << requestToken << "count=" << count;
  }
  emit itemCountLoadedWithToken(count, requestToken);
}

void QueryManager::ensureScannedForContext(
    const CollectionContext &context,
    const QList<CollectionConfig> &allCollections) {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      return;
    }
  }

  // Ensure we see the latest data committed by the query worker (which may have
  // just invalidated/cleared the collection cache). Without this, the scan
  // worker's WAL snapshot may be stale, causing needsRescan to return false
  // because it still sees old collection data that was just deleted.
  refreshWalView();

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

  // Scan current collection if needed. ensureCollectionScanned now handles
  // emitting both scanStarting and collectionScanCompleted signals internally,
  // ensuring proper overlay tracking even when scans fail.
  if (!ctx.config.mediaDirectory.trimmed().isEmpty()) {
    (void)ensureCollectionScanned(ctx.currentIndex, ctx.config);
  }

  // Scan all collections if the query scope requests it.
  if (ctx.queryIncludeAllCollections) {
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig col = allCollections[i];
      col.mediaDirectory =
          PathUtils::validateAndExpandPath(col.mediaDirectory, col.name);
      if (col.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      (void)ensureCollectionScanned(i, col);
    }
    return;
  }

  // Scan descendants if requested.
  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
    // Use pre-computed descendants if available (O(1) from cache), otherwise
    // fall back to O(n²) tree traversal for backward compatibility
    const QList<int> &rawDescendants =
        ctx.precomputedDescendants.isEmpty()
            ? CollectionUtils::collectDescendantIndices(ctx.currentIndex,
                                                        allCollections)
            : ctx.precomputedDescendants;
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

      (void)ensureCollectionScanned(descendantIndex, subCol);
    }
  }
}

void QueryManager::fetchItemsRange(
    const CollectionContext &context,
    const QList<CollectionConfig> &allCollections, int offset, int limit,
    const QString &filter) {
  if (!ensureDatabaseConnection()) {
    initDatabase();
    if (!m_db.isOpen()) {
      emit itemsRangeLoaded(offset, QStringList(), QHash<QString, QString>(),
                            QHash<QString, QString>(),
                            QHash<QString, QString>(), QHash<QString, int>());
      return;
    }
  }

  // Ensure we see the latest data committed by the scan worker.
  // This is critical after a rescan - without it, the range query may return
  // stale/empty results from a cached WAL snapshot.
  refreshWalView();

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
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory = PathUtils::validateAndExpandPath(
      ctx.config.artworkDirectory, ctx.config.name);

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  CollectionDirMaps dirMaps = buildDirectoryMaps(ctx, allCollections);

  const QString trimmedFilter = filter.trimmed();

  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][QueryManager] fetchItemsRange: collIndex="
               << context.currentIndex << "offset=" << offset
               << "limit=" << limit << "filter='" << trimmedFilter
               << "' includeSubfolders="
               << context.config.includeContentSubfolders
               << " showAllSubfolderItems="
               << context.config.showAllSubfolderItems << " currentSubfolder='"
               << context.config.currentSubfolder << "'";
  }

  QElapsedTimer rangeTimer;
  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    rangeTimer.start();
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // FAST PATH: Use precomputed sorted cache for O(1) range lookups
  // ═══════════════════════════════════════════════════════════════════════════
  // When the sorted cache is valid (built during fetchItemCount for large
  // collections), we can skip the expensive ORDER BY + OFFSET and just do:
  //   SELECT path, uuid FROM sorted_items_cache WHERE position BETWEEN ? AND ?
  // This is instant regardless of offset position.
  //
  // For cached startup paths that skip fetchItemCount, we build the cache
  // on-demand here when offset is high enough to benefit from it.

  // Schedule deferred cache build for large collections if not already built.
  // This handles the case where the app starts from cached viewport and
  // fetchItemCount was never called to build the cache. We don't block the
  // first query - instead we schedule the build to run after this query
  // returns, so subsequent queries can use the cache.
  //
  // EXCEPTION: Random sort mode requires a cache because SQL ORDER BY with
  // OFFSET cannot provide consistent random order across paginated requests.
  // Each page would get different random items. So for random mode, we build
  // the cache synchronously before proceeding.
  const bool isRandomSort = (ctx.sortMode == SortMode::Random);
  if (!hasSortedItemsCache() && !m_sortCacheBuildPending) {
    if (isRandomSort) {
      // Random mode: must build cache synchronously for consistent pagination
      if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
        qWarning() << "[RangeDiag] fetchItemsRange: Random sort requires cache, "
                      "building synchronously";
      }
      (void)populateSortedItemsCache(uuids, trimmedFilter, ctx.sortMode);
      // Fall through to use the cache we just built
    } else if (uuids.size() >= 10 &&
               offset >= UIConstants::Database::PRECOMPUTE_SORT_THRESHOLD) {
      if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
        qWarning() << "[RangeDiag] fetchItemsRange: scheduling deferred cache "
                      "build, offset="
                   << offset;
      }
      scheduleDeferredCacheBuild(uuids, trimmedFilter, ctx.sortMode);
      // Fall through to slow path for this query
    }
  }

  if (hasSortedItemsCache()) {
    // Verify cache hash still matches (in case filter or sortMode changed)
    const QByteArray currentHash =
        computeSortCacheHash(uuids, trimmedFilter, ctx.sortMode);
    if (currentHash == m_sortedItemsCacheHash) {
      if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
        qWarning() << "[RangeDiag] fetchItemsRange: using sorted cache, offset="
                   << offset << "limit=" << limit;
      }

      QStringList filePaths;
      QHash<QString, QString> fileNames;
      QHash<QString, QString> fileToArtworkDir;
      QHash<QString, QString> fileToMediaDir;
      QHash<QString, int> fileToCollectionIndex;

      // Simple position-based range query - O(1) regardless of offset
      QSqlQuery cacheQuery(m_db);
      cacheQuery.prepare("SELECT path, uuid FROM sorted_items_cache "
                         "WHERE position >= ? AND position < ? "
                         "ORDER BY position");
      cacheQuery.bindValue(0, offset);
      cacheQuery.bindValue(1, offset + limit);

      if (cacheQuery.exec()) {
        while (cacheQuery.next()) {
          QString relPath = cacheQuery.value(0).toString();
          QString uuid = cacheQuery.value(1).toString();
          QString mediaDir = dirMaps.uuidToMediaDir.value(uuid);
          QString artworkDir = dirMaps.uuidToArtworkDir.value(uuid);
          int collectionIndex = dirMaps.uuidToCollectionIndex.value(uuid, -1);

          QString fullPath;
          if (QDir::isAbsolutePath(relPath)) {
            fullPath = relPath;
          } else {
            fullPath = QDir(mediaDir).absoluteFilePath(relPath);
          }

          QString keyPath = canonicalKeyPath(fullPath, false, nullptr);
          filePaths.append(keyPath);
          fileNames[keyPath] =
              displayNameForBase(QFileInfo(keyPath).completeBaseName());
          if (!artworkDir.isEmpty()) {
            fileToArtworkDir[keyPath] = artworkDir;
          }
          if (!mediaDir.isEmpty()) {
            fileToMediaDir[keyPath] = mediaDir;
          }
          if (collectionIndex >= 0) {
            fileToCollectionIndex[keyPath] = collectionIndex;
          }
        }

        if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
          qWarning() << "[RangeDiag] fetchItemsRange (cached): totalMs="
                     << rangeTimer.elapsed()
                     << "resultCount=" << filePaths.size();
        }

        emit itemsRangeLoaded(offset, filePaths, fileNames, fileToArtworkDir,
                              fileToMediaDir, fileToCollectionIndex);
        return;
      } else {
        // Cache query failed - fall through to standard path
        if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
          qWarning() << "[RangeDiag] fetchItemsRange: cache query failed, "
                        "falling back:"
                     << cacheQuery.lastError().text();
        }
      }
    } else {
      // Hash mismatch - cache is stale (filter changed)
      if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
        qWarning() << "[RangeDiag] fetchItemsRange: cache hash mismatch, using "
                      "slow path";
      }
      clearSortedItemsCache();
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // SLOW PATH: Standard ORDER BY + OFFSET (fallback for small collections)
  // ═══════════════════════════════════════════════════════════════════════════

  if (m_itemsFtsAvailable && !m_itemsFtsReady) {
    m_itemsFtsReady = isItemsFtsReadyFromDb();
  }
  const QString ftsQuery =
      (m_itemsFtsAvailable && m_itemsFtsReady && !trimmedFilter.isEmpty())
          ? buildFtsPrefixQuery(trimmedFilter)
          : QString();
  const bool useFts = !ftsQuery.isEmpty();

  // Check if we need to use temp table for large UUID lists
  // SQLite has a default limit of 999 bind variables
  bool useTempTable = false;
  const QString uuidClause = buildUuidFilterClause(uuids, useTempTable);

  if (useTempTable) {
    if (!ensureQueryUuidsPopulated(uuids)) {
      auto err = ErrorContext::warning(
          ErrorCode::DatabaseQueryFailed,
          "Failed to populate UUID temp table for range query",
          "QueryManager::fetchItemsRange");
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      emit itemsRangeLoaded(offset, QStringList(), QHash<QString, QString>(),
                            QHash<QString, QString>(),
                            QHash<QString, QString>(), QHash<QString, int>());
      return;
    }
  }

  QString sql;
  if (useFts) {
    // Query FTS table directly - the FTS table contains name,
    // path, and collection_uuid so we can filter and sort directly.
    // Use GROUP BY path to deduplicate paths that appear in multiple
    // collections.
    if (useTempTable) {
      sql =
          "SELECT path, MIN(collection_uuid) as collection_uuid FROM items_fts "
          "WHERE items_fts MATCH ? AND EXISTS " +
          uuidClause;
    } else {
      sql =
          "SELECT path, MIN(collection_uuid) as collection_uuid FROM items_fts "
          "WHERE items_fts MATCH ? AND collection_uuid IN " +
          uuidClause;
    }
  } else {
    // Use GROUP BY path to deduplicate paths that appear in multiple
    // collections (e.g., when showAllSubcollectionItems=true).
    // MIN(collection_uuid) picks one arbitrarily. This ensures the result count
    // matches COUNT(DISTINCT path).
    if (useTempTable) {
      sql = "SELECT path, MIN(collection_uuid) as collection_uuid FROM items "
            "WHERE EXISTS " +
            uuidClause;
    } else {
      sql = "SELECT path, MIN(collection_uuid) as collection_uuid FROM items "
            "WHERE collection_uuid IN " +
            uuidClause;
    }
  }

  // Apply subfolder filtering when browsing subfolders
  const QString &subfolder = ctx.config.currentSubfolder;
  if (!subfolder.isEmpty()) {
    // In a subfolder - show only items whose path starts with subfolder/
    sql += " AND path LIKE ?";
  } else if (ctx.config.includeContentSubfolders &&
             !ctx.config.showAllSubfolderItems && trimmedFilter.isEmpty()) {
    // At root with subfolders enabled but NOT showing all items, we normally
    // exclude items in subfolders so the UI can present folder tiles.
    //
    // When a search filter is active, include subfolder items so search can
    // find matches even in "virtual folders only" collections.
    sql += " AND path NOT LIKE '%/%'";
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown
  // mixed together

  if (!trimmedFilter.isEmpty()) {
    if (!useFts) {
      sql += " AND name LIKE ?";
    }
  }

  // Add GROUP BY path to deduplicate paths (required since we use
  // MIN(collection_uuid))
  sql += " GROUP BY path";

  // Apply sort order based on sortMode
  // NOTE: Random sort should never reach the slow path - it's handled by
  // synchronous cache building above. If we get here for random, fall back to
  // alphabetical as a safeguard (items won't be truly random but at least
  // pagination will be consistent).
  switch (ctx.sortMode) {
  case SortMode::NameDescending:
    sql += " ORDER BY name COLLATE NOCASE DESC LIMIT ? OFFSET ?";
    break;
  case SortMode::CollectionAscending:
    // For collection sorting in slow path, we can't easily join, so fall back
    // to name sort The cache path handles this correctly with proper joins
    sql += " ORDER BY collection_uuid, name COLLATE NOCASE LIMIT ? OFFSET ?";
    break;
  case SortMode::CollectionDescending:
    sql +=
        " ORDER BY collection_uuid DESC, name COLLATE NOCASE LIMIT ? OFFSET ?";
    break;
  case SortMode::Random:
    // This shouldn't happen - Random mode forces cache creation above.
    // Fall back to alphabetical for consistent pagination.
    qWarning() << "[QueryManager] fetchItemsRange: Random sort reached slow "
                  "path unexpectedly";
    sql += " ORDER BY name COLLATE NOCASE LIMIT ? OFFSET ?";
    break;
  default:
    sql += " ORDER BY name COLLATE NOCASE LIMIT ? OFFSET ?";
    break;
  }

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qWarning() << "[RangeDiag] fetchItemsRange (slow path): offset=" << offset
               << "limit=" << limit << "uuids=" << uuids.size()
               << "useTempTable=" << useTempTable;
  }

  // Use cached prepared statement - dynamic SQL is cached by query string
  QSqlQuery &query = getPreparedStatement(sql);
  int bindPos = 0;

  // Bind FTS MATCH first (it appears first in the FTS SQL)
  if (useFts) {
    query.bindValue(bindPos++, ftsQuery);
  }
  // Then bind collection UUIDs (only when not using temp table)
  if (!useTempTable) {
    for (const QString &uuid : uuids) {
      query.bindValue(bindPos++, uuid);
    }
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
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  qint64 execMs = 0;
  if (query.exec()) {
    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      execMs = rangeTimer.elapsed();
    }
    while (query.next()) {
      QString relPath = query.value(0).toString();
      QString uuid = query.value(1).toString();
      QString mediaDir = dirMaps.uuidToMediaDir.value(uuid);
      QString artworkDir = dirMaps.uuidToArtworkDir.value(uuid);
      int collectionIndex = dirMaps.uuidToCollectionIndex.value(uuid, -1);

      QString fullPath;
      if (QDir::isAbsolutePath(relPath)) {
        fullPath = relPath;
      } else {
        fullPath = QDir(mediaDir).absoluteFilePath(relPath);
      }

      QString keyPath = canonicalKeyPath(fullPath, false, nullptr);
      filePaths.append(keyPath);
      fileNames[keyPath] =
          displayNameForBase(QFileInfo(keyPath).completeBaseName());
      if (!artworkDir.isEmpty()) {
        fileToArtworkDir[keyPath] = artworkDir;
      }
      if (!mediaDir.isEmpty()) {
        fileToMediaDir[keyPath] = mediaDir;
      }
      if (collectionIndex >= 0) {
        fileToCollectionIndex[keyPath] = collectionIndex;
      }
    }

    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qWarning() << "[RangeDiag] fetchItemsRange: execMs=" << execMs
                 << "totalMs=" << rangeTimer.elapsed()
                 << "resultCount=" << filePaths.size();
    }

    if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
      qWarning()
          << "[SearchDiag][QueryManager] fetchItemsRange: returned paths="
          << filePaths.size();
      if (!filePaths.isEmpty()) {
        qWarning() << "[SearchDiag][QueryManager] fetchItemsRange: firstPath="
                   << filePaths.first();
      }
    }
  } else {
    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qWarning() << "[RangeDiag] fetchItemsRange: QUERY FAILED after"
                 << rangeTimer.elapsed() << "ms:" << query.lastError().text();
    }
    auto err = ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                   "Fetch items range failed",
                                   "QueryManager::fetchItemsRange")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
    emit errorOccurred(err);
  }

  emit itemsRangeLoaded(offset, filePaths, fileNames, fileToArtworkDir,
                        fileToMediaDir, fileToCollectionIndex);
}

void QueryManager::fetchVisualIndexForPath(
    const CollectionContext &context,
    const QList<CollectionConfig> &allCollections, const QString &filePath) {
  if (filePath.isEmpty()) {
    emit visualIndexForPathLoaded(-1, filePath);
    return;
  }

  if (!ensureDatabaseConnection()) {
    emit visualIndexForPathLoaded(-1, filePath);
    return;
  }

  refreshWalView();

  if (!context.isValid()) {
    emit visualIndexForPathLoaded(-1, filePath);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory = PathUtils::validateAndExpandPath(
      ctx.config.artworkDirectory, ctx.config.name);

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  CollectionDirMaps dirMaps = buildDirectoryMaps(ctx, allCollections);

  // Try sorted cache first (fast path)
  if (hasSortedItemsCache()) {
    const QByteArray currentHash =
        computeSortCacheHash(uuids, QString(), ctx.sortMode);
    if (currentHash == m_sortedItemsCacheHash) {
      // Query sorted cache for this path
      // The cache stores relative paths, so we need to check both
      QSqlQuery cacheQuery(m_db);
      cacheQuery.prepare(
          "SELECT position FROM sorted_items_cache WHERE path = ?");

      // Convert to relative path if it's absolute
      for (auto it = dirMaps.uuidToMediaDir.begin();
           it != dirMaps.uuidToMediaDir.end(); ++it) {
        if (filePath.startsWith(it.value())) {
          QString candidate = filePath.mid(it.value().length());
          if (candidate.startsWith('/')) {
            candidate = candidate.mid(1);
          }
          // Try this relative path
          cacheQuery.bindValue(0, candidate);
          if (cacheQuery.exec() && cacheQuery.next()) {
            int position = cacheQuery.value(0).toInt();
            emit visualIndexForPathLoaded(position, filePath);
            return;
          }
        }
      }

      // Also try with the full canonical path
      cacheQuery.bindValue(0, filePath);
      if (cacheQuery.exec() && cacheQuery.next()) {
        int position = cacheQuery.value(0).toInt();
        emit visualIndexForPathLoaded(position, filePath);
        return;
      }
    }
  }

  // Slow path: query with ROW_NUMBER to find position
  // Build ORDER BY clause based on sort mode
  QString orderClause;
  switch (ctx.sortMode) {
  case SortMode::NameDescending:
    orderClause = "ORDER BY name COLLATE NOCASE DESC";
    break;
  case SortMode::CollectionAscending:
    orderClause = "ORDER BY collection_uuid, name COLLATE NOCASE";
    break;
  case SortMode::CollectionDescending:
    orderClause = "ORDER BY collection_uuid DESC, name COLLATE NOCASE";
    break;
  case SortMode::NameAscending:
  case SortMode::ArtworkFirst:
  case SortMode::ArtworkLast:
  case SortMode::Random:
  default:
    orderClause = "ORDER BY name COLLATE NOCASE";
    break;
  }

  // Build WHERE clause for UUIDs
  QString uuidPlaceholders;
  for (int i = 0; i < uuids.size(); ++i) {
    if (i > 0)
      uuidPlaceholders += ", ";
    uuidPlaceholders += "?";
  }

  // Use window function to get row number for matching path.
  // IMPORTANT: Use GROUP BY path to deduplicate paths that appear in multiple
  // collections (e.g., when showAllSubcollectionItems=true). SELECT DISTINCT
  // path, name doesn't work because the same path can have different
  // collection_uuid values. GROUP BY path ensures exactly one row per unique
  // path, matching COUNT(DISTINCT path) used by fetchItemCount.
  QString sql = QString("SELECT rn FROM ("
                        "  SELECT path, ROW_NUMBER() OVER (%1) - 1 as rn "
                        "  FROM (SELECT path, name FROM items WHERE "
                        "collection_uuid IN (%2) GROUP BY path)"
                        ") WHERE path = ?")
                    .arg(orderClause, uuidPlaceholders);

  QSqlQuery query(m_db);
  query.prepare(sql);

  int bindPos = 0;
  for (const QString &uuid : uuids) {
    query.bindValue(bindPos++, uuid);
  }

  // Convert full path to relative for database lookup
  QString relPath;
  for (auto it = dirMaps.uuidToMediaDir.begin();
       it != dirMaps.uuidToMediaDir.end(); ++it) {
    if (filePath.startsWith(it.value())) {
      relPath = filePath.mid(it.value().length());
      if (relPath.startsWith('/')) {
        relPath = relPath.mid(1);
      }
      break;
    }
  }

  if (relPath.isEmpty()) {
    // Try the full path as-is
    query.bindValue(bindPos++, filePath);
  } else {
    query.bindValue(bindPos++, relPath);
  }

  // Debug: count how many distinct paths the subquery produces (should match
  // fetchItemCount)
  QString countSql = QString("SELECT COUNT(DISTINCT path) FROM items WHERE "
                             "collection_uuid IN (%1)")
                         .arg(uuidPlaceholders);
  QSqlQuery countQuery(m_db);
  countQuery.prepare(countSql);
  int countBindPos = 0;
  for (const QString &uuid : uuids) {
    countQuery.bindValue(countBindPos++, uuid);
  }
  int rowCount = -1;
  if (countQuery.exec() && countQuery.next()) {
    rowCount = countQuery.value(0).toInt();
  }

  QString boundPath = relPath.isEmpty() ? filePath : relPath;
  qWarning() << "[SelectionRestore] fetchVisualIndexForPath:"
             << "rowCount in subquery:" << rowCount << "uuids:" << uuids.size()
             << "filePath:" << filePath << "relPath:" << relPath
             << "boundPath:" << boundPath;

  if (query.exec() && query.next()) {
    int position = query.value(0).toInt();
    qWarning() << "[SelectionRestore] Query returned position:" << position;
    emit visualIndexForPathLoaded(position, filePath);
  } else {
    qWarning() << "[SelectionRestore] Query returned NO MATCH, lastError:"
               << query.lastError().text();
    emit visualIndexForPathLoaded(-1, filePath);
  }
}

// ... Helper implementations ...

// SQL constants for prepared statement caching
namespace QuerySQL {
constexpr const char *COLLECTION_INFO =
    "SELECT last_scanned, name, ext_signature, dir_signature FROM collections "
    "WHERE uuid = ?";
constexpr const char *ITEM_PATH_CHECK =
    "SELECT path FROM items WHERE collection_uuid = ? LIMIT 1";
constexpr const char *ITEMS_MODIFIED_COUNT =
    "SELECT COUNT(*) FROM items WHERE collection_uuid = ? AND last_modified > "
    "?";
constexpr const char *ITEMS_COUNT_BY_UUID =
    "SELECT COUNT(*) FROM items WHERE collection_uuid = ?";
constexpr const char *DELETE_ITEMS_BY_UUID =
    "DELETE FROM items WHERE collection_uuid = ?";
constexpr const char *DELETE_COLLECTION_BY_UUID =
    "DELETE FROM collections WHERE uuid = ?";
constexpr const char *LOAD_ITEMS_BY_UUID =
    "SELECT DISTINCT path FROM items WHERE collection_uuid = ? ORDER BY name "
    "COLLATE NOCASE";
constexpr const char *UPDATE_COLLECTION_SCAN_METADATA =
    "UPDATE collections SET last_scanned = ?, dir_signature = ? WHERE uuid = ?";
} // namespace QuerySQL

bool QueryManager::needsRescan(int collectionIndex,
                               const CollectionConfig &collection) {
  Q_UNUSED(collectionIndex)

  if (collection.mediaDirectory.trimmed().isEmpty()) {
    if (m_db.isOpen()) {
      const QString uuid = CollectionUtils::computeCollectionUuid(
          collection.name, collection.mediaDirectory);
      clearCollectionFromDatabaseByUuid(uuid);
    }
    return false;
  }

  // Include includeContentSubfolders in the signature - changing it requires
  // rescan
  QString currentSignature = collection.extensions.isEmpty()
                                 ? QString()
                                 : collection.extensions.join('|');
  currentSignature += collection.includeContentSubfolders ? "|subfolders" : "";

  const QString uuid = CollectionUtils::computeCollectionUuid(
      collection.name, collection.mediaDirectory);

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

  // Capture lastScanned before any early returns that might invalidate query
  // state
  QDateTime lastScanned =
      QDateTime::fromString(query.value(0).toString(), Qt::ISODate);

  if (storedName != collection.name) {
    return true;
  }

  // If an older DB is missing ext_signature metadata, don't force a full
  // rescan. Seed it from the current config when items already exist.
  if (storedSignature != currentSignature) {
    QSqlQuery &countQuery = getPreparedStatement(QuerySQL::ITEMS_COUNT_BY_UUID);
    countQuery.bindValue(0, uuid);
    const bool hasItems = (countQuery.exec() && countQuery.next() &&
                           countQuery.value(0).toInt() > 0);
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

  // When includeContentSubfolders is enabled, check for subdirectory
  // modifications. For large collections, this check is expensive (iterates all
  // subdirs). Skip deep check if collection has items in DB - trust cached data
  // on startup. Full validation happens when user navigates into subfolders or
  // forces refresh.
  if (collection.includeContentSubfolders) {
    // If we have items in the database, validate the stored directory signature
    // by checking a bounded set of sampled directories (cheap, avoids deep
    // scans).
    QSqlQuery &countQuery = getPreparedStatement(QuerySQL::ITEMS_COUNT_BY_UUID);
    countQuery.bindValue(0, uuid);
    const bool hasItems = (countQuery.exec() && countQuery.next() &&
                           countQuery.value(0).toInt() > 0);
    if (!hasItems) {
      return true;
    }

    if (!storedDirSignature.trimmed().isEmpty()) {
      if (!dirSignatureStillValid(collection.mediaDirectory, true,
                                  storedDirSignature)) {
        return true;
      }
    } else {
      // Older DBs may have no dir_signature. Avoid forcing a full rescan when
      // items already exist; seed a bounded signature from the filesystem.
      const QString seeded =
          seedDirSignatureFromFilesystem(collection.mediaDirectory, true);
      if (seeded.trimmed().isEmpty()) {
        return true;
      }

      // Preserve the existing last_scanned value while recording the signature.
      QSqlQuery &meta =
          getPreparedStatement(QuerySQL::UPDATE_COLLECTION_SCAN_METADATA);
      const QString lastScannedIso =
          lastScanned.isValid()
              ? lastScanned.toString(Qt::ISODate)
              : QDateTime::currentDateTime().toString(Qt::ISODate);
      meta.bindValue(0, lastScannedIso);
      meta.bindValue(1, seeded);
      meta.bindValue(2, uuid);
      (void)meta.exec();
    }
  } else {
    // Flat collections: directory mtime is a sufficient cheap proxy for
    // new/deleted files.
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

QStringList
QueryManager::scanMediaDirectory(const CollectionConfig &collection,
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
      *dirSignatureOut =
          seedDirSignatureFromFilesystem(dir.absolutePath(), false);
    }

    // Throttle scan progress emissions to avoid spamming the UI event loop.
    QElapsedTimer progressTimer;
    progressTimer.start();
    qint64 lastProgressEmitMs =
        -UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS;
    auto maybeEmitScanProgress = [&](int processed, int total,
                                     bool force = false) {
      if (force) {
        emit scanItemsProgress(processed, total);
        lastProgressEmitMs = progressTimer.elapsed();
        return;
      }
      const qint64 nowMs = progressTimer.elapsed();
      if (nowMs - lastProgressEmitMs <
          UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS) {
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

  const int maxThreads =
      m_scanThreadPool ? std::max(1, m_scanThreadPool->maxThreadCount()) : 1;
  const int maxInFlight = std::max(1, maxThreads * 2);
  ScanCompletionQueue queue;

  QVector<DirSignatureSample> signatureSamples;
  signatureSamples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
  {
    QFileInfo rootInfo(rootPath);
    addDirSignatureSample(
        signatureSamples,
        DirSignatureSample{QString(),
                           rootInfo.lastModified().toSecsSinceEpoch()},
        UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
  }

  auto enqueue = [&](const QString &dirPath) {
    if (cancelFlag.load(std::memory_order_acquire)) {
      return;
    }
    if (!m_scanThreadPool) {
      return;
    }
    {
      QMutexLocker locker(&queue.mutex);
      ++queue.inFlight;
    }
    m_scanThreadPool->start(new DirectoryScanTask(
        dirPath, rootPath, nameFilters, cancelToken, &queue));
  };

  // Always scan root.
  enqueue(rootPath);

  QDirIterator dirIterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);

  int totalItemsScanned = 0;
  constexpr int PROGRESS_REPORT_INTERVAL = 500;
  int lastReportedCount = 0;
  int directoriesEnqueued = 1; // root
  int directoryResultsConsumed = 0;

  // Throttle scan progress emissions to avoid spamming the UI event loop.
  QElapsedTimer progressTimer;
  progressTimer.start();
  qint64 lastProgressEmitMs =
      -UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS;
  auto maybeEmitScanProgress = [&](int processed, int total,
                                   bool force = false) {
    if (force) {
      emit scanItemsProgress(processed, total);
      lastProgressEmitMs = progressTimer.elapsed();
      return;
    }
    const qint64 nowMs = progressTimer.elapsed();
    if (nowMs - lastProgressEmitMs <
        UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS) {
      return;
    }
    emit scanItemsProgress(processed, total);
    lastProgressEmitMs = nowMs;
  };

  while (!cancelFlag.load(std::memory_order_acquire)) {
    // Fill in-flight queue.
    while (dirIterator.hasNext() &&
           !cancelFlag.load(std::memory_order_acquire)) {
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
        addDirSignatureSample(
            signatureSamples, DirSignatureSample{relPath, mtimeSec},
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
        queue.hasSpace.wakeOne();
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
    for (auto it = result.timestamps.constBegin();
         it != result.timestamps.constEnd(); ++it) {
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
    qCDebug(lcQueryManager)
        << "Recursive scan done"
        << "collection=" << collection.name << "cancelled="
        << (cancelFlag.load(std::memory_order_acquire) ? "yes" : "no")
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

QStringList
QueryManager::loadOrScanCollection(int collectionIndex,
                                   const CollectionConfig &collection,
                                   QHash<QString, QDateTime> &timestamps) {
  QStringList filePaths;
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return filePaths;
  }

  if (needsRescan(collectionIndex, collection)) {
    QString dirSignature;
    filePaths = scanMediaDirectory(collection, timestamps, &dirSignature);
    if (!filePaths.isEmpty()) {
      saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection,
                          dirSignature);
    }
  } else {
    const QString uuid = CollectionUtils::computeCollectionUuid(
        collection.name, collection.mediaDirectory);
    filePaths = loadItemsFromDatabaseByUuid(uuid);
  }

  return filePaths;
}

void QueryManager::saveItemsToDatabase(
    int collectionIndex, const QStringList &filePaths,
    const QHash<QString, QDateTime> &timestamps,
    const CollectionConfig &collection, const QString &dirSignature) {
  Q_UNUSED(collectionIndex)

  if (!m_db.isOpen()) {
    auto err =
        ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database is not open",
                            "QueryManager::saveItemsToDatabase");
    ErrorUtils::logError(err);
    return;
  }

  // Include includeContentSubfolders in the signature to match needsRescan
  QString extSignature = collection.extensions.isEmpty()
                             ? QString()
                             : collection.extensions.join('|');
  extSignature += collection.includeContentSubfolders ? "|subfolders" : "";

  const QString uuid = CollectionUtils::computeCollectionUuid(
      collection.name, collection.mediaDirectory);

  // Temporarily disable synchronous writes for bulk insert performance
  QSqlQuery pragmaOff(m_db);
  if (!pragmaOff.exec("PRAGMA synchronous = OFF")) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to set synchronous=OFF for bulk insert",
                              "QueryManager::saveItemsToDatabase")
            .withDetails(pragmaOff.lastError().text()));
  }
  const SynchronousPragmaGuard restoreSynchronous(m_db);

  // Batch insert for performance - SQLite handles up to 999 variables per
  // statement With 5 columns per row, we can insert 199 rows per batch (995
  // variables)
  constexpr int BATCH_SIZE = 199;
  // Commit every N batches to save incremental progress (~100K items per
  // commit)
  constexpr int COMMIT_INTERVAL_BATCHES = 500;
  constexpr int PROGRESS_REPORT_INTERVAL = 50000; // Report every 50K items

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
  int itemsInserted = 0;  // cppcheck-suppress unreadVariable - used for progress reporting

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
        auto err =
            ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
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
    if (!prepareCollectionForItemsInsert(collection, uuid, extSignature,
                                         legacyId)) {
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

    QSqlQuery &meta =
        getPreparedStatement(QuerySQL::UPDATE_COLLECTION_SCAN_METADATA);
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

QStringList
QueryManager::loadItemsFromDatabaseByUuid(const QString &collectionUuid) {
  QStringList filePaths;

  if (!m_db.isOpen()) {
    auto err = ErrorContext::error(ErrorCode::DatabaseNotOpen,
                                   "Database is not open, cannot load items",
                                   "QueryManager::loadItemsFromDatabaseByUuid");
    ErrorUtils::logError(err);
    return filePaths;
  }

  // Use cached prepared statement for loading items
  QSqlQuery &query = getPreparedStatement(QuerySQL::LOAD_ITEMS_BY_UUID);
  query.bindValue(0, collectionUuid);

  if (!query.exec()) {
    auto err = ErrorContext::error(ErrorCode::DatabaseQueryFailed,
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

  // Invalidate UUID temp table cache so next query repopulates
  m_cachedQueryUuidsHash.clear();

  // Invalidate sorted items cache - forces rebuild on next fetchItemCount
  clearSortedItemsCache();

  clearCollectionFromDatabaseByUuid(collectionUuid);
  emit cacheInvalidated(collectionUuid);
}

void QueryManager::clearCollectionFromDatabaseByUuid(
    const QString &collectionUuid) {
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
      continue; // Retry if can't start transaction
    }

    try {
      // Use cached prepared statement for deleting items
      QSqlQuery &query = getPreparedStatement(QuerySQL::DELETE_ITEMS_BY_UUID);
      query.bindValue(0, collectionUuid);
      if (!query.exec()) {
        throw std::runtime_error(query.lastError().text().toStdString());
      }

      // Use cached prepared statement for deleting collection
      QSqlQuery &delc =
          getPreparedStatement(QuerySQL::DELETE_COLLECTION_BY_UUID);
      delc.bindValue(0, collectionUuid);
      delc.exec();

      m_db.commit();
      return; // Success - exit retry loop
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
                             QHash<QString, QString> *canonicalPathCache)
    -> QString {
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
    const QString keyPath =
        canonicalKeyPath(absPath, dedup, canonicalPathCache);

    if (dedup) {
      if (effectiveSeenCanonicalPaths &&
          !effectiveSeenCanonicalPaths->contains(keyPath)) {
        effectiveSeenCanonicalPaths->insert(keyPath);
        allFilePaths.append(keyPath);
      }
    } else {
      allFilePaths.append(keyPath);
    }

    const int lastSeparator =
        std::max(file.lastIndexOf('/'), file.lastIndexOf('\\'));
    const QString fileName =
        (lastSeparator >= 0) ? file.mid(lastSeparator + 1) : file;
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
      return descending ? lhs.priority > rhs.priority
                        : lhs.priority < rhs.priority;
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
