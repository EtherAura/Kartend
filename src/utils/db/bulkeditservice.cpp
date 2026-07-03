// Off-thread pipeline behind MainWindow::bulkEditInteractive. See the header
// for the contract; the body is what the button handler used to run inline on
// the GUI thread (batch load, applyAction, one autocommit save per row), moved
// here so it runs on the thread pool with the row writes batched into a single
// transaction.
#include "bulkeditservice.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "dbtxn.h"
#include "errorutils.h"
#include "itemmetadata.h"

namespace BulkEditService {

namespace {
// Progress-callback throttle: report every this-many examined rows so a
// 10k-item run posts ~150 queued GUI hops instead of one per row.
constexpr int PROGRESS_STRIDE = 64;
} // namespace

Outcome run(const QString &dbPath, const QString &collectionUuid, BulkEdit::Action action,
            const QString &parameter, const QStringList &paths,
            const std::shared_ptr<std::atomic_bool> &cancelRequested, const ProgressFn &progress) {
  Outcome out;
  if (dbPath.isEmpty()) {
    return out;
  }
  if (paths.isEmpty()) {
    out.ok = true; // vacuously complete — nothing to edit
    return out;
  }
  const auto isCanceled = [&cancelRequested]() {
    return cancelRequested && cancelRequested->load(std::memory_order_acquire);
  };

  // Unique connection name per call so concurrent / sequential runs never
  // collide in QSqlDatabase's global registry (same shape as
  // StatisticsService::gather).
  static std::atomic<quint64> connSeq{0};
  const QString conn =
      QStringLiteral("kartend_bulkedit_%1").arg(connSeq.fetch_add(1, std::memory_order_relaxed));
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
      ErrorUtils::logError(ErrorUtils::ErrorContext::critical(
                               ErrorUtils::ErrorCode::DatabaseConnectionFailed,
                               "Failed to open bulk-edit connection", "BulkEditService::run")
                               .withDetails(db.lastError().text()));
    } else {
      // Wait out transient writer contention (e.g. a scan commit on the
      // manager's worker connection) instead of failing the whole edit.
      QSqlQuery(db).exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

      // Phase 1 — read. One batched WHERE-IN load (chunked internally).
      // Paths without an item_metadata row become keyed stubs so the action
      // can create their row, mirroring the per-item editor.
      auto loaded = ItemMetadataStore::loadBatch(db, collectionUuid, paths);
      if (loaded.isError()) {
        ErrorUtils::logError(loaded.error());
      } else if (isCanceled()) {
        out.canceled = true;
      } else {
        QList<ItemMetadataStore::ItemMetadata> batch;
        batch.reserve(paths.size());
        const auto &byPath = loaded.value();
        for (const QString &path : paths) {
          auto it = byPath.find(path);
          ItemMetadataStore::ItemMetadata md =
              it != byPath.end() ? it.value() : ItemMetadataStore::ItemMetadata{};
          md.collectionUuid = collectionUuid;
          md.path = path;
          batch.append(md);
        }

        // Phase 2 — apply. Pure in-memory transform; rows the action does
        // nothing to come back with changed == false and are skipped below.
        const QList<BulkEdit::PendingChange> changes =
            BulkEdit::applyAction(action, parameter, batch);
        const int total = static_cast<int>(changes.size());
        if (progress) {
          progress(0, total);
        }

        // Phase 3 — write. Every row upsert lands inside ONE transaction: a
        // 10k-row edit costs a single commit fsync instead of one per row,
        // and cancel / commit failure rolls the whole edit back atomically
        // (the guard dtor covers every early exit).
        KartendDb::DbTransaction txn(db, "BulkEditService::run");
        if (!txn.active()) {
          ErrorUtils::logError(
              txn.beginError("Failed to begin bulk-edit transaction", "BulkEditService::run"));
        } else {
          int done = 0;
          for (const BulkEdit::PendingChange &change : changes) {
            if (isCanceled()) {
              out.canceled = true;
              break;
            }
            if (change.changed) {
              // Per-row failures are logged and skipped — the pre-existing
              // GUI loop's semantics — so one bad row doesn't abort the
              // batch; "Applied to X of Y" reflects only rows really saved.
              if (auto saved = ItemMetadataStore::save(db, change.metadata); saved.isError()) {
                ErrorUtils::logError(saved.error());
              } else {
                out.writtenPaths.append(change.metadata.path);
              }
            }
            ++done;
            if (progress && (done % PROGRESS_STRIDE) == 0) {
              progress(done, total);
            }
          }
          if (!out.canceled) {
            if (txn.commit()) {
              out.written = static_cast<int>(out.writtenPaths.size());
              out.ok = true;
              if (progress) {
                progress(total, total);
              }
            } else {
              ErrorUtils::logError(
                  txn.commitError("Failed to commit bulk edit", "BulkEditService::run"));
            }
          }
        }
        if (!out.ok) {
          // Rolled back (cancel / BEGIN / COMMIT failure) — nothing was
          // persisted, so hand the caller no stale invalidation targets.
          out.writtenPaths.clear();
          out.written = 0;
        }
      }
    }
  }
  // db (and the store helpers' queries) are out of scope before
  // removeDatabase, so Qt doesn't warn about a connection still in use.
  QSqlDatabase::removeDatabase(conn);
  return out;
}

} // namespace BulkEditService
