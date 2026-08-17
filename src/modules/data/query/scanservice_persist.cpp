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

#include "dbtxn.h"
#include "scanservice_internal.h"

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
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <stdexcept>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "errorutils.h"
#include "multidisc.h"
#include "multidisccollapse.h"
#include "pathutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "scanartwork.h"
#include "uiconstants/database.h"

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using namespace QueryManagerInternal;
using namespace ScanServiceInternal;

// APPLY_BATCH_SIZE is persist-only; the rest of the shared scan-pipeline
// constants + helpers (BATCH_SIZE, COMMIT_INTERVAL_BATCHES,
// PROGRESS_REPORT_INTERVAL, execAndLog, ScanProgressThrottle) now live in
// scanservice_internal.h.
namespace {
constexpr int APPLY_BATCH_SIZE = KartendDb::BatchSizes::StagedScanApplyBatch;

// Kartend-3mq7v: the multi-disc collapse post-pass, shared by both persist
// pipelines (the streaming scanAndSaveItemsToDatabase and the in-memory
// saveItemsToDatabase). Both stage into the same connection-local
// scanned_items table and both apply from it, so the collapse belongs at the
// single point between those two steps rather than inside either walk — see
// multidisccollapse.h for why it cannot be a filter in the stream.
//
// Runs on BOTH arms of the setting, not just when grouping is on: a collection
// the user turned grouping OFF for has to have its managed playlist directory
// removed, or the generated .m3u files outlive the feature that produced them.
// The member files stage normally on the same scan, so the library goes back to
// one item per disc in a single pass.
[[nodiscard]] QList<MultiDiscCollapse::CollapsedGroup>
collapseMultiDiscGroups(QSqlDatabase &db, int &txnDepth, const CollectionConfig &collection,
                        const QString &uuid) {
  const QString playlistDir = MultiDisc::playlistDirFor(
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), uuid);
  if (!collection.groupMultiDisc) {
    (void)MultiDiscCollapse::discardPlaylists(playlistDir);
    return {};
  }
  return MultiDiscCollapse::collapseStagedGroups(db, txnDepth, uuid, playlistDir);
}

// Discs that became one item no longer count individually towards the "X of Y
// items added" summary — N staged members collapse to a single staged row.
[[nodiscard]] int stagedCountAfterCollapse(int itemsStaged,
                                           const QList<MultiDiscCollapse::CollapsedGroup> &groups) {
  for (const MultiDiscCollapse::CollapsedGroup &group : groups) {
    itemsStaged -= static_cast<int>(group.memberPaths.size()) - 1;
  }
  return std::max(0, itemsStaged);
}
} // namespace

