// Item-persist cluster extracted from querymanager.cpp:
//   - ensureCollectionScanned, insertItemsBatch
//   - ensureScannedItemsTempTable, clearScannedItemsTempTable
//   - insertScannedItemsBatch, applyScannedItemsToDatabase
//   - deleteMissingItemsByUuidUsingScannedItems, prepareCollectionForItemsInsert
//   - scanAndSaveItemsToDatabase (~553 LOC, the main scan+persist driver)
// Members of QueryManager; access existing class state.
//
// Anonymous-namespace scan helpers (DirectoryScanTask, ScanCompletionQueue,
// SynchronousPragmaGuard, dirSignature*) are duplicated here matching the
// querymanagerlifecycle.cpp / querymanagerfetchrange.cpp pattern.
#include "querymanager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutex>
#include <QRegularExpression>
#include <QRunnable>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVector>
#include <QWaitCondition>
#include <atomic>
#include <memory>
#include <stdexcept>

#include "collectionutils.h"
#include "errorutils.h"
#include "querymanagersql.h"
#include "uiconstants.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)
#define debugLog(msg) qCDebug(lcQueryManager) << msg

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

