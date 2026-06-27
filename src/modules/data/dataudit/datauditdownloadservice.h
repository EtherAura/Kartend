#ifndef DATAUDITDOWNLOADSERVICE_H
#define DATAUDITDOWNLOADSERVICE_H

// Stage-1 of the DatAuditDialog decomposition (Kartend-ahf3d): the
// download + provenance + check-for-updates orchestration extracted off the
// QDialog so it is unit/integration-testable without instantiating the dialog.
//
// The dialog keeps the view concerns (input gathering, busy/progress UI, the
// confirm prompts, and its own QFutureWatchers) and calls this service instead
// of invoking NoIntroDownload::run / RedumpDownload::run + QtConcurrent::run
// directly. The off-thread compute (performDownload, the update-check worker)
// lives here; the SQLite provenance reads/writes stay on the UI thread and reach
// the DB through an injected ProvenanceAccess seam — so this service never opens
// a QSqlDatabase itself. A later stage (DatAuditProfileStore) replaces the
// injected accessor with a real store; until then the dialog wires it to its
// existing transient-connection helper.

#include <functional>

#include <QFuture>
#include <QList>
#include <QString>
#include <QStringList>

#include "datlibrarystate.h"   // DatLibraryState::Provenance
#include "nointrodownloader.h" // NoIntroDownload::Options / CancelToken / ProgressFn
#include "nointroparse.h"      // NoIntroParse::DailyForm
#include "redumpparse.h"       // RedumpParse::System

class DatAuditDownloadService {
public:
  /// Off-thread outcome of one source's download + extract pipeline, carrying
  /// the provenance fields a later "check for updates" needs.
  struct Outcome {
    bool ok = false;
    QString error;
    QString packName;
    int datCount = 0;
    QStringList datPaths; ///< Extracted DAT file paths.
    QString source;       ///< "nointro" / "redump".
    QString slug;         ///< Redump system slug (empty for No-Intro).
    int systemId = 0;     ///< No-Intro system id (0 for Redump).
    QString version;      ///< No-Intro pack date / Redump header version at fetch time.
  };

  /// Off-thread outcome of a "check for updates" run: how many tracked
  /// catalogues were checked, and the re-download outcome of each found outdated.
  struct UpdateResult {
    int checked = 0;
    QList<Outcome> downloads; ///< One per outdated catalogue re-downloaded.
  };

  /// Off-thread outcome of the No-Intro daily-form probe — the form the user
  /// picks sets from, or a user-facing error. Formerly
  /// DatAuditDialog::DailyFormOutcome (Kartend-ahf3d stage 4).
  struct DailyFormOutcome {
    bool ok = false;
    QString error;
    NoIntroParse::DailyForm form;
  };

  /// Off-thread outcome of the redump.org systems-list fetch. Formerly
  /// DatAuditDialog::RedumpSystemsOutcome (Kartend-ahf3d stage 4).
  struct RedumpSystemsOutcome {
    bool ok = false;
    QString error;
    QList<RedumpParse::System> systems;
  };

  /// One download request, gathered by the dialog from the Download page widgets
  /// (or synthesised per outdated catalogue by the update worker).
  struct Request {
    bool redump = false;
    NoIntroDownload::Options niOpts;
    QString redumpSlug; ///< Redump system slug (redump only).
    QString packDate;   ///< No-Intro pack date (the No-Intro revision; redump reads its own).
    QString libraryDir; ///< Destination DAT-library folder.
  };

  /// Injected SQLite provenance seam (the dialog implements both via its
  /// transient-connection helper; stage-2's DatAuditProfileStore supplants it).
  struct ProvenanceAccess {
    std::function<QList<DatLibraryState::Provenance>()> loadAll;
    std::function<void(const DatLibraryState::Provenance &)> record;
  };

  explicit DatAuditDownloadService(ProvenanceAccess access);

  /// Download + extract one source's DAT(s) into `req.libraryDir`, filling the
  /// provenance fields. Pure (no `this`, no UI, no DB) so it runs on the worker
  /// thread and is directly testable. `progress` may be null.
  [[nodiscard]] static Outcome performDownload(const Request &req,
                                               const NoIntroDownload::CancelToken &cancel,
                                               const NoIntroDownload::ProgressFn &progress);

  /// Off-thread wrapper around performDownload. The caller owns the watcher.
  [[nodiscard]] QFuture<Outcome> startDownload(const Request &req,
                                               const NoIntroDownload::CancelToken &cancel,
                                               const NoIntroDownload::ProgressFn &progress) const;

  /// Tracked provenance via the injected accessor (runs on the calling thread —
  /// load it before handing the list to checkUpdates' worker).
  [[nodiscard]] QList<DatLibraryState::Provenance> trackedProvenance() const;

  /// Off-thread "check for updates": probe each tracked catalogue's current
  /// revision, re-download the outdated ones into `libraryDir`. Pure — the new
  /// provenance is returned in the result for the caller to persist.
  [[nodiscard]] QFuture<UpdateResult>
  checkUpdates(const QList<DatLibraryState::Provenance> &tracked, const QString &libraryDir,
               const NoIntroDownload::CancelToken &cancel) const;

  /// Off-thread fetch of the redump.org systems list (network-bound; runtime-
  /// gated like performDownload). The caller owns the watcher.
  [[nodiscard]] QFuture<RedumpSystemsOutcome>
  startRedumpSystemsFetch(const NoIntroDownload::CancelToken &cancel) const;

  /// Off-thread fetch of a No-Intro system's daily-download form (network-bound).
  /// The caller owns the watcher.
  [[nodiscard]] QFuture<DailyFormOutcome>
  startDailyFormFetch(int systemId, const NoIntroDownload::CancelToken &cancel) const;

  /// Persist provenance for every DAT in a successful outcome, via the injected
  /// accessor (DB write stays on the caller's thread).
  void recordProvenance(const Outcome &o) const;

private:
  ProvenanceAccess m_access;
};

#endif // DATAUDITDOWNLOADSERVICE_H
