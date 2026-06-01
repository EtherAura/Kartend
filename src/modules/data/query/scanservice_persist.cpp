// Sibling TU of scanservice.cpp — persist-side of the scan pipeline.
//
// Phase 2 onward: commitStagedScanResults (apply the staged scan results to
// the database), scanAndSaveItemsToDatabase (the streaming scan+persist
// orchestrator), prepareCollectionForItemsInsert (per-collection setup
// before the per-item INSERT loop), saveItemsToDatabase (the in-memory
// save pipeline used by the non-streaming path).
//
// Scan-side (needsRescan / scanMediaDirectory / stageFilesystemScan) and
// the caller-facing entry points (ensureCollectionScanned /
// ensureScannedForContext) stay in scanservice.cpp.
//
// All remain ScanService members; this is a TU split to bring the parent
// out of the ~1500 LOC zone. Anon-namespace helpers shared across both
// sides live in querymanagerhelpers.h::QueryManagerInternal — pulled in
// via the `using namespace` below.

#include "scanservice.h"

#include "batchsizes.h"
#include "preparedstatementcache.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <stdexcept>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
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
// Shared constants + helpers — mirrored from scanservice.cpp's anonymous
// namespace because the persist-side functions below reference them. Each
// TU's anon namespace keeps its own copy (constexpr / internal-linkage);
// the alternative would be a tiny private header for these symbols. Keep
// the two copies in sync if the originals change.
// ============================================================================
namespace {
constexpr int BATCH_SIZE = KartendDb::BatchSizes::FilesystemScanBatch;
constexpr int COMMIT_INTERVAL_BATCHES = KartendDb::BatchSizes::ScanCommitInterval;
constexpr int PROGRESS_REPORT_INTERVAL = 50000;
constexpr int APPLY_BATCH_SIZE = KartendDb::BatchSizes::StagedScanApplyBatch;

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

class ScanProgressThrottle {
public:
  using Emitter = std::function<void(int processed, int total)>;

  ScanProgressThrottle(int minIntervalMs, Emitter emitter)
      : m_minIntervalMs(minIntervalMs), m_emitter(std::move(emitter)) {
    m_timer.start();
    m_lastEmitMs = -minIntervalMs;
  }

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

// ============================================================================
// Phase 2 – commitStagedScanResults
// ============================================================================
bool ScanService::commitStagedScanResults(const CollectionConfig &collection, const QString &uuid,
                                          const QString &extSignature, const QString &dirSignature,
                                          int &itemsApplied) {
  itemsApplied = 0;

  // Throttle progress emissions during the apply phase.
  ScanProgressThrottle throttle(UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS,
                                [this](int p, int t) { emit scanItemsProgress(p, t); });
  auto maybeEmitScanProgress = [&](int processed, int total, bool force = false) {
    throttle.report(processed, total, force);
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
                                      "ScanService::commitStagedScanResults")
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
                                       "ScanService::commitStagedScanResults")
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

      QSqlQuery &sel = m_cache.get(QuerySQL::SELECT_STAGED_SCAN_RESULTS);
      sel.addBindValue(lastRowId);
      sel.addBindValue(APPLY_BATCH_SIZE);
      if (auto err = execAndLog(sel, "Failed to read staged scan results",
                                "ScanService::commitStagedScanResults")) {
        emit errorOccurred(*err);
        upsertOk = false;
        break;
      }

      QStringList paths;
      QStringList relPaths;
      QStringList names;
      QStringList lastModified;
      paths.reserve(APPLY_BATCH_SIZE);
      relPaths.reserve(APPLY_BATCH_SIZE);
      names.reserve(APPLY_BATCH_SIZE);
      lastModified.reserve(APPLY_BATCH_SIZE);