// ============================================================================
// Phase 2 – commitStagedScanResults
// ============================================================================
bool ScanService::commitStagedScanResults(const CollectionConfig &collection, const QString &uuid,
                                          const QString &extSignature, const QString &dirSignature,
                                          int &itemsApplied) {
  itemsApplied = 0;

  // Throttle progress emissions during the apply phase. Shared across retry
  // attempts so a retried apply keeps the emission cadence instead of
  // restarting it.
  ScanProgressThrottle throttle(UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS,
                                [this](int p, int t) { emit scanItemsProgress(p, t); });
  auto maybeEmitScanProgress = [&](int processed, int total, bool force) {
    throttle.report(processed, total, force);
  };

  // Prepare collection row (upsert into collections table). Has its own
  // bounded SQLITE_BUSY retry ladder (Kartend-5s54/6yhq).
  int legacyId = -1;
  if (!prepareCollectionForItemsInsert(collection, uuid, extSignature, legacyId)) {
    return false;
  }

  // Kartend-kt39d: bounded retry of the WHOLE apply transaction on lock
  // contention. The apply is a deferred BEGIN, then a COUNT read, then
  // writes — so a commit landing on another connection (main-thread
  // DatabaseManager, the query slots) between the snapshot read and the
  // write-lock upgrade fails the first write INSTANTLY with
  // SQLITE_BUSY_SNAPSHOT (the busy handler never engages; extending
  // busy_timeout does nothing — measured and documented in Kartend-h62cc).
  // A fresh attempt takes a new snapshot, which resolves BUSY_SNAPSHOT
  // outright; a writer genuinely HOLDING the lock engages this connection's
  // busy_timeout inside the attempt instead. The scanned_items staging table
  // is TEMP (connection-local) and only read by the apply, and a failed
  // attempt rolls back completely, so the apply is restartable as-is.
  // Without this ladder a single lost lock was TERMINAL for the session:
  // ensureCollectionScanned records the uuid in m_failedScanUuids and every
  // later reload-driven pass skips the collection until app restart.
  constexpr int MAX_APPLY_ATTEMPTS = 3;
  constexpr int BASE_DELAY_MS = 100;
  for (int attempt = 0; attempt < MAX_APPLY_ATTEMPTS; ++attempt) {
    if (attempt > 0) {
      if (isScanCancelled()) {
        return false;
      }
      // Brief backoff (100ms, 200ms) so a writer mid-commit can clear; the
      // BUSY_SNAPSHOT case would succeed immediately either way.
      QThread::msleep(BASE_DELAY_MS * (1 << (attempt - 1)));
    }
    bool lockContention = false;
    const bool finalAttempt = (attempt == MAX_APPLY_ATTEMPTS - 1);
    if (applyStagedScanResultsAttempt(uuid, dirSignature, legacyId, maybeEmitScanProgress,
                                      finalAttempt, lockContention, itemsApplied)) {
      if (attempt > 0) {
        qCWarning(lcQueryManager).nospace()
            << "ScanService::commitStagedScanResults: apply landed on attempt " << (attempt + 1)
            << " after database-is-locked contention";
      }
      return true;
    }
    if (!lockContention) {
      // Genuine failure or cancellation — already reported by the attempt.
      return false;
    }
    // Lock contention with retries remaining: the attempt logged at debug
    // with a will-retry note; loop for a fresh snapshot.
  }
  return false; // terminal: the final attempt reported at warning + errorOccurred
}

