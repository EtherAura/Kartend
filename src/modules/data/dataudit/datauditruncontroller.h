#ifndef DATAUDITRUNCONTROLLER_H
#define DATAUDITRUNCONTROLLER_H

// Stage 3 of the DatAuditDialog decomposition (Kartend-ahf3d): the off-thread
// audit-run orchestration, extracted off the QDialog. onRun's worker — build the
// DAT catalogue, open the file-hash-cache DB connection on the worker thread,
// run DatAudit::run, and marshal throttled progress back — lives here. The
// controller owns the QFutureWatcher and the shared cancel flag, and emits
// progress()/finished() on its own (UI) thread; the dialog keeps pure view
// concerns (progress bar, summary, snapshot persistence) and connects to those
// signals.
//
// Named *RunController, not DatAuditRunner, because namespace DatAudit already
// owns datauditrunner.h — the stateless audit ENGINE (DatAudit::run) this
// orchestrates.

#include <atomic>
#include <memory>

#include <QFutureWatcher>
#include <QObject>
#include <QStringList>

#include "datauditlayout.h" // DatAudit::Layout
#include "datauditrunner.h" // DatAudit::AuditOutput / AuditProgress
#include "dataudittypes.h"  // DatAudit::MergeMode

class DatAuditRunController : public QObject {
  Q_OBJECT

public:
  /// Everything the worker needs, snapshotted on the UI thread before launch so
  /// the worker touches no dialog/profile state.
  struct Request {
    QStringList datPaths;
    QStringList scanRoots;
    QStringList regionPrefs;
    QStringList ignoreGlobs;
    bool onePerGame = false;
    bool ignoreHashCache = false;
    DatAudit::Layout layout = DatAudit::Layout::Unknown;
    DatAudit::MergeMode mergeMode = DatAudit::MergeMode::Split;
  };

  explicit DatAuditRunController(QObject *parent = nullptr);
  ~DatAuditRunController() override;

  /// Launches the audit on a worker thread. No-op if a run is already in flight.
  void start(const Request &request);
  /// Asks the in-flight run to stop at the next cancellation check.
  void cancel();
  [[nodiscard]] bool isRunning() const;

signals:
  /// Throttled (~200/run) progress, already marshalled to this object's thread.
  void progress(const DatAudit::AuditProgress &p);
  /// The completed audit output (carries cancelled + failedDats).
  void finished(const DatAudit::AuditOutput &out);

private:
  void onWatcherFinished();

  QFutureWatcher<DatAudit::AuditOutput> m_watcher;
  std::shared_ptr<std::atomic<bool>> m_cancel;
};

#endif // DATAUDITRUNCONTROLLER_H