      qint64 batchMaxRowId = lastRowId;
      while (sel.next()) {
        const qint64 rowId = sel.value(0).toLongLong();
        batchMaxRowId = std::max(batchMaxRowId, rowId);
        paths.append(sel.value(1).toString());
        relPaths.append(sel.value(2).toString());
        names.append(sel.value(3).toString());
        lastModified.append(sel.value(4).toString());
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
                    "rel_path, name, last_modified, date_added) VALUES ";
      QStringList valueSets;
      valueSets.reserve(paths.size());
      for (int i = 0; i < paths.size(); ++i) {
        valueSets.append("(?, ?, ?, ?, ?, ?, ?)");
      }
      sql += valueSets.join(", ");
      sql += " ON CONFLICT(collection_uuid, path) DO UPDATE SET "
             "collection_id=excluded.collection_id, "
             "rel_path=excluded.rel_path, "
             "name=excluded.name, "
             "last_modified=excluded.last_modified";

      // Route through the prepared-statement cache so full batches (the
      // common case — every batch except the last is APPLY_BATCH_SIZE rows
      // and produces an identical SQL string) reuse the same prepared
      // statement across the entire scan instead of paying a fresh
      // prepare/finalize cycle per batch (Kartend-o5mr). The trailing
      // partial batch gets its own cache entry; both stay bounded by the
      // cache's max size. lastError() is read off the cached statement;
      // it still surfaces SQLite's "too many SQL variables" diagnostics.
      QSqlQuery &ins = m_cache.get(sql);
      for (int i = 0; i < paths.size(); ++i) {
        ins.addBindValue(legacyId);
        ins.addBindValue(uuid);
        ins.addBindValue(paths[i]);
        ins.addBindValue(relPaths[i]);
        ins.addBindValue(names[i]);
        ins.addBindValue(lastModified[i]);
        ins.addBindValue(nowEpochSec);
      }
      if (auto err = execAndLog(ins, "Failed to apply staged scan results",
                                "ScanService::commitStagedScanResults")) {
        emit errorOccurred(*err);
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
  const bool deleteOk = m_scannedItems.deleteItemsMissingFromScan(uuid);

  // Update collection metadata (last_scanned + dir_signature).
  QSqlQuery &meta = m_cache.get("UPDATE collections SET last_scanned = ?, "
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
                                        "ScanService::commitStagedScanResults")
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
bool ScanService::scanAndSaveItemsToDatabase(int collectionIndex,
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
                                   "ScanService::scanAndSaveItemsToDatabase");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return false;
  }

  QDir dir(collection.mediaDirectory);
  if (!dir.exists()) {
    auto err =
        ErrorContext::warning(ErrorCode::MediaDirectoryNotFound, "Media directory does not exist",
                              "ScanService::scanAndSaveItemsToDatabase")
            .withDetails(collection.mediaDirectory);
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return false;
  }

  // Include includeContentSubfolders in the signature to match needsRescan.
  QString extSignature =
      collection.extensions.isEmpty() ? QString() : collection.extensions.join('|');
  extSignature += collection.folderBrowsing.includeContentSubfolders ? "|subfolders" : "";

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Temporarily disable synchronous writes for bulk insert performance.
  QSqlQuery pragmaOff(m_db);
  // Kartend-y9if: NORMAL has the same speed envelope as OFF under WAL
  // (the actual win comes from coalescing fsyncs across the transaction,
  // not from skipping them entirely) and rules out the torn-page
  // corruption window a crash mid-scan could otherwise leave behind.
  if (!pragmaOff.exec("PRAGMA synchronous = NORMAL")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to set synchronous=OFF for bulk insert",
                                               "ScanService::scanAndSaveItemsToDatabase")
                             .withDetails(pragmaOff.lastError().text()));
  }
  const SynchronousPragmaGuard restoreSynchronous(m_db);

  // Prepare the staging temp table.
  if (!m_scannedItems.ensure()) {
    return false;
  }
  m_scannedItems.clear();

