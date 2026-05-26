// Collection-rescan subsystem extracted from QueryManager.
//
// This TU consolidates the scan code that previously lived across
// querymanagerscan.cpp, querymanagerscanandsave.cpp, querymanagerpersist.cpp
// (the scan-only parts) and querymanagerlifecycle.cpp (saveItemsToDatabase):
//
//   - requestCancelScan / isScanCancelled / resetScanCancellation
//   - needsRescan, scanMediaDirectory
//   - stageFilesystemScan / commitStagedScanResults / scanAndSaveItemsToDatabase
//     (the streaming scan+persist pipeline)
//   - saveItemsToDatabase (the in-memory save pipeline)
//   - prepareCollectionForItemsInsert, ensureCollectionScanned
//   - ensureScannedForContext
//
// ScanService borrows its QSqlDatabase + PreparedStatementCache by reference
// from the owning QueryManager (the ScannedItemsTable pattern); there is no
// QueryManager back-pointer. Scan-only free helpers (DirectoryScanTask,
// ScanCompletionQueue, SynchronousPragmaGuard, dirSignature*) live in
// querymanagerhelpers.h::QueryManagerInternal — pulled in via the
// `using namespace` below. The connection-level free functions
// (maybeAbsolutizeItemPaths, clearCollectionFromDatabaseByUuid) are also
// declared there.
#include "scanservice.h"

#include "batchsizes.h"
#include "preparedstatementcache.h"

#include <atomic>
#include <limits>
#include <memory>
#include <optional>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QMutex>
#include <QRunnable>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <QWaitCondition>
#include <stdexcept>

#include "collectionutils.h"
#include "errorutils.h"
#include "pathutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "uiconstants/database.h"

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using namespace QueryManagerInternal;

// ============================================================================
// Shared constants for all scan phases
// ============================================================================
namespace {
constexpr int BATCH_SIZE = KartendDb::BatchSizes::FilesystemScanBatch;
constexpr int COMMIT_INTERVAL_BATCHES = KartendDb::BatchSizes::ScanCommitInterval;
constexpr int PROGRESS_REPORT_INTERVAL = 50000;
constexpr int APPLY_BATCH_SIZE = KartendDb::BatchSizes::StagedScanApplyBatch;

// Kartend-a911.2: executes a prepared query and logs a DatabaseQueryFailed
// warning on failure. Returns nullopt on success, the constructed
// ErrorContext on failure (already logged) so callers that also need to
// emit errorOccurred or unwind control flow can do so without rebuilding
// the error. Centralises bind-count diagnostics + lastError forwarding
// across the scan pipeline.
[[nodiscard]] std::optional<ErrorContext>
execAndLog(QSqlQuery &query, const QString &failureMessage, const QString &callerLocation) {
  if (query.exec()) {
    return std::nullopt;
  }
  auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, failureMessage, callerLocation)
                 .withDetails(query.lastError().text());
  ErrorUtils::logError(err);
  return err;
}

// Kartend-7axx: the four scan/save pipelines all open-coded the same
// throttle pattern (lambda over a QElapsedTimer + a lastEmitMs cursor).
// Factor it once so adding a new pipeline doesn't grow a fifth copy.
class ScanProgressThrottle {
public:
  using Emitter = std::function<void(int processed, int total)>;

  ScanProgressThrottle(int minIntervalMs, Emitter emitter)
      : m_minIntervalMs(minIntervalMs), m_emitter(std::move(emitter)) {
    m_timer.start();
    // Initialise so the first non-force report always fires (legacy
    // behaviour from each pipeline's open-coded copy).
    m_lastEmitMs = -minIntervalMs;
  }

  /// Named `report`, not `emit`, because Qt's emit-keyword macro would
  /// expand to nothing and produce a syntax error at the call site.
  void report(int processed, int total, bool force = false) {
    if (force) {
      m_emitter(processed, total);
      m_lastEmitMs = m_timer.elapsed();
      return;
    }
    const qint64 nowMs = m_timer.elapsed();
    if (nowMs - m_lastEmitMs < m_minIntervalMs) return;
    m_emitter(processed, total);
    m_lastEmitMs = nowMs;
  }

private:
  QElapsedTimer m_timer;
  qint64 m_lastEmitMs = 0;
  int m_minIntervalMs;
  Emitter m_emitter;
};
} // namespace

