#include "datauditruncontroller.h"

#include <QAtomicInteger>
#include <QDateTime>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QtConcurrent>

#include "databaseschema.h"
#include "datauditcatalogue.h" // DatAudit::buildCatalogue / Catalogue
#include "datauditprofile.h"   // DatAuditProfile::ResultRow / touchLastScan / replaceResults
#include "datcache.h"          // DatCache::Store / defaultPath
#include "errorutils.h"

using DatAudit::AuditOutput;
using DatAudit::AuditProgress;

namespace {

/// Map the engine's rows to the persisted snapshot shape (moved off the audit
/// page's onAuditFinished together with the writes, Kartend-h7xnr.5).
QList<DatAuditProfile::ResultRow> buildResultRows(const QList<DatAudit::AuditRow> &auditRows) {
  QList<DatAuditProfile::ResultRow> rows;
  rows.reserve(auditRows.size());
  for (const DatAudit::AuditRow &r : auditRows) {
    DatAuditProfile::ResultRow row;
    // File-backed rows key by path (unique per enumeration); entry-only
    // rows (Missing) key by the canonical name. Prefixes keep the two key
    // spaces from colliding in the shared primary key.
    row.entryKey = r.filePath.isEmpty() ? QStringLiteral("entry:") + r.expectedName
                                        : QStringLiteral("file:") + r.filePath;
    row.status = static_cast<int>(r.status);
    row.filePath = r.filePath;
    row.detail = r.expectedName;
    // Source DAT + game + MIA so the browser's tree/game-list rollups are
    // grouped queries (Kartend-34lab, schema v22).
    row.sourceName = r.sourceName;
    row.gameName = r.gameName;
    row.mia = r.mia;
    row.zipIndex = r.zipIndex; // archive member index for the ZipIndex column
    rows.append(row);
  }
  return rows;
}

} // namespace

DatAuditRunController::DatAuditRunController(QObject *parent) : QObject(parent) {
  connect(&m_watcher, &QFutureWatcher<AuditOutput>::finished, this,
          &DatAuditRunController::onWatcherFinished);
}

DatAuditRunController::~DatAuditRunController() {
  // Never let a worker outlive the controller: ask it to stop and block until it
  // returns. waitForFinished does not pump events, so the queued finished()
  // never fires against a half-destroyed owner during teardown.
  if (m_cancel) {
    m_cancel->store(true);
  }
  if (m_watcher.isRunning()) {
    m_watcher.waitForFinished();
  }
}

bool DatAuditRunController::isRunning() const {
  return m_watcher.isRunning();
}

void DatAuditRunController::cancel() {
  if (m_cancel) {
    m_cancel->store(true);
  }
}

