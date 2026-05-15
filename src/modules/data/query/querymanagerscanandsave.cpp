// Scan-and-save pipeline for QueryManager (refactor).
//
// The monolithic scanAndSaveItemsToDatabase (~553 LOC) has been decomposed
// into two focused phases plus a thin orchestrator:
//
//   Phase 1 – stageFilesystemScan
//     Walks the filesystem (flat or recursive-parallel) and streams
//     discovered files into the scanned_items TEMP table. Returns the
//     number of files staged and the computed directory signature.
//
//   Phase 2 – commitStagedScanResults
//     Prepares the collection row (upsert collections), then upserts
//     scanned_items → items, deletes items no longer on disk, updates
//     last_scanned / dir_signature metadata, and commits.
//
//   Orchestrator – scanAndSaveItemsToDatabase
//     Validates preconditions, sets up PRAGMA / temp-table scaffolding,
//     calls Phase 1 → Phase 2 in sequence, and populates the
//     outItemsScanned / outItemsApplied counters for the caller.
//
// All three are private members of QueryManager; they share access to
// m_db, m_scanCancellationToken, m_scanThreadPool, and the helper
// functions in querymanagerhelpers.h.
#include "querymanager.h"

#include <atomic>
#include <memory>
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
#include <stdexcept>

#include "collectionutils.h"
#include "errorutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "uiconstants.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using namespace QueryManagerInternal;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)
#define debugLog(msg) qCDebug(lcQueryManager) << msg

// ============================================================================
// Shared constants for all scan phases
// ============================================================================
namespace {
constexpr int BATCH_SIZE = 199;
constexpr int COMMIT_INTERVAL_BATCHES = 500;
constexpr int PROGRESS_REPORT_INTERVAL = 50000;
constexpr int APPLY_BATCH_SIZE = 199; // 5 cols/row -> stays under SQLite 999 bind limit
} // namespace

// ============================================================================
// Phase 1 – stageFilesystemScan
// ============================================================================
bool QueryManager::stageFilesystemScan(const CollectionConfig &collection,
                                       const QStringList &nameFilters, int &itemsStaged,
                                       QString &dirSignatureOut) {
  itemsStaged = 0;
  dirSignatureOut.clear();

  QDir dir(collection.mediaDirectory);

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
                                          "QueryManager::stageFilesystemScan")
                       .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        return false;
      }
      inTransaction = true;
      batchesSinceCommit = 0;
    }

    insertScannedItemsBatch(batchPaths, batchTimestamps, collection.mediaDirectory);
    itemsStaged += batchPaths.size();
    ++batchesSinceCommit;

    batchPaths.clear();
    batchTimestamps.clear();

    if (batchesSinceCommit >= COMMIT_INTERVAL_BATCHES) {
      if (!m_db.commit()) {
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to commit streaming insert transaction",
                                          "QueryManager::stageFilesystemScan")
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
  if (!collection.includeContentSubfolders) {
    dirSignatureOut = seedDirSignatureFromFilesystem(dir.absolutePath(), false);

    constexpr int SCAN_PROGRESS_INTERVAL = 500;
    int scanned = 0;

    // QDir::System keeps symlinks visible when their targets are temporarily
    // unreachable; see querymanagerscan.cpp scanMediaDirectory for context.
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
      // See querymanagerscan.cpp scanMediaDirectory for why broken-symlink
      // mtimes need an epoch fallback (NOT NULL constraint).
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
                                        "QueryManager::stageFilesystemScan")
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

// ============================================================================
// Phase 2 – commitStagedScanResults
// ============================================================================
bool QueryManager::commitStagedScanResults(const CollectionConfig &collection, const QString &uuid,
                                           const QString &extSignature, const QString &dirSignature,
                                           int &itemsApplied) {
  itemsApplied = 0;

  // Throttle progress emissions during the apply phase.
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

  // Prepare collection row (upsert into collections table).
  int legacyId = -1;
  if (!prepareCollectionForItemsInsert(collection, uuid, extSignature, legacyId)) {
    return false;
  }

  // Begin the apply transaction.
  if (!m_db.transaction()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                      "Failed to start transaction to apply scan results",
                                      "QueryManager::commitStagedScanResults")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return false;
  }

  // Count staged rows so we can report "Indexing X of Y" progress.
  qint64 totalToApply = 0;
  {
    QSqlQuery count(m_db);
    if (count.exec("SELECT COUNT(*) FROM scanned_items") && count.next()) {
      totalToApply = count.value(0).toLongLong();
    } else {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to count staged scan results",
                                       "QueryManager::commitStagedScanResults")
                     .withDetails(count.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      m_db.rollback();
      return false;
    }
  }

  // Force the overlay into "Indexing" mode (total known) immediately.
  maybeEmitScanProgress(
      0, static_cast<int>(std::min<qint64>(totalToApply, std::numeric_limits<int>::max())), true);

  // Batched upsert: scanned_items → items.
  bool upsertOk = true;
  if (totalToApply > 0) {
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
        auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                         "Failed to read staged scan results",
                                         "QueryManager::commitStagedScanResults")
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

      // date_added is stamped at insert time and intentionally OMITTED
      // from the ON CONFLICT update clause — re-scanning a known item
      // must NOT reset its "date added to library" stamp. New rows
      // (cleared library, re-imported, or first-ever scan) get the
      // current epoch; existing rows keep whatever the v12 backfill or
      // an earlier scan recorded.
      const qint64 nowEpochSec = QDateTime::currentSecsSinceEpoch();

      QString sql = "INSERT INTO items (collection_id, collection_uuid, path, "
                    "name, last_modified, date_added) VALUES ";
      QStringList valueSets;
      valueSets.reserve(paths.size());
      for (int i = 0; i < paths.size(); ++i) {
        valueSets.append("(?, ?, ?, ?, ?, ?)");
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
        ins.addBindValue(nowEpochSec);
      }
      if (!ins.exec()) {
        auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                         "Failed to apply staged scan results",
                                         "QueryManager::commitStagedScanResults")
                       .withDetails(ins.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        upsertOk = false;
        break;
      }

      lastRowId = batchMaxRowId;
      applied += paths.size();

      const int clampedApplied =
          static_cast<int>(std::min<qint64>(applied, std::numeric_limits<int>::max()));
      const int clampedTotal =
          static_cast<int>(std::min<qint64>(totalToApply, std::numeric_limits<int>::max()));
      maybeEmitScanProgress(clampedApplied, clampedTotal);
    }

    // Force a final progress update so the overlay reaches 100% for indexing.
    const int clampedTotal =
        static_cast<int>(std::min<qint64>(totalToApply, std::numeric_limits<int>::max()));
    maybeEmitScanProgress(clampedTotal, clampedTotal, true);
  }

  // Delete items no longer on disk.
  const bool deleteOk = deleteMissingItemsByUuidUsingScannedItems(uuid);

  // Update collection metadata (last_scanned + dir_signature).
  QSqlQuery &meta = getPreparedStatement("UPDATE collections SET last_scanned = ?, "
                                         "dir_signature = ? WHERE uuid = ?");
  meta.bindValue(0, QDateTime::currentDateTime().toString(Qt::ISODate));
  meta.bindValue(1, dirSignature);
  meta.bindValue(2, uuid);
  const bool metaOk = meta.exec();

  // Commit or rollback the apply transaction.
  bool committed = false;
  if (upsertOk && deleteOk && metaOk) {
    committed = m_db.commit();
    if (!committed) {
      auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                        "Failed to commit scan results",
                                        "QueryManager::commitStagedScanResults")
                     .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      m_db.rollback();
    }
  } else {
    m_db.rollback();
  }

  const bool success = upsertOk && deleteOk && metaOk && committed;
  if (success) {
    itemsApplied =
        static_cast<int>(std::min<qint64>(totalToApply, std::numeric_limits<int>::max()));
  }
  return success;
}