bool ScanService::applyStagedScanResultsAttempt(
    const QString &uuid, const QString &dirSignature, int legacyId,
    const std::function<void(int, int, bool)> &maybeEmitScanProgress, bool finalAttempt,
    bool &lockContention, int &itemsApplied) {
  itemsApplied = 0;
  lockContention = false;

  // Classifying reporter (Kartend-kt39d, mirroring the h62cc shape in
  // ensureItemsFtsReady): lock contention on a non-final attempt logs at
  // debug with a will-retry note — the caller retries the whole transaction
  // on a fresh snapshot — while the final attempt and every non-lock failure
  // keep the warning/critical log + errorOccurred emission.
  const auto reportFailure = [&](const ErrorUtils::ErrorContext &err) {
    if (KartendDb::isLockContentionError(err)) {
      lockContention = true;
      if (!finalAttempt) {
        qCDebug(lcQueryManager).nospace()
            << err.source << ": " << err.message << " (" << err.details
            << ") — transient lock contention; retrying the apply on a fresh snapshot";
        return;
      }
    }
    ErrorUtils::logError(err);
    emit errorOccurred(err);
  };
  const auto queryError = [](const QString &what, const QSqlError &e) {
    return ErrorContext::warning(ErrorCode::DatabaseQueryFailed, what,
                                 "ScanService::commitStagedScanResults")
        .withDetails(e.text());
  };

  // Begin the apply transaction on the shared QueryManager depth counter
  // (Kartend-l94tw: was the depth-less ctor, which bypassed coordination with
  // other guards on this connection). No error sink on the guard: failures
  // route through reportFailure above so retried attempts stay quiet.
  KartendDb::DbTransaction txn(m_db, m_txnDepth, "ScanService::commitStagedScanResults");
  if (!txn.active()) {
    reportFailure(txn.beginError("Failed to start transaction to apply scan results"));
    return false;
  }

  // Count staged rows so we can report "Indexing X of Y" progress.
  qint64 totalToApply = 0;
  {
    QSqlQuery count(m_db);
    if (count.exec("SELECT COUNT(*) FROM scanned_items") && count.next()) {
      totalToApply = count.value(0).toLongLong();
    } else {
      reportFailure(queryError("Failed to count staged scan results", count.lastError()));
      return false; // guard dtor rolls back
    }
  }

  // Force the overlay into "Indexing" mode (total known) immediately.
  maybeEmitScanProgress(
      0, static_cast<int>(std::min<qint64>(totalToApply, std::numeric_limits<int>::max())), true);

  // Batched upsert: scanned_items → items.
  if (totalToApply > 0) {
    qint64 applied = 0;
    qint64 lastRowId = 0;

    while (applied < totalToApply) {
      if (isScanCancelled()) {
        return false; // guard dtor rolls back; not retried (lockContention stays false)
      }

      QSqlQuery &sel = m_cache.get(QuerySQL::SELECT_STAGED_SCAN_RESULTS);
      // Positional binds — cached statements must never addBindValue (see
      // the PreparedStatementCache contract).
      sel.bindValue(0, lastRowId);
      sel.bindValue(1, APPLY_BATCH_SIZE);
      if (!sel.exec()) {
        reportFailure(queryError("Failed to read staged scan results", sel.lastError()));
        return false;
      }

      QStringList paths;
      QStringList relPaths;
      QStringList names;
      QStringList lastModified;
      QList<qint64> fileSizes;
      QStringList artworkPaths;
      paths.reserve(APPLY_BATCH_SIZE);
      relPaths.reserve(APPLY_BATCH_SIZE);
      names.reserve(APPLY_BATCH_SIZE);
      lastModified.reserve(APPLY_BATCH_SIZE);
      fileSizes.reserve(APPLY_BATCH_SIZE);
      artworkPaths.reserve(APPLY_BATCH_SIZE);

      qint64 batchMaxRowId = lastRowId;
      while (sel.next()) {
        const qint64 rowId = sel.value(0).toLongLong();
        batchMaxRowId = std::max(batchMaxRowId, rowId);
        paths.append(sel.value(1).toString());
        relPaths.append(sel.value(2).toString());
        names.append(sel.value(3).toString());
        lastModified.append(sel.value(4).toString());
        fileSizes.append(sel.value(5).toLongLong());
        artworkPaths.append(sel.value(6).toString());
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

      // file_size IS in the DO UPDATE clause (unlike date_added): sizes
      // change on rescan and the Size sort / SQL fast paths read this
      // column, so a stale value is as bad as a missing one. The streaming
      // pipeline previously omitted the column entirely and every row it
      // persisted kept file_size DEFAULT 0 — degrading sort_file_size to
      // name order and forcing the metadata path to stat-fallback
      // (Kartend-3gb7e). Mirrors ScannedItemsTable::applyToItems.
      //
      // artwork_path is in the same category as file_size — a per-scan cache of
      // something on disk, refreshed on conflict (Kartend-guyc5). The staged
      // value comes from ScanArtwork::resolveStagedArtwork; an empty string is
      // "no cover found on this scan", which is what every DB-side artwork
      // predicate tests for and what the previous value must be replaced with
      // when the art is gone.
      QString sql = "INSERT INTO items (collection_id, collection_uuid, path, "
                    "rel_path, name, last_modified, file_size, date_added, artwork_path) VALUES ";
      QStringList valueSets;
      valueSets.reserve(paths.size());
      for (int i = 0; i < paths.size(); ++i) {
        valueSets.append("(?, ?, ?, ?, ?, ?, ?, ?, ?)");
      }
      sql += valueSets.join(", ");
      sql += " ON CONFLICT(collection_uuid, path) DO UPDATE SET "
             "collection_id=excluded.collection_id, "
             "rel_path=excluded.rel_path, "
             "name=excluded.name, "
             "last_modified=excluded.last_modified, "
             "file_size=excluded.file_size, "
             "artwork_path=excluded.artwork_path";

      // Route through the prepared-statement cache so full batches (the
      // common case — every batch except the last is APPLY_BATCH_SIZE rows
      // and produces an identical SQL string) reuse the same prepared
      // statement across the entire scan instead of paying a fresh
      // prepare/finalize cycle per batch (Kartend-o5mr). The trailing
      // partial batch gets its own cache entry; both stay bounded by the
      // cache's max size. lastError() is read off the cached statement;
      // it still surfaces SQLite's "too many SQL variables" diagnostics.
      QSqlQuery &ins = m_cache.get(sql);
      // Positional binds — cached statements must never addBindValue (see
      // the PreparedStatementCache contract).
      int bindPos = 0;
      for (int i = 0; i < paths.size(); ++i) {
        ins.bindValue(bindPos++, legacyId);
        ins.bindValue(bindPos++, uuid);
        ins.bindValue(bindPos++, paths[i]);
        ins.bindValue(bindPos++, relPaths[i]);
        ins.bindValue(bindPos++, names[i]);
        ins.bindValue(bindPos++, lastModified[i]);
        ins.bindValue(bindPos++, fileSizes[i]);
        ins.bindValue(bindPos++, nowEpochSec);
        ins.bindValue(bindPos++, artworkPaths[i]);
      }
      if (!ins.exec()) {
        // The hot lock-contention site: the txn's first write after the
        // deferred COUNT read — where SQLITE_BUSY_SNAPSHOT lands.
        reportFailure(queryError("Failed to apply staged scan results", ins.lastError()));
        return false;
      }

      lastRowId = batchMaxRowId;
      applied += paths.size();

      const int clampedApplied =
          static_cast<int>(std::min<qint64>(applied, std::numeric_limits<int>::max()));
      const int clampedTotal =
          static_cast<int>(std::min<qint64>(totalToApply, std::numeric_limits<int>::max()));
      maybeEmitScanProgress(clampedApplied, clampedTotal, false);
    }

    // Force a final progress update so the overlay reaches 100% for indexing.
    const int clampedTotal =
        static_cast<int>(std::min<qint64>(totalToApply, std::numeric_limits<int>::max()));
    maybeEmitScanProgress(clampedTotal, clampedTotal, true);
  }

  // Delete items no longer on disk. When the staged set is empty this is the
  // txn's first write, so it can hit BUSY_SNAPSHOT too — classify via the
  // error details it exposes.
  QString deleteErrorDetails;
  if (!m_scannedItems.deleteItemsMissingFromScan(uuid, &deleteErrorDetails)) {
    reportFailure(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                        "Failed to delete missing items using scanned_items",
                                        "ScanService::commitStagedScanResults")
                      .withDetails(deleteErrorDetails));
    return false;
  }

  // Update collection metadata (last_scanned + dir_signature).
  QSqlQuery &meta = m_cache.get("UPDATE collections SET last_scanned = ?, "
                                "dir_signature = ? WHERE uuid = ?");
  meta.bindValue(0, QDateTime::currentDateTime().toString(Qt::ISODate));
  meta.bindValue(1, dirSignature);
  meta.bindValue(2, uuid);
  if (!meta.exec()) {
    // Pre-kt39d this failure was silent (metaOk just skipped the commit).
    reportFailure(queryError("Failed to update collection scan metadata", meta.lastError()));
    return false;
  }

  // Commit the apply transaction. On failure the guard's dtor rolls back on
  // scope exit — nothing below touches the connection. commitError() must be
  // built before any other DB work disturbs lastError().
  if (!txn.commit()) {
    reportFailure(txn.commitError("Failed to commit scan results"));
    return false;
  }

  itemsApplied = static_cast<int>(std::min<qint64>(totalToApply, std::numeric_limits<int>::max()));
  return true;
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

  // Same signature builder needsRescan uses — the two must never drift.
  const QString extSignature = buildExtSignature(collection);

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Kartend-fux2w: the per-scan "PRAGMA synchronous" set + restore guard were
  // deleted — every connection already opens at NORMAL via ConnectionPragmas
  // (Kartend-y9if chose NORMAL over OFF: same speed envelope under WAL, no
  // torn-page window), so both were no-ops with a misleading
  // "synchronous=OFF" failure message.

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
  const QStringList nameFilters = buildNameFilters(collection);

  // Phase 1: Walk filesystem and stage into temp table.
  int itemsStaged = 0;
  QString dirSignature;
  if (!stageFilesystemScan(collection, nameFilters, itemsStaged, dirSignature)) {
    return false;
  }

  if (isScanCancelled()) {
    return false;
  }

  // Phase 1.5: collapse multi-disc releases in the staging table so phase 2
  // applies one row per release instead of one per disc. Deliberately after the
  // cancellation check — a cancelled scan must not write playlists.
  const QList<MultiDiscCollapse::CollapsedGroup> collapsed =
      collapseMultiDiscGroups(m_db, m_txnDepth, collection, uuid);
  itemsStaged = stagedCountAfterCollapse(itemsStaged, collapsed);

  // Phase 1.6: resolve each staged row's cover into the staging table so the
  // apply carries it into items.artwork_path (Kartend-guyc5). AFTER the
  // collapse on purpose: the rows that exist now are the rows that will become
  // items, so a collapsed release resolves art the same way its tile will.
  if (!isScanCancelled()) {
    (void)ScanArtwork::resolveStagedArtwork(m_db, m_txnDepth, collection.artworkDirectory, uuid,
                                            collection.mediaDirectory,
                                            collection.folderBrowsing.includeArtworkSubfolders);
  }

  // Phase 2: Apply staged results to persistent DB.
  int itemsApplied = 0;
  const bool success =
      commitStagedScanResults(collection, uuid, extSignature, dirSignature, itemsApplied);

  // Phase 3: the collapsed items' merged metadata. Only now does the row it
  // writes to exist, and only a committed apply means the member rows it was
  // gathered from are really gone.
  if (success && !collapsed.isEmpty()) {
    MultiDiscCollapse::applyMergedMetadata(m_db, uuid, collapsed);
  }

  // surface scan stats to the caller.
  if (outItemsScanned) {
    *outItemsScanned = itemsStaged;
  }
  if (outItemsApplied) {
    *outItemsApplied = success ? itemsApplied : 0;
  }
  return success;
}

