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
// ScanCompletionQueue, dirSignature*) live in
// querymanagerhelpers.h::QueryManagerInternal — pulled in via the
// `using namespace` below. The connection-level free functions
// (maybeAbsolutizeItemPaths, clearCollectionFromDatabaseByUuid) are also
// declared there.
#include "scanservice.h"
#include "scanservice_internal.h"

#include "batchsizes.h"
#include "preparedstatementcache.h"

#include <algorithm>
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

#include "collection/collectioncontext.h"
#include "collection/hierarchyhelpers.h"
#include "collection/typehelpers.h"
#include "dbtxn.h"
#include "errorutils.h"
#include "extensionutils.h"
#include "pathutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "uiconstants/database.h"

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using namespace QueryManagerInternal;
using namespace ScanServiceInternal;

ScanService::ScanService(QSqlDatabase &db, PreparedStatementCache &cache, int &txnDepth,
                         QObject *parent)
    : QObject(parent), m_db(db), m_cache(cache), m_txnDepth(txnDepth) {}

void ScanService::requestCancelScan() {
  m_scanWork.requestCancel();
}

bool ScanService::isScanCancelled() const {
  return m_scanWork.isCancelled();
}

void ScanService::resetScanCancellation() {
  m_scanWork.reset();
}

bool ScanService::collectionHasItems(const QString &uuid) {
  QSqlQuery &countQuery = m_cache.get(QuerySQL::ITEMS_COUNT_BY_UUID);
  countQuery.bindValue(0, uuid);
  return countQuery.exec() && countQuery.next() && countQuery.value(0).toInt() > 0;
}

