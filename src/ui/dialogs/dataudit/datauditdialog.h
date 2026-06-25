#ifndef DATAUDITDIALOG_H
#define DATAUDITDIALOG_H

#include <atomic>
#include <functional>
#include <memory>

#include <QDialog>
#include <QFutureWatcher>

#include "datauditauditpage.h"
#include "datauditdownloadpage.h"
#include "datauditdownloadservice.h"
#include "datauditlibrarypage.h"
#include "datauditprofilestore.h"

class QHideEvent;
class QLabel;
class QListWidget;
class QSplitter;
class QStackedWidget;
class DatAuditBrowserPage;
struct CollectionConfig;

/// The DAT manager window (Kartend-m6qsb.17). A standalone, non-modal window in
/// the Settings dialog's visual idiom: a left nav rail + QStackedWidget content
/// pane with a context header, native Breeze palette, and geometry persistence.
/// Four pages, each its own QWidget (Kartend-oa0lu):
///   - Audit:    DatAuditAuditPage — profile CRUD, DAT/scan-root inputs, layout
///               detection, run/cancel + results table, fix + exports.
///   - Library:  DatAuditLibraryPage — the watched DAT-library folder + review.
///   - Download: DatAuditDownloadPage — fetch No-Intro / Redump packs + import.
///   - Browser:  DatAuditBrowserPage — the RomVault-style read-only tree.
///
/// This window is now the nav shell plus the cross-page orchestration: it owns
/// the shared DatAuditProfileStore + DatAuditDownloadService, the library-path /
/// import / scraper hooks, the update-check watcher, and geometry; each page
/// owns its own widgets and emits the intent the dialog wires up.
class DatAuditDialog : public QDialog {
  Q_OBJECT
public:
  explicit DatAuditDialog(QWidget *parent = nullptr);
  ~DatAuditDialog() override;

  /// Borrow the live collection list (owned by MainWindow) so the audit page's
  /// profile editor can offer an optional "Linked collection" picker and the
  /// browser can group profiles under their collection. Null/empty is fine.
  /// Called by DatAuditController on every open so the list stays fresh.
  void setCollections(QList<CollectionConfig> *collections);

  /// Aim the window at a specific collection (Kartend-4mqkof, launched from
  /// collection settings). Selects the audit profile already linked to
  /// @p collectionUuid if one exists; otherwise seeds an *unsaved* working
  /// profile from the collection (name + DAT files + the media dir as a scan
  /// root + the collection link) for the user to review and Save/Run. Nothing
  /// is persisted until the user saves. Delegates to the audit page.
  void openForCollection(const QString &collectionUuid, const QString &collectionName,
                         const QString &mediaDir, const QStringList &datPaths);

  /// Hook for the DAT-library review (Kartend-m6qsb.5) — opens the proposal
  /// review, which lives on DatAuditController. Unset = library actions hidden
  /// (standalone/test construction).
  void setLibraryOpener(std::function<void()> opener);

  /// Library-folder accessors (Kartend-m6qsb.5/.16): read the configured DAT
  /// library path and persist a new one. Used by the Library and Download
  /// pages — the download extracts packs into this folder. Unset hides the
  /// Download page (no destination to import into).
  void setLibraryPathAccessors(std::function<QString()> getter,
                               std::function<void(const QString &)> setter);

  /// Provider for the global default quarantine folder (Settings → General → DAT
  /// Auditor). The Fix dialog prefills its quarantine field from the audited
  /// profile's per-collection root, falling back to this. Unset = no global
  /// default (standalone/test). Forwarded to the audit page.
  void setQuarantineDefaultProvider(std::function<QString()> provider);

  /// Hook for "Import DAT pack" (Kartend-m6qsb.22): given a chosen zip / folder
  /// / .dat path, the controller imports it into the library and opens the
  /// review. Unset hides the import buttons (standalone/test).
  void setImportHandler(std::function<void(const QString &)> handler);

  /// Hook to open the scraper scoped to a collection (Kartend-m6qsb.27): after
  /// a Fix renames files to canonical names, the audit page offers to re-scrape
  /// the linked collection (canonical names match far better). Unset = no
  /// offer (standalone/test, or an unlinked audit). Forwarded to the audit page.
  void setScraperOpener(std::function<void(const QString &collectionUuid)> opener);

protected:
  /// Persist window geometry + splitter state on close (the controller hides,
  /// not destroys, this window, so the next open restores the user's layout).
  void hideEvent(QHideEvent *event) override;

private slots:
  void onImportZip();
  void onImportFolder();
  // Download-page update-check (Kartend-oa0lu): the download flow lives in
  // DatAuditDownloadPage; the dialog keeps the library-button update-check.
  void onCheckUpdates();
  void onCheckUpdatesFinished();

private:
  // Nav-shell assembly (Kartend-m6qsb.17).
  void wireDownloadActions();
  void addNavEntry(const QString &label, const QStringList &iconNames, int pageIndex);
  void onNavRowChanged();
  void applyUniformSizing();
  void persistGeometry();
  void restoreGeometry_();

  // The four pages (Kartend-oa0lu): each owns its widgets + flow; the dialog
  // wires their intent signals and shares the store/service.
  DatAuditAuditPage *m_auditPage = nullptr;
  DatAuditLibraryPage *m_libraryPage = nullptr;
  DatAuditDownloadPage *m_downloadPage = nullptr;
  /// RomVault-style browser page (Kartend-34lab). Lazily refreshed the first
  /// time the user switches to it (and on each subsequent switch).
  DatAuditBrowserPage *m_browserPage = nullptr;

  std::function<void()> m_libraryOpener;
  QList<CollectionConfig> *m_collections = nullptr; ///< Borrowed from MainWindow; not owned.

  // Nav shell (Kartend-m6qsb.17).
  QSplitter *m_splitter = nullptr;
  QListWidget *m_nav = nullptr;
  QStackedWidget *m_pages = nullptr;
  QLabel *m_contextIcon = nullptr;
  QLabel *m_contextTitle = nullptr;

  // Library / update-check (Kartend-oa0lu): the widgets live on the library
  // page; the dialog keeps the update-check watcher + the library-path/import
  // accessors shared across pages.
  QFutureWatcher<DatAuditDownloadService::UpdateResult> m_updateWatcher;
  std::shared_ptr<std::atomic<bool>> m_updateCancel;
  std::function<QString()> m_getLibraryPath;
  std::function<void(const QString &)> m_importPack;

  /// SQLite access for profiles / result snapshots / DAT-library provenance
  /// (Kartend-ahf3d stage 2). Declared before m_downloadService so it is
  /// constructed first — the download service's provenance accessor (wired in
  /// the ctor) reads through it — and injected by reference into m_auditPage.
  DatAuditProfileStore m_profileStore;
  /// Off-thread download + provenance + update-check orchestration extracted
  /// from this dialog (Kartend-ahf3d stage 1). Constructed in the ctor with the
  /// provenance accessor backed by m_profileStore; see datauditdownloadservice.h.
  std::unique_ptr<DatAuditDownloadService> m_downloadService;
};

#endif // DATAUDITDIALOG_H