  struct ScannedItemsTempTableCleanup {
    ScanService *self = nullptr;
    bool enabled = false;
    ~ScannedItemsTempTableCleanup() {
      if (enabled && self) {
        self->m_scannedItems.clear();
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

bool ScanService::prepareCollectionForItemsInsert(const CollectionConfig &collection,
                                                  const QString &uuid, const QString &extSignature,
                                                  int &legacyIdOut) {
  legacyIdOut = -1;

  // Retry constants for lock handling. Kartend-6yhq dropped MAX_RETRIES
  // from 5 to 4: the previous worst-case backoff cumulative was
  // 100+200+400+800+1600 = 3.1s of dead worker thread time on a
  // genuinely contended DB. The new ceiling is 100+200+400+800 = 1.5s,
  // which is still long enough for sane contention to clear without
  // pinning the scan worker for multiple seconds per collection.
  // Cancellation is also polled between sleeps so the user can interrupt
  // mid-backoff.
  constexpr int MAX_RETRIES = 4;
  constexpr int BASE_DELAY_MS = 100;

  bool prepareSuccess = false;
  for (int attempt = 0; attempt < MAX_RETRIES && !prepareSuccess; ++attempt) {
    if (attempt > 0) {
      if (isScanCancelled()) {
        return false;
      }
      // Exponential backoff: 100ms, 200ms, 400ms, 800ms
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
        const QString initialLastScanned = QDateTime::fromSecsSinceEpoch(0).toString(Qt::ISODate);
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

      QString errorText = ErrorUtils::exceptionMessage(e);
      bool isLockError = errorText.contains("locked", Qt::CaseInsensitive);

      if (!isLockError || attempt == MAX_RETRIES - 1) {
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to prepare collection for items",
                                          "ScanService::prepareCollectionForItemsInsert")
                       .withDetails(errorText);
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        return false;
      }
    }
  }

  return prepareSuccess;
}

void ScanService::saveItemsToDatabase(int collectionIndex, const QStringList &filePaths,
                                      const QHash<QString, QDateTime> &timestamps,
                                      const CollectionConfig &collection,
                                      const QString &dirSignature) {
  Q_UNUSED(collectionIndex)

  if (!m_db.isOpen()) {
    auto err = ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database is not open",
                                   "ScanService::saveItemsToDatabase");
    ErrorUtils::logError(err);
    return;
  }

  // Include includeContentSubfolders in the signature to match needsRescan
  QString extSignature =
      collection.extensions.isEmpty() ? QString() : collection.extensions.join('|');
  extSignature += collection.folderBrowsing.includeContentSubfolders ? "|subfolders" : "";

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Temporarily disable synchronous writes for bulk insert performance
  QSqlQuery pragmaOff(m_db);
  // Kartend-y9if: NORMAL has the same speed envelope as OFF under WAL
  // (the actual win comes from coalescing fsyncs across the transaction,
  // not from skipping them entirely) and rules out the torn-page
  // corruption window a crash mid-scan could otherwise leave behind.
  if (!pragmaOff.exec("PRAGMA synchronous = NORMAL")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to set synchronous=OFF for bulk insert",
                                               "ScanService::saveItemsToDatabase")
                             .withDetails(pragmaOff.lastError().text()));
  }
  const SynchronousPragmaGuard restoreSynchronous(m_db);

  // Kartend-v5d4: BATCH_SIZE / COMMIT_INTERVAL_BATCHES / PROGRESS_REPORT_INTERVAL
  // are file-scope constants in the anonymous namespace at the top of this
  // TU; the local-static re-declarations that used to live here shadowed
  // them and could silently drift apart from the canonical values during a
  // refactor. Using the file-scope names directly removes that footgun.
  const int totalItems = filePaths.size();

  // Cancellation-safe writes: stage into TEMP table and only apply to
  // persistent DB on success.
  if (!m_scannedItems.ensure()) {
    return;
  }

  m_scannedItems.clear();

  struct ScannedItemsTempTableCleanup {
    ScanService *self = nullptr;
    bool enabled = false;
    ~ScannedItemsTempTableCleanup() {
      if (enabled && self) {
        self->m_scannedItems.clear();
      }
    }
  } scannedItemsCleanup{this, true};

  // Second phase: insert items in batches with periodic commits
  int batchesSinceCommit = 0;
  bool inTransaction = false;
  int itemsInserted = 0; // cppcheck-suppress unreadVariable - used for progress reporting

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
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to start transaction for bulk insert",
                                          "ScanService::saveItemsToDatabase")
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
    m_scannedItems.insertBatch(batchPaths, timestamps, collection.mediaDirectory);

    itemsInserted = batchEnd;
    ++batchesSinceCommit;

    // Commit periodically to save incremental progress
    if (batchesSinceCommit >= COMMIT_INTERVAL_BATCHES) {
      if (!m_db.commit()) {
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to commit bulk insert transaction",
                                          "ScanService::saveItemsToDatabase")
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
      auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                        "Failed to commit final bulk insert transaction",
                                        "ScanService::saveItemsToDatabase")
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
      auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                        "Failed to start transaction to apply staged scan results",
                                        "ScanService::saveItemsToDatabase")
                     .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return;
    }

    const bool upsertOk = m_scannedItems.applyToItems(legacyId, uuid);
    const bool deleteOk = m_scannedItems.deleteItemsMissingFromScan(uuid);

    QSqlQuery &meta = m_cache.get(QuerySQL::UPDATE_COLLECTION_SCAN_METADATA);
    meta.bindValue(0, QDateTime::currentDateTime().toString(Qt::ISODate));
    meta.bindValue(1, dirSignature);
    meta.bindValue(2, uuid);
    const bool metaOk = meta.exec();

    if (upsertOk && deleteOk && metaOk) {
      if (!m_db.commit()) {
        // A dropped commit leaves the scan metadata unpersisted, so
        // needsRescan() keeps returning true and the collection re-scans every
        // launch (Kartend-gv7f). Surface it and roll back rather than swallow.
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to commit staged scan results",
                                          "ScanService::saveItemsToDatabase")
                       .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        m_db.rollback();
      }
    } else {
      m_db.rollback();
    }
  }
}