ScanService::ScanService(QSqlDatabase &db, PreparedStatementCache &cache, QObject *parent)
    : QObject(parent), m_db(db), m_cache(cache) {}

void ScanService::requestCancelScan() {
  m_scanWork.requestCancel();
}

bool ScanService::isScanCancelled() const {
  return m_scanWork.isCancelled();
}

void ScanService::resetScanCancellation() {
  m_scanWork.reset();
}

bool ScanService::needsRescan(int collectionIndex, const CollectionConfig &collection) {
  Q_UNUSED(collectionIndex)

  if (collection.mediaDirectory.trimmed().isEmpty()) {
    if (m_db.isOpen()) {
      const QString uuid =
          CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);
      QueryManagerInternal::clearCollectionFromDatabaseByUuid(m_db, m_cache, uuid);
    }
    return false;
  }

  // Include includeContentSubfolders in the signature - changing it requires
  // rescan
  QString currentSignature =
      collection.extensions.isEmpty() ? QString() : collection.extensions.join('|');
  currentSignature += collection.folderBrowsing.includeContentSubfolders ? "|subfolders" : "";

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Use cached prepared statement for collection info lookup
  QSqlQuery &query = m_cache.get(QuerySQL::COLLECTION_INFO);
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
  QDateTime lastScanned = QDateTime::fromString(query.value(0).toString(), Qt::ISODate);

  if (storedName != collection.name) {
    return true;
  }

  // If an older DB is missing ext_signature metadata, don't force a full
  // rescan. Seed it from the current config when items already exist.
  if (storedSignature != currentSignature) {
    QSqlQuery &countQuery = m_cache.get(QuerySQL::ITEMS_COUNT_BY_UUID);
    countQuery.bindValue(0, uuid);
    const bool hasItems =
        (countQuery.exec() && countQuery.next() && countQuery.value(0).toInt() > 0);
    if (hasItems && storedSignature.trimmed().isEmpty()) {
      QSqlQuery &update = m_cache.get(QuerySQL::UPDATE_COLLECTION_EXT_SIGNATURE);
      update.addBindValue(currentSignature);
      update.addBindValue(uuid);
      (void)execAndLog(update, "Failed to backfill ext_signature", "ScanService::needsRescan");
    } else {
      return true;
    }
  }

  // Use cached prepared statement for item path check
  QSqlQuery &pathQuery = m_cache.get(QuerySQL::ITEM_PATH_CHECK);
  pathQuery.bindValue(0, uuid);

  if (pathQuery.exec() && pathQuery.next()) {
    QString storedPath = pathQuery.value(0).toString();
    QString storedFullPath = QDir(collection.mediaDirectory).absoluteFilePath(storedPath);
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
  if (collection.folderBrowsing.includeContentSubfolders) {
    // If we have items in the database, validate the stored directory signature
    // by checking a bounded set of sampled directories (cheap, avoids deep
    // scans).
    QSqlQuery &countQuery = m_cache.get(QuerySQL::ITEMS_COUNT_BY_UUID);
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
      QSqlQuery &meta = m_cache.get(QuerySQL::UPDATE_COLLECTION_SCAN_METADATA);
      const QString lastScannedIso = lastScanned.isValid()
                                         ? lastScanned.toString(Qt::ISODate)
                                         : QDateTime::currentDateTime().toString(Qt::ISODate);
      meta.bindValue(0, lastScannedIso);
      meta.bindValue(1, seeded);
      meta.bindValue(2, uuid);
      (void)execAndLog(meta, "Failed to seed dir_signature", "ScanService::needsRescan");
    }
  } else {
    // Flat collections: directory mtime is a sufficient cheap proxy for
    // new/deleted files.
    if (dirInfo.lastModified() > lastScanned) {
      return true;
    }
  }

  // Use cached prepared statement for modified items count
  QSqlQuery &newer = m_cache.get(QuerySQL::ITEMS_MODIFIED_COUNT);
  newer.bindValue(0, uuid);
  newer.bindValue(1, lastScanned.toString(Qt::ISODate));
  if (execAndLog(newer, "Failed to count modified items", "ScanService::needsRescan")) {
    return false;
  }

  return newer.next() && newer.value(0).toInt() > 0;
}

