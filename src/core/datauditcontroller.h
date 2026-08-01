#ifndef DATAUDITCONTROLLER_H
#define DATAUDITCONTROLLER_H

#include <functional>

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include "datcollectionmatch.h"
#include "datlibraryscan.h"

QT_BEGIN_NAMESPACE
class QWidget;
class QFileSystemWatcher;
class QTimer;
QT_END_NAMESPACE

class DatAuditDialog;
struct CollectionConfig;

template <typename T> class QList;

/// Owns the DAT Audit sub-window, mirroring ScraperController's role for the
/// scraper: the dialog is constructed lazily on first open and reused across
/// opens (hidden, not destroyed, on close), so it survives the user closing
/// the window. Lives on MainWindow as a unique_ptr(nullptr-parent) member.
///
/// The audit dialog is self-contained (it picks its own DAT files + scan
/// folders and runs the engine off-thread); beyond the parent window it borrows
/// MainWindow's live collection list so the profile editor can offer an
/// optional "Linked collection" picker (Kartend-x9mkif.3). Profile-driven
/// launches remain a later follow-up.
struct DatAuditControllerContext {
  std::function<QWidget *()> getParentWindow;

  /// The live collection list (each collection's UUID is derived from its name
  /// + media dir), borrowed from MainWindow. Null/empty is fine — the picker
  /// then offers only "(none)".
  std::function<QList<CollectionConfig> *()> getCollections;

  /// DAT-library hooks (Kartend-m6qsb.5). All optional; unset = library off.
  std::function<QString()> getDatLibraryPath;
  std::function<void(const QString &root)> saveDatLibraryPath;
  /// Global default quarantine folder for the Fix dialog. Optional; unset = no
  /// global default (the Fix field stays blank unless a profile sets its own).
  std::function<QString()> getQuarantineDefaultDir;
  /// Append a DAT to the collection's datFilePaths and persist the INI —
  /// linked audit profiles pick the change up automatically because their
  /// lists are derived (Kartend-m6qsb.2).
  std::function<void(const QString &collectionUuid, const QString &datPath)> attachDatToCollection;
  /// Create a new collection for `datPath` (prompting the user) with the DAT
  /// attached; returns the new collection's uuid, or empty when cancelled.
  /// Powers the review dialog's "Add to new collection…" choice
  /// (Kartend-m6qsb.18). get-prefixed per the controller-ctx accessor rule
  /// (non-void thunk) even though it also creates.
  std::function<QString(const QString &datPath)> getNewCollectionForDat;
  /// Open the scraper scoped to the collection with this uuid — used after a
  /// Fix renames files to canonical names, to re-scrape them (Kartend-m6qsb.27).
  std::function<void(const QString &collectionUuid)> openScraperForCollection;
  /// Non-modal surfacing for "new catalogues matched" (status bar).
  std::function<void(const QString &message)> showStatusMessage;
};

class DatAuditController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DatAuditController)
public:
  explicit DatAuditController(QObject *parent = nullptr);
  ~DatAuditController() override;

  void setContext(const DatAuditControllerContext &context);

public slots:
  /// Show (or raise) the DAT Audit window, constructing it on first call.
  void openDialog();

  /// Show the DAT Audit window aimed at a specific collection (Kartend-4mqkof,
  /// launched from collection settings): selects the linked audit profile if
  /// one exists, otherwise seeds an unsaved working profile from the collection.
  /// See DatAuditDialog::openForCollection.
  void openForCollection(const QString &collectionUuid, const QString &collectionName,
                         const QString &mediaDir, const QStringList &datPaths);

  /// Async startup pass over the DAT-library folder (Kartend-m6qsb.5): probe
  /// headers, rank collection matches, and surface a status-bar hint when
  /// proposals exist. No-op when no library folder is configured. Runs the
  /// scan on the global pool — it is a bounded header-probe walk, never an
  /// ingest — and never applies anything (confirm-only).
  void startupLibraryScan();

  /// Open the modal review dialog over the latest proposals (rescanning
  /// fresh when the startup scan found none or hasn't run).
  void openLibraryReview();

  /// Import a DAT zip / folder / file into the library (prompting for a
  /// library folder if none is set) and open the review (Kartend-m6qsb.22).
  void importDatPack(const QString &sourcePath);

private:
  /// Build the dialog on first use and refresh its borrowed collection list.
  /// Does not show — callers raise it after any per-open setup.
  DatAuditDialog *ensureDialog();
  /// CollectionInfo projections of the live collection list (uuid computed
  /// the same way every other consumer keys on).
  [[nodiscard]] QList<DatCollectionMatch::CollectionInfo> collectionInfos() const;
  /// Synchronous scan of `root` against the live collections, dismissals
  /// filtered via a short-lived app-DB connection.
  [[nodiscard]] DatLibraryScan::ScanResult scanLibrary(const QString &root) const;

  /// Persist a new library root through the host, then retarget the live
  /// filesystem watch at it. Used wherever the path changes so watching follows.
  void persistLibraryPath(const QString &root);
  /// Point the QFileSystemWatcher at the current library folder (Kartend-m6qsb.10):
  /// drop the old paths, watch the root + its immediate subfolders if it exists.
  /// Resets the notified-proposal set when the root changes so a new library
  /// notifies fresh. No-op when no library is configured.
  void updateLibraryWatch();

  /// Kartend-j6a00: while an application-modal window (the Settings dialog)
  /// is up, the audit dialog is transient-parented to it so Qt exempts it
  /// from the modal input block. Non-null exactly while adopted; guards
  /// duplicate reparent-back connections when the dialog is opened twice
  /// from the same modal session.
  QPointer<QWidget> m_modalParent;
  void adoptModalParentIfNeeded(DatAuditDialog *dialog);
  void restoreBaseParent();

  DatAuditControllerContext m_ctx;
  /// Cached across opens (hidden, not destroyed, on close). QPointer so a
  /// destruction elsewhere (parent teardown, a future WA_DeleteOnClose)
  /// nulls the cache and ensureDialog() self-heals by recreating instead of
  /// reusing a dangling pointer.
  QPointer<DatAuditDialog> m_dialog;
  QFutureWatcher<DatLibraryScan::ScanResult> m_libraryWatcher;
  DatLibraryScan::ScanResult m_startupProposals;

  // Live filesystem watching of the DAT-library folder (Kartend-m6qsb.10).
  QFileSystemWatcher *m_libraryFsWatcher = nullptr;
  QTimer *m_libraryDebounce = nullptr; ///< Coalesces bursts (multi-file copies) into one rescan.
  QString m_watchedRoot;               ///< Currently-watched library root ('' = none).
  /// canonicalPath+mtime of proposals already surfaced this session, so a live
  /// rescan only notifies about genuinely-new/changed catalogues ("doesn't nag").
  QSet<QString> m_notifiedKeys;
};

#endif // DATAUDITCONTROLLER_H