// ============================================================================
// Orchestrator – scanAndSaveItemsToDatabase
// ============================================================================
bool QueryManager::scanAndSaveItemsToDatabase(int collectionIndex,
                                              const CollectionConfig &collection,
                                              int *outItemsScanned, int *outItemsApplied) {
  Q_UNUSED(collectionIndex)

  // populate summary counters for the caller even on early-return
  // error paths so the UI gets a consistent "0 of 0" report instead of stale
  // garbage when a scan can't run.
  if (outItemsScanned) {
    *outItemsScanned = 0;
  }
  if (outItemsApplied) {
    *outItemsApplied = 0;
  }

  if (!m_db.isOpen()) {
    auto err = ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database is not open",
                                   "QueryManager::scanAndSaveItemsToDatabase");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return false;
  }

  QDir dir(collection.mediaDirectory);
  if (!dir.exists()) {
    auto err =
        ErrorContext::warning(ErrorCode::MediaDirectoryNotFound, "Media directory does not exist",
                              "QueryManager::scanAndSaveItemsToDatabase")
            .withDetails(collection.mediaDirectory);
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return false;
  }

  // Include includeContentSubfolders in the signature to match needsRescan.
  QString extSignature =
      collection.extensions.isEmpty() ? QString() : collection.extensions.join('|');
  extSignature += collection.includeContentSubfolders ? "|subfolders" : "";

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Temporarily disable synchronous writes for bulk insert performance.
  QSqlQuery pragmaOff(m_db);
  if (!pragmaOff.exec("PRAGMA synchronous = OFF")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to set synchronous=OFF for bulk insert",
                                               "QueryManager::scanAndSaveItemsToDatabase")
                             .withDetails(pragmaOff.lastError().text()));
  }
  const SynchronousPragmaGuard restoreSynchronous(m_db);

  // Prepare the staging temp table.
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

  // Build name filters from extensions.
  QStringList nameFilters;
  if (!collection.extensions.isEmpty()) {
    for (const QString &ext : collection.extensions) {
      nameFilters << "*." + ext;
    }
  }

  // Phase 1: Walk filesystem and stage into temp table.
  int itemsStaged = 0;
  QString dirSignature;
  if (!stageFilesystemScan(collection, nameFilters, itemsStaged, dirSignature)) {
    return false;
  }

  if (isScanCancelled()) {
    return false;
  }

  // Phase 2: Apply staged results to persistent DB.
  int itemsApplied = 0;
  const bool success =
      commitStagedScanResults(collection, uuid, extSignature, dirSignature, itemsApplied);

  // surface scan stats to the caller.
  if (outItemsScanned) {
    *outItemsScanned = itemsStaged;
  }
  if (outItemsApplied) {
    *outItemsApplied = success ? itemsApplied : 0;
  }
  return success;
}