QStringList ScanService::scanMediaDirectory(const CollectionConfig &collection,
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
  if (!collection.folderBrowsing.includeContentSubfolders) {
    if (dirSignatureOut) {
      *dirSignatureOut = seedDirSignatureFromFilesystem(dir.absolutePath(), false);
    }

    // Throttle scan progress emissions to avoid spamming the UI event loop.
    ScanProgressThrottle throttle(UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS,
                                  [this](int p, int t) { emit scanItemsProgress(p, t); });
    auto maybeEmitScanProgress = [&](int processed, int total, bool force = false) {
      throttle.report(processed, total, force);
    };

    // Sequential scan for flat directories (original behavior). Per-loop
    // override of the file-static PROGRESS_REPORT_INTERVAL = 50000 — flat
    // dirs hit each report ~100× more often, so the tighter 500-item cadence
    // keeps the UI feedback live without flooding the event loop.
    constexpr int FLAT_PROGRESS_REPORT_INTERVAL = 500;
    int itemsScanned = 0;

    // QDir::System is required so symlinks whose targets are temporarily
    // unreachable (e.g. external/btrfs mount not ready at app start) are still
    // listed — without it Qt classifies them as Unknown and drops them, which
    // then causes deleteMissingItemsByUuidUsingScannedItems to prune their rows.
    QDirIterator iterator(dir.absolutePath(), nameFilters, QDir::Files | QDir::System,
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
      // QFileInfo::lastModified follows symlinks; broken/unreachable targets
      // return invalid, which round-trips through QDateTime::toString as a
      // null QString and trips the NOT NULL items.last_modified constraint.
      // Fall back to epoch so the row persists; the next scan against a
      // reachable target overwrites this via the upsert clause.
      QDateTime mtime = info.lastModified();
      if (!mtime.isValid()) {
        mtime = QDateTime::fromSecsSinceEpoch(0);
      }
      timestamps[relativePath] = mtime;

      ++itemsScanned;
      if (itemsScanned % FLAT_PROGRESS_REPORT_INTERVAL == 0) {
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
  const auto cancelToken = m_scanWork.token();
  if (!cancelToken) {
    return filePaths;
  }
  const std::atomic<bool> &cancelFlag = *cancelToken;

  const int maxThreads = m_scanWork.maxThreadCount();
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
    m_scanWork.start(new DirectoryScanTask(dirPath, rootPath, nameFilters, cancelToken, &queue));
  };

  // Always scan root.
  enqueue(rootPath);

  QDirIterator dirIterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);

  int totalItemsScanned = 0;
  // Per-loop override of the file-static PROGRESS_REPORT_INTERVAL = 50000 —
  // parallel recursive scans report on a 500-item delta cadence so the
  // progress bar stays live without the throttle eating every report.
  constexpr int RECURSIVE_PROGRESS_REPORT_INTERVAL = 500;
  int lastReportedCount = 0;
  int directoriesEnqueued = 1; // root
  int directoryResultsConsumed = 0;

  // Throttle scan progress emissions to avoid spamming the UI event loop.
  ScanProgressThrottle throttle(UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS,
                                [this](int p, int t) { emit scanItemsProgress(p, t); });
  auto maybeEmitScanProgress = [&](int processed, int total, bool force = false) {
    throttle.report(processed, total, force);
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
    for (auto it = result.timestamps.constBegin(); it != result.timestamps.constEnd(); ++it) {
      timestamps.insert(it.key(), it.value());
    }

    totalItemsScanned += result.relativePaths.size();
    if (totalItemsScanned - lastReportedCount >= RECURSIVE_PROGRESS_REPORT_INTERVAL) {
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

// ============================================================================
// Phase 1 – stageFilesystemScan
// ============================================================================
bool ScanService::stageFilesystemScan(const CollectionConfig &collection,
                                      const QStringList &nameFilters, int &itemsStaged,
                                      QString &dirSignatureOut) {
  itemsStaged = 0;
  dirSignatureOut.clear();

  QDir dir(collection.mediaDirectory);

  // Throttle scan progress emissions to avoid spamming the UI event loop.
  ScanProgressThrottle throttle(UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS,
                                [this](int p, int t) { emit scanItemsProgress(p, t); });
  auto maybeEmitScanProgress = [&](int processed, int total, bool force = false) {
    throttle.report(processed, total, force);
  };

  bool inTransaction = false;
  int batchesSinceCommit = 0;

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
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to start transaction for streaming insert",
                                          "ScanService::stageFilesystemScan")
                       .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        return false;
      }
      inTransaction = true;
      batchesSinceCommit = 0;
    }

    m_scannedItems.insertBatch(batchPaths, batchTimestamps, collection.mediaDirectory);
    itemsStaged += batchPaths.size();
    ++batchesSinceCommit;

    batchPaths.clear();
    batchTimestamps.clear();

    if (batchesSinceCommit >= COMMIT_INTERVAL_BATCHES) {
      if (!m_db.commit()) {
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to commit streaming insert transaction",
                                          "ScanService::stageFilesystemScan")
                       .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        m_db.rollback();
        return false;
      }
      inTransaction = false;
      maybeEmitScanProgress(itemsStaged, -1);
    }
    if (itemsStaged % PROGRESS_REPORT_INTERVAL == 0) {
      maybeEmitScanProgress(itemsStaged, -1);
    }

    return true;
  };

  // ── Non-recursive scan: stream files directly ──────────────────────────
  if (!collection.folderBrowsing.includeContentSubfolders) {
    dirSignatureOut = seedDirSignatureFromFilesystem(dir.absolutePath(), false);

    constexpr int SCAN_PROGRESS_INTERVAL = 500;
    int scanned = 0;

    // QDir::System keeps symlinks visible when their targets are temporarily
    // unreachable; see scanMediaDirectory above for context.
    QDirIterator iterator(dir.absolutePath(), nameFilters, QDir::Files | QDir::System,
                          QDirIterator::NoIteratorFlags);
    while (iterator.hasNext()) {
      if (isScanCancelled()) {
        break;
      }

      iterator.next();
      const QString relativePath = iterator.fileName();
      const QFileInfo info = iterator.fileInfo();
      batchPaths.append(relativePath);
      // See scanMediaDirectory above for why broken-symlink mtimes need an
      // epoch fallback (NOT NULL constraint).
      QDateTime mtime = info.lastModified();
      if (!mtime.isValid()) {
        mtime = QDateTime::fromSecsSinceEpoch(0);
      }
      batchTimestamps[relativePath] = mtime;

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
    // ── Recursive scan: parallel directory scanning ────────────────────
    QElapsedTimer scanTimer;
    scanTimer.start();

    const QString rootPath = dir.absolutePath();
    const auto cancelToken = m_scanWork.token();
    if (!cancelToken) {
      return false;
    }
    const std::atomic<bool> &cancelFlag = *cancelToken;

    const int maxThreads = m_scanWork.maxThreadCount();
    const int maxInFlight = std::max(1, maxThreads * 2);

    ScanCompletionQueue queue;

    QVector<DirSignatureSample> signatureSamples;
    signatureSamples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
    {
      QFileInfo rootInfo(rootPath);
      addDirSignatureSample(
          signatureSamples,
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
      m_scanWork.start(new DirectoryScanTask(dirPath, rootPath, nameFilters, cancelToken, &queue));
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
      qCDebug(lcQueryManager) << "Recursive scan+stream done"
                              << "cancelled="
                              << (cancelFlag.load(std::memory_order_acquire) ? "yes" : "no")
                              << "dirsEnqueued=" << directoriesEnqueued
                              << "dirResults=" << directoryResultsConsumed
                              << "filesFound=" << totalItemsScanned
                              << "elapsedMs=" << scanTimer.elapsed();
    }

    dirSignatureOut = buildDirSignatureJson(true, signatureSamples);

    (void)flushBatch();
  }

  // Final commit for any remaining items in an open staging transaction.
  if (inTransaction) {
    if (!m_db.commit()) {
      auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                        "Failed to commit final streaming insert transaction",
                                        "ScanService::stageFilesystemScan")
                     .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      m_db.rollback();
      return false;
    }
  }

  if (itemsStaged > 0 && !isScanCancelled()) {
    maybeEmitScanProgress(itemsStaged, -1, true);
  }

  return true;
}