// The includeContentSubfolders arm of needsRescan: validate (or seed) the
// stored directory signature against the filesystem. Returns true when a full
// rescan is required, false to continue with the cheaper modified-items check.
bool ScanService::subfolderDirNeedsRescan(const QString &uuid, const CollectionConfig &collection,
                                          const QString &storedDirSignature,
                                          const QDateTime &lastScanned) {
  // If we have items in the database, validate the stored directory signature by
  // checking a bounded set of sampled directories (cheap, avoids deep scans).
  if (!collectionHasItems(uuid)) {
    return true;
  }

  if (!storedDirSignature.trimmed().isEmpty()) {
    return !dirSignatureStillValid(collection.mediaDirectory, true, storedDirSignature);
  }

  // Older DBs may have no dir_signature. Avoid forcing a full rescan when items
  // already exist; seed a bounded signature from the filesystem.
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
  return false;
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
    const bool hasItems = collectionHasItems(uuid);
    if (hasItems && storedSignature.trimmed().isEmpty()) {
      QSqlQuery &update = m_cache.get(QuerySQL::UPDATE_COLLECTION_EXT_SIGNATURE);
      // Positional binds — cached statements must never addBindValue (see
      // the PreparedStatementCache contract).
      update.bindValue(0, currentSignature);
      update.bindValue(1, uuid);
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

  // When includeContentSubfolders is enabled, validate the stored directory
  // signature (bounded sampling — see subfolderDirNeedsRescan). Otherwise the
  // directory mtime is a sufficient cheap proxy for new/deleted files.
  if (collection.folderBrowsing.includeContentSubfolders) {
    if (subfolderDirNeedsRescan(uuid, collection, storedDirSignature, lastScanned)) {
      return true;
    }
  } else {
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

QStringList ScanService::buildNameFilters(const CollectionConfig &collection) {
  // Glob composition is centralized in ExtensionUtils::toNameFilters — it
  // strips any "*."/"." prefix before prepending "*.", so a glob-form token
  // (the pre-Kartend-693zb canonical INI form) can never double-prefix into
  // the never-matching "*.*.ext".
  return ExtensionUtils::toNameFilters(collection.extensions);
}

QStringList ScanService::scanMediaDirectory(const CollectionConfig &collection,
                                            QHash<QString, QDateTime> &timestamps,
                                            QString *dirSignatureOut, bool *scanCompletedOut) {
  if (scanCompletedOut) {
    *scanCompletedOut = false;
  }
  QStringList filePaths;
  QDir dir(collection.mediaDirectory);

  if (!dir.exists()) {
    // Missing / unmounted directory: NOT a completed scan — an empty result
    // here must never be persisted as "the collection is empty"
    // (Kartend-fys4o).
    return filePaths;
  }

  if (dirSignatureOut) {
    *dirSignatureOut = QString();
  }

  const QStringList nameFilters = buildNameFilters(collection);

  // For non-recursive scans or small directories, use sequential scanning
  // Parallel scanning has overhead that only pays off with multiple directories
  if (!collection.folderBrowsing.includeContentSubfolders) {
    scanSequential(dir, nameFilters, timestamps, filePaths, dirSignatureOut);
  } else {
    scanParallel(dir, nameFilters, timestamps, filePaths, dirSignatureOut);
  }

  if (scanCompletedOut) {
    *scanCompletedOut = !isScanCancelled();
  }
  return filePaths;
}

void ScanService::scanSequential(const QDir &dir, const QStringList &nameFilters,
                                 QHash<QString, QDateTime> &timestamps, QStringList &filePaths,
                                 QString *dirSignatureOut) {
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
      return;
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
}

bool ScanService::walkDirectoriesParallel(
    const QDir &dir, const QStringList &nameFilters,
    const std::function<void(DirectoryScanResult &&)> &onResult, QString &dirSignatureOut) {
  // Scan directories in parallel with bounded in-flight tasks and consume
  // results as they complete to avoid head-of-line blocking. The per-result
  // consumption (in-memory accumulate vs. streaming into the batch stager) is
  // the caller's job, handed back through onResult.
  QElapsedTimer scanTimer;
  scanTimer.start();

  const QString rootPath = dir.absolutePath();
  const QDir rootDir(rootPath);
  const auto cancelToken = m_scanWork.token();
  if (!cancelToken) {
    return false;
  }
  const std::atomic<bool> &cancelFlag = *cancelToken;

  const int maxThreads = m_scanWork.maxThreadCount();
  const int maxInFlight = std::max(1, maxThreads * 2);
  // Heap-own the queue (shared_ptr) so a worker still finishing its final
  // QMutexLocker teardown can't touch a destroyed mutex after this frame
  // returns and reuses the stack (Kartend-bl8w0). `queue` aliases the owned
  // object so the accesses below read unchanged; each DirectoryScanTask
  // co-owns it by value, so the last finisher frees it.
  auto queuePtr = std::make_shared<ScanCompletionQueue>();
  ScanCompletionQueue &queue = *queuePtr;

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
    m_scanWork.start(new DirectoryScanTask(dirPath, rootPath, nameFilters, cancelToken, queuePtr));
  };

  // Always scan root.
  enqueue(rootPath);

  QDirIterator dirIterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);

  int directoriesEnqueued = 1; // root
  int directoryResultsConsumed = 0;

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
        // Reuse the once-built rootDir + the iterator's already-stat'd
        // QFileInfo rather than rebuilding QDir(rootPath) / QFileInfo(dirPath)
        // per directory — the two arms had drifted to different (equivalent
        // but redundantly-stat'ing) forms here (Kartend audit 1jwfk).
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
        // Acquire-load pairs with the worker's release-store in pushChunk so
        // TSan sees this chunk's payload as synchronized across the lossy
        // QWaitCondition epoch (ScanCompletionQueue::handoffSeq).
        (void)queue.handoffSeq.load(std::memory_order_acquire);
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
    onResult(std::move(result));
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
    qCDebug(lcQueryManager) << "Parallel directory walk done"
                            << "cancelled="
                            << (cancelFlag.load(std::memory_order_acquire) ? "yes" : "no")
                            << "dirsEnqueued=" << directoriesEnqueued
                            << "dirResults=" << directoryResultsConsumed
                            << "elapsedMs=" << scanTimer.elapsed();
  }

  dirSignatureOut = buildDirSignatureJson(true, signatureSamples);
  return true;
}

void ScanService::scanParallel(const QDir &dir, const QStringList &nameFilters,
                               QHash<QString, QDateTime> &timestamps, QStringList &filePaths,
                               QString *dirSignatureOut) {
  int totalItemsScanned = 0;
  // Override of the file-static PROGRESS_REPORT_INTERVAL = 50000 — parallel
  // recursive scans report on a 500-item delta cadence so the progress bar
  // stays live without the throttle eating every report.
  constexpr int RECURSIVE_PROGRESS_REPORT_INTERVAL = 500;
  int lastReportedCount = 0;

  // Throttle scan progress emissions to avoid spamming the UI event loop.
  ScanProgressThrottle throttle(UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS,
                                [this](int p, int t) { emit scanItemsProgress(p, t); });

  QString signature;
  const bool walked = walkDirectoriesParallel(
      dir, nameFilters,
      [&](DirectoryScanResult &&result) {
        if (result.relativePaths.isEmpty()) {
          return;
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
          throttle.report(totalItemsScanned, -1, false);
        }
      },
      signature);
  if (!walked) {
    return; // scan-work cancel token unavailable
  }

  // Check if cancelled during parallel scan.
  if (isScanCancelled()) {
    filePaths.clear();
    timestamps.clear();
    return;
  }

  if (dirSignatureOut) {
    *dirSignatureOut = signature;
  }

  // Emit final progress.
  if (totalItemsScanned > 0) {
    throttle.report(totalItemsScanned, -1, true);
  }
}