namespace {
// Carries the driver's native error code alongside the message so the retry loop
// below can decide retry-vs-abort on the stable SQLite result code rather than a
// locale/phrasing-fragile text match (Kartend-5s54).
struct SqlStepError : std::runtime_error {
  explicit SqlStepError(const QSqlError &e)
      : std::runtime_error(e.text().toStdString()), nativeCode(e.nativeErrorCode()) {}
  QString nativeCode;
};
} // namespace

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

    // Guard on the shared depth counter (Kartend-l94tw). A failed BEGIN just
    // retries the attempt; any exception below unwinds into the catch and the
    // guard's dtor rolls back at the end of the iteration.
    KartendDb::DbTransaction txn(m_db, m_txnDepth, "ScanService::prepareCollectionForItemsInsert");
    if (!txn.active()) {
      continue;
    }

    try {
      QSqlQuery update(m_db);
      update.prepare("UPDATE collections SET name=?, ext_signature=? WHERE uuid=?");
      update.addBindValue(collection.name);
      update.addBindValue(extSignature);
      update.addBindValue(uuid);
      // Kartend-fux2w: a failed UPDATE (lock, IO) must enter the SQLITE_BUSY
      // retry ladder like every other statement in this try block — the
      // discarded return previously dropped collection renames / signature
      // refreshes silently.
      if (!update.exec()) {
        throw SqlStepError(update.lastError());
      }

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
          throw SqlStepError(insert.lastError());
        }
      }

      {
        QSqlQuery idq(m_db);
        idq.prepare("SELECT id FROM collections WHERE uuid = ?");
        idq.addBindValue(uuid);
        if (!idq.exec()) {
          throw SqlStepError(idq.lastError());
        }
        if (!idq.next()) {
          // The row was just upserted above, so a missing id is a real DB
          // fault. Abort so the transaction rolls back rather than committing
          // collection_id = -1 for every applied item (Kartend-f7iy3).
          throw SqlStepError(idq.lastError());
        }
        legacyIdOut = idq.value(0).toInt();
      }

      if (!txn.commit()) {
        throw SqlStepError(m_db.lastError());
      }
      prepareSuccess = true;
    } catch (const std::exception &e) {
      // No explicit rollback: the guard's dtor rolls back when this attempt's
      // scope exits without a successful commit.
      const QString errorText = ErrorUtils::exceptionMessage(e);
      // Prefer the driver's native result code (locale/phrasing-stable):
      // SQLITE_BUSY = 5, SQLITE_LOCKED = 6. Keep the text match as a fallback so
      // detection never regresses if a driver reports no native code (Kartend-5s54).
      bool isLockError = errorText.contains(QLatin1String("locked"), Qt::CaseInsensitive);
      if (const auto *sqlErr = dynamic_cast<const SqlStepError *>(&e)) {
        const int code = sqlErr->nativeCode.toInt();
        isLockError = isLockError || code == 5 || code == 6;
      }

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

  // Same signature builder needsRescan uses — the two must never drift.
  const QString extSignature = buildExtSignature(collection);

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Kartend-fux2w: per-scan synchronous-pragma set + guard deleted — see the
  // matching note in scanAndSaveItemsToDatabase above.

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

  // Second phase: insert items in batches with periodic commits. The
  // interval-commit transaction uses the shared DbTransaction guard on the
  // QueryManager-owned depth counter (Kartend-l94tw): emplace() begins,
  // commit()+reset() closes an interval, reset() alone rolls back
  // (cancellation), and destruction covers every early return.
  int batchesSinceCommit = 0;
  std::optional<KartendDb::DbTransaction> txn;
  int itemsInserted = 0; // cppcheck-suppress unreadVariable - used for progress reporting

  for (int batchStart = 0; batchStart < totalItems; batchStart += BATCH_SIZE) {
    // Check for cancellation between batches
    if (isScanCancelled()) {
      txn.reset(); // rolls back any open interval transaction
      break;
    }

    // Start new transaction if needed. emplace() inline (not via a helper
    // lambda) so bugprone-unchecked-optional-access can see the optional is
    // engaged before the accesses below.
    if (!txn.has_value()) {
      txn.emplace(m_db, m_txnDepth, "ScanService::saveItemsToDatabase",
                  [this](const ErrorUtils::ErrorContext &e) { emit errorOccurred(e); });
      if (!txn->activeOrReport("Failed to start transaction for bulk insert")) {
        return;
      }
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
    if (!m_scannedItems.insertBatch(batchPaths, timestamps, collection.mediaDirectory)) {
      // A failed staging INSERT leaves the temp table partial; roll back the
      // open interval transaction and bail before the apply block, so phase 2's
      // deleteItemsMissingFromScan never runs against a partial scan (it would
      // prune items whose rows merely failed to stage) and last_scanned /
      // dir_signature are not advanced (Kartend-o1ed7). insertBatch already
      // logged the cause; the staging cleanup guard empties the temp table.
      txn.reset(); // guard dtor rolls back the aborted transaction
      return;
    }

    itemsInserted = batchEnd;
    ++batchesSinceCommit;

    // Commit periodically to save incremental progress. The has_value()
    // term is always true here (the loop top engages the optional) but makes
    // the access provably checked for bugprone-unchecked-optional-access.
    if (batchesSinceCommit >= COMMIT_INTERVAL_BATCHES && txn.has_value()) {
      if (!txn->commitOrReport("Failed to commit bulk insert transaction")) {
        return; // guard dtor rolls back the aborted transaction (no-op)
      }
      txn.reset();

      // Report progress
      emit scanItemsProgress(itemsInserted, totalItems);
    }

    // Report progress at intervals even within transaction
    if (itemsInserted % PROGRESS_REPORT_INTERVAL == 0) {
      emit scanItemsProgress(itemsInserted, totalItems);
    }
  }

  // Final commit for any remaining items
  if (txn) {
    if (!txn->commitOrReport("Failed to commit final bulk insert transaction")) {
      return; // guard dtor rolls back the aborted transaction (no-op)
    }
    txn.reset();
  }

  // Final progress report
  if (!isScanCancelled()) {
    emit scanItemsProgress(totalItems, totalItems);

    // Multi-disc collapse (Kartend-3mq7v): same post-pass the streaming
    // pipeline runs, at the same point — after staging, before the apply.
    const QList<MultiDiscCollapse::CollapsedGroup> collapsed =
        collapseMultiDiscGroups(m_db, m_txnDepth, collection, uuid);

    // Staged-artwork resolution (Kartend-guyc5): same pass, same seam as the
    // streaming pipeline — after the collapse, before the apply.
    (void)ScanArtwork::resolveStagedArtwork(m_db, m_txnDepth, collection.artworkDirectory, uuid,
                                            collection.mediaDirectory,
                                            collection.folderBrowsing.includeArtworkSubfolders);

    int legacyId = -1;
    if (!prepareCollectionForItemsInsert(collection, uuid, extSignature, legacyId)) {
      return;
    }

    // Apply staged results atomically, under the same bounded lock-contention
    // retry + classification as commitStagedScanResults (Kartend-kt39d — see
    // the ladder rationale there). This legacy path previously ran the apply
    // once and swallowed a meta.exec() failure entirely (metaOk just skipped
    // the commit; the guard's dtor rolled back with no log and no
    // errorOccurred), so last_scanned never advanced and needsRescan re-walked
    // the directory on every load, invisibly. Every step now routes through a
    // classifying reporter: lock contention on a non-final attempt logs at
    // debug and retries the whole transaction on a fresh snapshot; the final
    // attempt and every non-lock failure keep the warning log + errorOccurred.
    // The staged temp table is connection-local and only read by the apply,
    // and a failed attempt rolls back completely, so the apply is restartable.
    const auto applyAttempt = [&](bool finalAttempt, bool &lockContention) -> bool {
      lockContention = false;
      const auto reportFailure = [&](const ErrorUtils::ErrorContext &err) {
        if (KartendDb::isLockContentionError(err)) {
          lockContention = true;
          if (!finalAttempt) {
            qCDebug(lcQueryManager).nospace()
                << err.source << ": " << err.message << " (" << err.details
                << ") — transient lock contention; retrying the apply on a fresh snapshot";
            return;
          }
        }
        ErrorUtils::logError(err);
        emit errorOccurred(err);
      };
      const auto stepError = [](const QString &what, const QString &details) {
        return ErrorContext::warning(ErrorCode::DatabaseQueryFailed, what,
                                     "ScanService::saveItemsToDatabase")
            .withDetails(details);
      };

      // No error sink on the guard: failures route through reportFailure so
      // retried attempts stay quiet (same shape as applyStagedScanResultsAttempt).
      KartendDb::DbTransaction applyTxn(m_db, m_txnDepth, "ScanService::saveItemsToDatabase");
      if (!applyTxn.active()) {
        reportFailure(
            applyTxn.beginError("Failed to start transaction to apply staged scan results"));
        return false;
      }

      QString upsertErrorDetails;
      if (!m_scannedItems.applyToItems(legacyId, uuid, &upsertErrorDetails)) {
        // The hot lock-contention site: the txn's first write.
        reportFailure(stepError("Failed to apply scanned_items upsert", upsertErrorDetails));
        return false;
      }

      QString deleteErrorDetails;
      if (!m_scannedItems.deleteItemsMissingFromScan(uuid, &deleteErrorDetails)) {
        reportFailure(
            stepError("Failed to delete missing items using scanned_items", deleteErrorDetails));
        return false;
      }

      QSqlQuery &meta = m_cache.get(QuerySQL::UPDATE_COLLECTION_SCAN_METADATA);
      meta.bindValue(0, QDateTime::currentDateTime().toString(Qt::ISODate));
      meta.bindValue(1, dirSignature);
      meta.bindValue(2, uuid);
      if (!meta.exec()) {
        reportFailure(
            stepError("Failed to update collection scan metadata", meta.lastError().text()));
        return false;
      }

      // A dropped commit leaves the scan metadata unpersisted, so
      // needsRescan() keeps returning true and the collection re-scans every
      // launch (Kartend-gv7f). Surface it rather than swallow; the guard's
      // dtor rolls back the aborted transaction.
      if (!applyTxn.commit()) {
        reportFailure(applyTxn.commitError("Failed to commit staged scan results"));
        return false;
      }
      return true;
    };

    constexpr int MAX_APPLY_ATTEMPTS = 3;
    constexpr int BASE_DELAY_MS = 100;
    for (int attempt = 0; attempt < MAX_APPLY_ATTEMPTS; ++attempt) {
      if (attempt > 0) {
        if (isScanCancelled()) {
          return;
        }
        // Brief backoff (100ms, 200ms) so a writer mid-commit can clear.
        QThread::msleep(BASE_DELAY_MS * (1 << (attempt - 1)));
      }
      bool lockContention = false;
      if (applyAttempt(attempt == MAX_APPLY_ATTEMPTS - 1, lockContention)) {
        if (attempt > 0) {
          qCWarning(lcQueryManager).nospace()
              << "ScanService::saveItemsToDatabase: apply landed on attempt " << (attempt + 1)
              << " after database-is-locked contention";
        }
        // The collapsed items' rows exist only now that the apply committed.
        if (!collapsed.isEmpty()) {
          MultiDiscCollapse::applyMergedMetadata(m_db, uuid, collapsed);
        }
        return;
      }
      if (!lockContention) {
        return; // genuine failure — already reported by the attempt
      }
      // Lock contention with retries remaining: loop for a fresh snapshot.
    }
    // Terminal: the final attempt reported at warning + errorOccurred.
  }
}