bool ScanService::ensureCollectionScanned(int collectionIndex, const CollectionConfig &collection) {
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return false;
  }

  // If the directory isn't present, don't emit scan-starting UI or attempt a
  // scan. This avoids rapid "scan" loops when a collection points to a missing
  // mount.
  if (!QFileInfo(collection.mediaDirectory).exists()) {
    return false;
  }

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // A scan that already failed this session is not retried automatically.
  // The failure is deterministic (schema/IO/bind-limit) and rolls back without
  // persisting dir_signature, so needsRescan() stays true. Re-running it on
  // every collectionScanCompleted-driven reload spins an unbreakable
  // scan->fail->reload loop. Return without emitting scanStarting/
  // collectionScanCompleted: emitting them would re-trigger the reload and
  // keep the loop alive. The collection is left untouched until the next
  // session (a fresh QueryManager starts with an empty set) or until the
  // user changes the media directory (which yields a different UUID).
  if (m_failedScanUuids.contains(uuid)) {
    return false;
  }

  if (!needsRescan(collectionIndex, collection)) {
    return false;
  }

  // Reset cancellation flag before starting new scan
  resetScanCancellation();

  // Notify UI that a scan is starting (estimated items unknown, use -1)
  emit scanStarting(collection.name, -1);

  // Stream scan results directly into DB inserts to reduce peak memory.
  // capture scan stats so we can emit the summary signal used
  // by the settings dialog's "X of Y items added" confirmation.
  int itemsScanned = 0;
  int itemsApplied = 0;
  const bool success =
      scanAndSaveItemsToDatabase(collectionIndex, collection, &itemsScanned, &itemsApplied);

  // Remember a failed scan so the next reload-driven pass skips it instead of
  // retrying forever (see the m_failedScanUuids guard above).
  if (!success) {
    m_failedScanUuids.insert(uuid);
  }

  // Always emit collectionScanCompleted when we emitted scanStarting, even if
  // the scan failed. This ensures MainWindow's m_activeScanCount is decremented
  // properly and the overlay is hidden. The caller can still check the return
  // value to know if the scan was successful.
  emit collectionScanCompleted(uuid);
  emit collectionScanSummary(uuid, itemsScanned, itemsApplied, success);

  return success;
}

void ScanService::ensureScannedForContext(const CollectionContext &context,
                                          const QList<CollectionConfig> &allCollections) {
  // v13 path-convention reconcile: must run before any scan so the
  // absolute-vs-absolute join in deleteItemsMissingFromScan stays consistent.
  // The meta-flag gate makes this a cheap no-op once the reconcile (typically
  // done first in loadAllCollections) has completed.
  QueryManagerInternal::maybeAbsolutizeItemPaths(m_db, allCollections);

  if (!context.isValid()) {
    auto err =
        ErrorContext::error(ErrorCode::InvalidCollectionContext, "Invalid collection context",
                            "ScanService::ensureScannedForContext");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory =
      PathUtils::validateAndExpandPath(ctx.config.mediaDirectory, ctx.config.name);

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
      col.mediaDirectory = PathUtils::validateAndExpandPath(col.mediaDirectory, col.name);
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
            ? CollectionUtils::collectDescendantIndices(ctx.currentIndex, allCollections)
            : ctx.precomputedDescendants;
    for (int descendantIndex : rawDescendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }

      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }

      (void)ensureCollectionScanned(descendantIndex, subCol);
    }
  }
}
