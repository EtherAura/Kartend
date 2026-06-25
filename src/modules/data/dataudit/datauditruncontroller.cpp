#include "datauditruncontroller.h"

#include <QAtomicInteger>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QtConcurrent>

#include "databaseschema.h"
#include "datauditcatalogue.h" // DatAudit::buildCatalogue / Catalogue
#include "datcache.h"          // DatCache::Store / defaultPath

using DatAudit::AuditOutput;
using DatAudit::AuditProgress;

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

bool DatAuditRunController::isRunning() const { return m_watcher.isRunning(); }

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
    QMetaObject::invokeMethod(
        this, [this, prog] { emit progress(prog); }, Qt::QueuedConnection);
  };

  const QStringList dats = req.datPaths;
  const QStringList roots = req.scanRoots;
  const QStringList regionPrefs = req.regionPrefs;
  const QStringList ignore = req.ignoreGlobs;
  const bool onePerGame = req.onePerGame;
  const bool ignoreHashCache = req.ignoreHashCache;
  const DatAudit::Layout layout = req.layout;
  const DatAudit::MergeMode mergeMode = req.mergeMode;
  auto cancel = m_cancel;

  auto future = QtConcurrent::run([dats, roots, cancel, regionPrefs, ignore, onePerGame, layout,
                                   mergeMode, ignoreHashCache, progressFn]() -> AuditOutput {
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
      hashDb.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return out;
  });
  m_watcher.setFuture(future);
}

void DatAuditRunController::onWatcherFinished() { emit finished(m_watcher.result()); }
