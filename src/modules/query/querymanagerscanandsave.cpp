// Item-persist cluster extracted from querymanager.cpp:
//   - ensureCollectionScanned, insertItemsBatch
//   - ensureScannedItemsTempTable, clearScannedItemsTempTable
//   - insertScannedItemsBatch, applyScannedItemsToDatabase
//   - deleteMissingItemsByUuidUsingScannedItems, prepareCollectionForItemsInsert
//   - scanAndSaveItemsToDatabase (~553 LOC, the main scan+persist driver)
// Members of QueryManager; access existing class state.
//
// Scan helpers (DirectoryScanTask, ScanCompletionQueue, SynchronousPragmaGuard,
// dirSignature*) live in querymanagerhelpers.h::QueryManagerInternal — pulled
// in via `using namespace QueryManagerInternal;` below.
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
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "uiconstants.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using namespace QueryManagerInternal;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)
#define debugLog(msg) qCDebug(lcQueryManager) << msg
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