bool ScanService::stageScannedFile(QStringList &batchPaths,
                                   QHash<QString, QDateTime> &batchTimestamps,
                                   const QString &relativePath, const QDateTime &mtime,
                                   const std::function<bool()> &flushBatch) {
  batchPaths.append(relativePath);
  batchTimestamps[relativePath] = mtime;
  if (batchPaths.size() >= BATCH_SIZE) {
    return flushBatch();
  }
  return true;
}

// ============================================================================
// Phase 1 – stageFilesystemScan
// ============================================================================

// ── Non-recursive arm: stream files directly off a flat QDirIterator ───────
bool ScanService::stageFlatScan(const QDir &dir, const QStringList &nameFilters,
                                QStringList &batchPaths, QHash<QString, QDateTime> &batchTimestamps,
                                const std::function<bool()> &flushBatch,
                                const std::function<void(int, int)> &reportProgress,
                                QString &dirSignatureOut) {
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
    // See scanMediaDirectory above for why broken-symlink mtimes need an
    // epoch fallback (NOT NULL constraint).
    QDateTime mtime = info.lastModified();
    if (!mtime.isValid()) {
      mtime = QDateTime::fromSecsSinceEpoch(0);
    }

    ++scanned;
    if (scanned % SCAN_PROGRESS_INTERVAL == 0) {
      reportProgress(scanned, -1);
    }

    if (!stageScannedFile(batchPaths, batchTimestamps, relativePath, mtime, flushBatch)) {
      break;
    }
  }

  (void)flushBatch();
  return true;
}

// ── Recursive arm: parallel directory walk over the scan-work pool ─────────
bool ScanService::stageRecursiveScan(const QDir &dir, const QStringList &nameFilters,
                                     QStringList &batchPaths,
                                     QHash<QString, QDateTime> &batchTimestamps,
                                     const std::function<bool()> &flushBatch,
                                     const std::function<void(int, int)> &reportProgress,
                                     QString &dirSignatureOut) {
  int totalItemsScanned = 0;
  int lastReportedCount = 0;

  const bool walked = walkDirectoriesParallel(
      dir, nameFilters,
      [&](DirectoryScanResult &&result) {
        if (result.relativePaths.isEmpty()) {
          return;
        }
        for (const QString &p : result.relativePaths) {
          if (!stageScannedFile(batchPaths, batchTimestamps, p, result.timestamps.value(p),
                                flushBatch)) {
            break;
          }
        }

        totalItemsScanned += result.relativePaths.size();
        if (totalItemsScanned - lastReportedCount >= PROGRESS_REPORT_INTERVAL) {
          lastReportedCount = totalItemsScanned;
          reportProgress(totalItemsScanned, -1);
        }
      },
      dirSignatureOut);
  if (!walked) {
    return false; // scan-work cancel token unavailable (controller torn down)
  }

  (void)flushBatch();
  return true;
}

