#ifndef KARTEND_UTILS_DB_ARTWORKWIZARDSERVICE_H
#define KARTEND_UTILS_DB_ARTWORKWIZARDSERVICE_H

#include <functional>

#include <QList>
#include <QString>
#include <QStringList>

// Off-thread preparation for the "assign missing artwork" wizard. Opens a
// private SQLite connection to the database FILE (the same own-connection shape
// BulkEditService::run and StatisticsService::gather use — QSqlDatabase
// connections are thread-affine, so the worker must never touch the GUI-thread
// connection), enumerates the collection's items whose artwork_path is empty,
// and snapshots the artwork directory's file listing. Pure over its inputs (a
// db file path + collection uuid + artwork dir) and touches no GUI or shared
// state, so LibraryToolsController can run it via QtConcurrent::run and the prep
// can be exercised against a real on-disk database in tests. Read-only: it never
// writes, so there is no transaction and cancellation just abandons the result.

namespace ArtworkWizardService {

/// One item awaiting artwork: the item's file path and the display name the
/// wizard ranks candidates against. UI-free (no dialog types) so the service
/// stays in the db layer.
struct Item {
  QString filePath;
  QString itemName;
};

/// Result of prepare(). `dbOk` is false when the db path is empty or the
/// database / query could not be opened — the caller surfaces that distinctly
/// from an empty queue (every item already has artwork).
struct Prepared {
  QList<Item> queue;          ///< items with empty / NULL artwork_path
  QStringList directoryFiles; ///< absolute paths of files in the artwork dir
  bool dbOk = false;          ///< connection + enumeration query succeeded
};

/// Progress hook. Invoked ON THE WORKER THREAD during the O(n) item scan,
/// throttled (plus a 0/total prologue and total/total epilogue); `done` counts
/// examined rows, `total` the full row count. Callers marshal to the GUI thread
/// themselves (queued hop guarded by QPointer).
using ProgressFn = std::function<void(int done, int total)>;

/// Opens a private connection to @p dbPath, collects items in @p collectionUuid
/// whose artwork_path is empty (name-ordered, matching the app's enumeration),
/// and — only when that queue is non-empty — snapshots the files in
/// @p artworkDir. Returns a default Prepared (dbOk == false) when @p dbPath is
/// empty or the database cannot be opened. Safe to run off the main thread.
[[nodiscard]] Prepared prepare(const QString &dbPath, const QString &collectionUuid,
                               const QString &artworkDir, const ProgressFn &progress = {});

} // namespace ArtworkWizardService

#endif // KARTEND_UTILS_DB_ARTWORKWIZARDSERVICE_H
