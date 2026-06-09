#ifndef DATAUDITRUNNER_H
#define DATAUDITRUNNER_H

#include <atomic>
#include <functional>
#include <memory>

#include <QList>
#include <QString>
#include <QStringList>

#include "datauditcatalogue.h"
#include "dataudittypes.h"

class QSqlDatabase;

namespace DatCache {
class Store;
}

namespace DatAudit {

/// Scope of a scan. The DAT catalogue is supplied separately (it is reusable
/// across runs and comparatively expensive to build).
struct AuditOptions {
  QStringList scanRoots;   ///< Folders walked recursively.
  QStringList ignoreGlobs; ///< Wildcards matched on basename; matches are skipped.
  QStringList datPaths;    ///< The DAT files, so a DAT sitting inside a scan root isn't audited.
};

/// Progress tick delivered on the calling thread during a run.
struct AuditProgress {
  int filesDone = 0;
  int filesTotal = 0;
  QString currentFile;
};

/// Result of a run (or of classify()).
struct AuditOutput {
  QList<AuditRow> rows;
  AuditSummary summary;
  bool cancelled = false;
};

/// One scanned file paired with its hashes — the pure input to classify().
struct ScannedFile {
  QString path;
  QString crc;
  QString md5;
  QString sha1;
  qint64 size = -1;
  bool readOk = true; ///< false => unreadable; classify() marks it Corrupt.
};

using ProgressFn = std::function<void(const AuditProgress &)>;
using CancelToken = std::shared_ptr<std::atomic<bool>>;

/// Build a Catalogue by ingesting each DAT through `cache` and enumerating its
/// records. A DAT that fails to open/parse is skipped and its path appended to
/// `failedDats` (when non-null) so the caller can warn without aborting the
/// whole audit. An empty list yields an empty catalogue.
[[nodiscard]] Catalogue buildCatalogue(DatCache::Store &cache, const QStringList &datPaths,
                                       QStringList *failedDats = nullptr);

/// Classify already-hashed files against a catalogue. Pure — no I/O, no
/// threads — so it is exhaustively unit-testable. Emits one row per file
/// (Have / WrongName / WrongHash / Duplicate / Unknown / Corrupt) plus one
/// Missing row per catalogue entry no file satisfied, and fills
/// totalCatalogue / totalFiles on the summary.
[[nodiscard]] AuditOutput classify(const Catalogue &catalogue, const QList<ScannedFile> &files);

/// Full scan: enumerate `opts.scanRoots`, hash each file (through FileHashCache
/// when `cacheDb` is non-null, else RomHasher directly; the hashing is fanned
/// out across the global thread pool while cache reads/writes stay on the
/// calling thread for QSqlDatabase safety), and classify against `catalogue`.
/// Reports progress via `progress` and aborts promptly when `cancel` flips
/// true — the partial output is returned with `cancelled = true`. Runs
/// synchronously on the calling thread; callers keeping a UI responsive run it
/// on a worker thread. `cacheDb` (if given) is used only from the calling
/// thread.
[[nodiscard]] AuditOutput run(const Catalogue &catalogue, const AuditOptions &opts,
                              QSqlDatabase *cacheDb, const CancelToken &cancel,
                              const ProgressFn &progress);

} // namespace DatAudit

#endif // DATAUDITRUNNER_H