// Orchestrator: owns the batch + transaction state shared by both arms and
// the final-commit epilogue. The arms above only walk the filesystem and feed
// stageScannedFile/flushBatch.
bool ScanService::stageFilesystemScan(const CollectionConfig &collection,
                                      const QStringList &nameFilters, int &itemsStaged,
                                      QString &dirSignatureOut) {
  itemsStaged = 0;
  dirSignatureOut.clear();

  QDir dir(collection.mediaDirectory);

  // Throttle scan progress emissions to avoid spamming the UI event loop.
  ScanProgressThrottle throttle(UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS,
                                [this](int p, int t) { emit scanItemsProgress(p, t); });
  const std::function<void(int, int)> reportProgress = [&](int processed, int total) {
    throttle.report(processed, total, /*force=*/false);
  };

  // Streaming staging transaction (Kartend-l94tw: shared DbTransaction guard
  // on the QueryManager-owned depth counter instead of a hand-rolled
  // inTransaction flag). engaged = transaction open; commit()+reset() closes
  // an interval; reset() alone rolls back (cancellation); destruction at
  // function exit covers every early return.
  std::optional<KartendDb::DbTransaction> txn;
  const auto makeTxn = [&]() {
    txn.emplace(m_db, m_txnDepth, "ScanService::stageFilesystemScan",
                [this](const ErrorUtils::ErrorContext &e) { emit errorOccurred(e); });
  };
  // Distinguishes a staging-transaction failure from a cooperative cancel:
  // both make flushBatch return false (so the arms stop walking), but only a
  // failure may abort the whole scan. Without this, a swallowed mid-scan
  // BEGIN/COMMIT failure left the staging table PARTIAL while phase 2 still
  // ran deleteItemsMissingFromScan against it — silently pruning items that
  // exist on disk (Kartend-tlfgf).
  bool stagingFailed = false;
  int batchesSinceCommit = 0;

  QStringList batchPaths;
  batchPaths.reserve(BATCH_SIZE);
  QHash<QString, QDateTime> batchTimestamps;
  batchTimestamps.reserve(BATCH_SIZE * 2);

  const std::function<bool()> flushBatch = [&]() -> bool {
    if (stagingFailed) {
      // Failure is latched; don't re-attempt the batch (the arms call
      // flushBatch once more after their walk loop exits).
      return false;
    }
    if (batchPaths.isEmpty()) {
      return true;
    }
    if (isScanCancelled()) {
      txn.reset(); // rolls back any open staging transaction
      batchPaths.clear();
      batchTimestamps.clear();
      return false;
    }

    if (!txn) {
      makeTxn();
      if (!txn->activeOrReport("Failed to start transaction for streaming insert")) {
        txn.reset();
        stagingFailed = true;
        return false;
      }
      batchesSinceCommit = 0;
    }

    if (!m_scannedItems.insertBatch(batchPaths, batchTimestamps, collection.mediaDirectory)) {
      // A failed staging INSERT leaves the temp table partial; latch the
      // failure (already logged by insertBatch) so the staging transaction
      // rolls back via txn.reset() and phase 2's deleteItemsMissingFromScan is
      // skipped — otherwise unstaged-but-present items would be pruned
      // (Kartend-o1ed7). Don't credit itemsStaged for the dropped batch.
      txn.reset();
      stagingFailed = true;
      return false;
    }
    itemsStaged += batchPaths.size();
    ++batchesSinceCommit;

    batchPaths.clear();
    batchTimestamps.clear();

    if (batchesSinceCommit >= COMMIT_INTERVAL_BATCHES) {
      if (!txn->commitOrReport("Failed to commit streaming insert transaction")) {
        // Drop the guard so the final-commit epilogue can't re-attempt a
        // commit on the no-longer-open transaction and double-report
        // (Kartend-3ibqt — the quirk noted when this was extracted in
        // Kartend-in6cl). The dtor's rollback on the aborted transaction is
        // a harmless no-op at the SQLite level (see dbtxn.h).
        txn.reset();
        stagingFailed = true;
        return false;
      }
      txn.reset();
      reportProgress(itemsStaged, -1);
    }
    if (itemsStaged % PROGRESS_REPORT_INTERVAL == 0) {
      reportProgress(itemsStaged, -1);
    }

    return true;
  };

  if (!collection.folderBrowsing.includeContentSubfolders) {
    if (!stageFlatScan(dir, nameFilters, batchPaths, batchTimestamps, flushBatch, reportProgress,
                       dirSignatureOut)) {
      return false;
    }
  } else {
    if (!stageRecursiveScan(dir, nameFilters, batchPaths, batchTimestamps, flushBatch,
                            reportProgress, dirSignatureOut)) {
      return false;
    }
  }

  // A staging-transaction failure means the temp table holds only a partial
  // file list; phase 2 would treat every unstaged item as deleted. The error
  // was already logged + emitted by the guard's *OrReport — just abort.
  if (stagingFailed) {
    return false;
  }

  // Final commit for any remaining items in an open staging transaction.
  if (txn) {
    if (!txn->commitOrReport("Failed to commit final streaming insert transaction")) {
      return false; // guard dtor rolls back the aborted transaction (no-op)
    }
    txn.reset();
  }

  if (itemsStaged > 0 && !isScanCancelled()) {
    throttle.report(itemsStaged, -1, /*force=*/true);
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
  // The failure is deterministic (schema/IO/bind-limit) or has already
  // exhausted the apply transaction's bounded lock-contention retries
  // (Kartend-kt39d), and rolls back without persisting dir_signature, so
  // needsRescan() stays true. Re-running it on
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
  // retrying forever (see the m_failedScanUuids guard above). A user-cancelled
  // scan is exempt (Kartend-n0daq): it also returns false (the staging/apply
  // bails on isScanCancelled()), but it is not a deterministic failure —
  // poisoning the uuid would block rescanning the collection until restart.
  // The cancel token is still set here: it is only cleared by the
  // resetScanCancellation() at the top of the NEXT ensureCollectionScanned.
  if (!success && !isScanCancelled()) {
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