void DatAuditRunController::start(const Request &req) {
  if (m_watcher.isRunning()) {
    return;
  }
  m_cancel = std::make_shared<std::atomic<bool>>(false);

  // Real progress (Kartend-m6qsb.7): the engine ticks on the worker thread;
  // updates marshal to this object's (UI) thread via a queued invoke, throttled
  // to ~200 posts per run so a 50k-file audit doesn't flood the event queue.
  auto lastPosted = std::make_shared<std::atomic<int>>(-1);
  auto progressFn = [this, lastPosted](const AuditProgress &prog) {
    const int step = qMax(1, prog.filesTotal / 200);
    const int last = lastPosted->load(std::memory_order_relaxed);
    if (prog.filesDone != prog.filesTotal && prog.filesDone - last < step) {
      return;
    }
    lastPosted->store(prog.filesDone, std::memory_order_relaxed);
    QMetaObject::invokeMethod(this, [this, prog] { emit progress(prog); }, Qt::QueuedConnection);
  };

  const QStringList dats = req.datPaths;
  const QStringList roots = req.scanRoots;
  const QStringList regionPrefs = req.regionPrefs;
  const QStringList ignore = req.ignoreGlobs;
  const bool onePerGame = req.onePerGame;
  const bool ignoreHashCache = req.ignoreHashCache;
  const DatAudit::Layout layout = req.layout;
  const DatAudit::MergeMode mergeMode = req.mergeMode;
  const qint64 persistId = req.persistProfileId;
  auto cancel = m_cancel;

  // `this` is safe to capture: the destructor blocks on waitForFinished, so
  // the worker can never outlive the controller (same guarantee progressFn's
  // queued invokes rely on).
  auto future =
      QtConcurrent::run([this, dats, roots, cancel, regionPrefs, ignore, onePerGame, layout,
                         mergeMode, ignoreHashCache, persistId, progressFn]() -> AuditOutput {
        // Both DB connections below are created, used, and removed on THIS worker
        // thread, satisfying QSqlDatabase's thread affinity.
        DatCache::Store cache(DatCache::defaultPath());
        QStringList failed;
        DatAudit::Catalogue cat = DatAudit::buildCatalogue(cache, dats, &failed);
        DatAudit::AuditOptions opts;
        opts.scanRoots = roots;
        opts.datPaths = dats;
        opts.ignoreGlobs = ignore;
        opts.regionPrefs = regionPrefs;
        opts.onePerGame = onePerGame;
        opts.layout = layout;
        opts.mergeMode = mergeMode;
        opts.ignoreHashCache = ignoreHashCache;

        // Open a main-DB connection for the file-hash cache so re-audits skip
        // re-hashing unchanged files (the v17 file_hash_cache table already exists;
        // the app applied migrations at startup). WAL lets this coexist with the
        // DatabaseManager's own connection.
        static QAtomicInteger<quint64> hashConnCounter{0};
        const QString conn =
            QStringLiteral("dataudit_hashcache_%1").arg(hashConnCounter.fetchAndAddRelaxed(1));
        AuditOutput out;
        {
          QSqlDatabase hashDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
          QSqlDatabase *cacheDb = nullptr;
          if (DatabaseSchema::openConnection(
                  hashDb, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))) {
            DatabaseSchema::applyConnectionPragmas(hashDb);
            cacheDb = &hashDb;
          }
          out = DatAudit::run(cat, opts, cacheDb, cancel, progressFn);
          // Carry the failed-DAT list back to the GUI thread so it isn't silently
          // dropped — the audit ran against a partial catalogue otherwise
          // (Kartend-2zcrz).
          out.failedDats = failed;
          // Persist the snapshot + last-scan stamp for a SAVED profile right here
          // on the worker (Kartend-h7xnr.5): the dat_audit_* tables live in the
          // same app DB the hash-cache connection is already open on, and doing
          // the DELETE + per-row INSERT transaction GUI-side stalled the dialog
          // at scan finish on large catalogues. A cancelled audit is a partial
          // statement of the world — never persisted.
          if (persistId >= 0 && !out.cancelled) {
            if (cacheDb != nullptr) {
              const qint64 now = QDateTime::currentMSecsSinceEpoch();
              bool ok = true;
              if (auto stamped = DatAuditProfile::touchLastScan(*cacheDb, persistId, now);
                  stamped.isError()) {
                ErrorUtils::logError(stamped.error());
                ok = false;
              }
              if (auto replaced = DatAuditProfile::replaceResults(*cacheDb, persistId,
                                                                  buildResultRows(out.rows));
                  replaced.isError()) {
                ErrorUtils::logError(replaced.error());
                ok = false;
              }
              if (ok) {
                // Queued to the controller's (UI) thread; posted before this
                // lambda returns, so it is always delivered before finished().
                QMetaObject::invokeMethod(
                    this, [this, persistId, now] { emit snapshotPersisted(persistId, now); },
                    Qt::QueuedConnection);
              }
            } else {
              ErrorUtils::logError(ErrorUtils::ErrorContext::error(
                  ErrorUtils::ErrorCode::DatabaseConnectionFailed,
                  "Audit finished but the result snapshot could not be persisted",
                  "DatAuditRunController"));
            }
          }
          hashDb.close();
        }
        QSqlDatabase::removeDatabase(conn);
        return out;
      });
  m_watcher.setFuture(future);
}

void DatAuditRunController::onWatcherFinished() {
  emit finished(m_watcher.result());
}
