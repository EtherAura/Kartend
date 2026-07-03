#ifndef KARTEND_UTILS_DB_BULKEDITSERVICE_H
#define KARTEND_UTILS_DB_BULKEDITSERVICE_H

#include <atomic>
#include <functional>
#include <memory>

#include <QString>
#include <QStringList>

#include "bulkedit.h"

// Off-thread read → apply → write pipeline behind the bulk-edit tool. Opens a
// private SQLite connection to the database file (the same own-connection
// shape StatisticsService::gather uses for reads and ScrapeWriteWorker uses
// for writes — QSqlDatabase connections are thread-affine, so the worker must
// never touch the GUI-thread connection), batch-loads the per-item metadata,
// applies BulkEdit::applyAction, and persists every changed row inside ONE
// transaction. Pure over its inputs (a db file path + the user's choice + the
// path list) and touches no GUI or shared state, so MainWindow can run it via
// QtConcurrent::run and the pipeline can be exercised against a real on-disk
// database in tests.

namespace BulkEditService {

/// Result bundle for run(). `writtenPaths` lists exactly the rows that were
/// committed so the caller can drop the main connection's per-item metadata
/// cache entries (IDatabaseManager::invalidateMetadataCacheItem — the hook
/// that exists for external writers on their own connection).
struct Outcome {
  int written = 0;          ///< rows the action changed AND that were committed
  QStringList writtenPaths; ///< paths of those rows, for cache invalidation
  bool ok = false;          ///< pipeline completed and the transaction committed
  bool canceled = false;    ///< cancel observed; everything was rolled back
};

/// Progress hook. Invoked ON THE WORKER THREAD, throttled to roughly one call
/// per 64 examined rows (plus a 0/total prologue and a total/total epilogue on
/// success); `done` counts examined rows, `total` the full row count. Callers
/// marshal to the GUI thread themselves (queued hop guarded by QPointer).
using ProgressFn = std::function<void(int done, int total)>;

/// Runs the pipeline against a private connection to @p dbPath. Rows in
/// @p paths without an item_metadata row are treated as empty stubs so the
/// action can create them (mirrors the per-item editor). Rows the action does
/// not change are skipped; per-row save failures are logged and skipped so one
/// bad row doesn't abort the batch. Cancellation is all-or-nothing: when
/// @p cancelRequested flips, the enclosing transaction rolls back and the
/// database is left untouched (canceled == true, written == 0). Returns a
/// default Outcome (ok == false) when @p dbPath is empty or the database
/// cannot be opened. Safe to run off the main thread.
[[nodiscard]] Outcome run(const QString &dbPath, const QString &collectionUuid,
                          BulkEdit::Action action, const QString &parameter,
                          const QStringList &paths,
                          const std::shared_ptr<std::atomic_bool> &cancelRequested,
                          const ProgressFn &progress = {});

} // namespace BulkEditService

#endif // KARTEND_UTILS_DB_BULKEDITSERVICE_H
