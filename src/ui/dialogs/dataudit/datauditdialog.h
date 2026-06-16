#ifndef DATAUDITDIALOG_H
#define DATAUDITDIALOG_H

#include <atomic>
#include <functional>
#include <memory>

#include <QDialog>
#include <QFutureWatcher>

#include "datauditlayout.h"
#include "datauditprofile.h"
#include "datauditrunner.h"
#include "datlibrarystate.h"
#include "nointrodownloader.h"
#include "nointroparse.h"
#include "redumpparse.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTableView;
class QSortFilterProxyModel;
class QVBoxLayout;
class QHideEvent;
class QModelIndex;
class QPoint;
class DatAuditBrowserPage;
struct CollectionConfig;

namespace DatAudit {
class DatAuditModel;
}

/// The DAT manager window (Kartend-m6qsb.17). A standalone, non-modal window
/// in the Settings dialog's visual idiom: a left nav rail + QStackedWidget
/// content pane with a context header, native Breeze palette, QGroupBox /
/// QFormLayout sections, and geometry persistence. Three pages:
///   - Audit:    profile CRUD, DAT/scan-root lists (locked when collection-
///               linked), structure detection, run/cancel + progress, results
///               table + filter, fix + exports.
///   - Library:  the watched DAT-library folder + the confirm-only proposal
///               review (Kartend-m6qsb.5).
///   - Download: fetch No-Intro daily DAT packs and import them into the
///               library (Kartend-m6qsb.16).
///
/// The audit/library/download engines live in their own modules; this window
/// is the (reshelled) UI surface plus the off-thread orchestration the audit
/// and download need.
class DatAuditDialog : public QDialog {
  Q_OBJECT
public:
  explicit DatAuditDialog(QWidget *parent = nullptr);
  ~DatAuditDialog() override;

  /// Borrow the live collection list (owned by MainWindow) so the profile
  /// editor can offer an optional "Linked collection" picker. Null/empty is
  /// fine. Called by DatAuditController on every open so the list stays fresh.
  void setCollections(QList<CollectionConfig> *collections);

  /// Aim the window at a specific collection (Kartend-4mqkof, launched from
  /// collection settings). Selects the audit profile already linked to
  /// @p collectionUuid if one exists; otherwise seeds an *unsaved* working
  /// profile from the collection (name + DAT files + the media dir as a scan
  /// root + the collection link) for the user to review and Save/Run. Nothing
  /// is persisted until the user saves.
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

  /// Hook for "Import DAT pack" (Kartend-m6qsb.22): given a chosen zip / folder
  /// / .dat path, the controller imports it into the library and opens the
  /// review. Unset hides the import buttons (standalone/test).
  void setImportHandler(std::function<void(const QString &)> handler);

  /// Hook to open the scraper scoped to a collection (Kartend-m6qsb.27): after
  /// a Fix renames files to canonical names, the dialog offers to re-scrape
  /// the linked collection (canonical names match far better). Unset = no
  /// offer (standalone/test, or an unlinked audit).
  void setScraperOpener(std::function<void(const QString &collectionUuid)> opener);

protected:
  /// Persist window geometry + splitter state on close (the controller hides,
  /// not destroys, this window, so the next open restores the user's layout).
  void hideEvent(QHideEvent *event) override;

private slots:
  void onProfileSelected(int index);
  void onNewProfile();
  void onEditProfile();
  void onDuplicateProfile();
  void onRenameProfile();
  void onDeleteProfile();
  void onAddDat();
  void onRemoveDat();
  void onAddRoot();
  void onRemoveRoot();
  void onRun();
  void onCancel();
  void onAuditFinished();
  void onFilterChanged(int index);
  void onResultDoubleClicked(const QModelIndex &index);
  void onResultsContextMenu(const QPoint &pos);
  void onImportZip();
  void onImportFolder();
  void onFix();
  void onExportCsv();
  void onExportFixdat();
  void onExportMissList();
  // Download page (Kartend-m6qsb.16 No-Intro, .23 Redump)
  void onDownloadSourceChanged();
  void onLoadDailyForm();
  void onLoadDailyFormFinished();
  void onRedumpSystemsFetched();
  void onStartDownload();
  void onDownloadFinished();
  void onCancelDownload();
  void onCheckUpdates();
  void onCheckUpdatesFinished();

private:
  /// Off-thread outcome of the daily-form probe (carries the parsed form or a
  /// user-facing error).
  struct DailyFormOutcome {
    bool ok = false;
    QString error;
    NoIntroParse::DailyForm form;
  };

  /// Off-thread outcome of the download+import pipeline.
  struct DownloadOutcome {
    bool ok = false;
    QString error;
    QString packName;
    int datCount = 0;
    // Provenance for the extracted DATs (Kartend-m6qsb.26), recorded so a later
    // "check for updates" can ask the source for a newer revision.
    QStringList datPaths; ///< Extracted DAT file paths.
    QString source;       ///< "nointro" / "redump".
    QString slug;         ///< Redump system slug (empty for No-Intro).
    int systemId = 0;     ///< No-Intro system id (0 for Redump).
    QString version;      ///< No-Intro pack date / Redump header version at fetch time.
  };

  /// Off-thread outcome of the redump.org systems-list fetch.
  struct RedumpSystemsOutcome {
    bool ok = false;
    QString error;
    QList<RedumpParse::System> systems;
  };

  /// Off-thread outcome of a "check for updates" run (Kartend-m6qsb.31): how
  /// many provenanced catalogues were checked, and the re-download outcome of
  /// each one found outdated (carrying the new provenance to persist).
  struct UpdateRunResult {
    int checked = 0;
    QList<DownloadOutcome> downloads; ///< One per outdated catalogue re-downloaded.
  };

  /// Download + extract one source's DAT(s) into `lib`, returning the outcome
  /// with provenance fields filled. Shared by the manual Download page and the
  /// "check for updates" re-download. Static + pure-of-`this` so it runs on the
  /// worker thread. `progress` may be null.
  [[nodiscard]] static DownloadOutcome performDownload(bool redump, NoIntroDownload::Options niOpts,
                                                       const QString &redumpSlug,
                                                       const QString &packDate, const QString &lib,
                                                       const NoIntroDownload::CancelToken &cancel,
                                                       const NoIntroDownload::ProgressFn &progress);
  /// Persist provenance for every DAT in a successful download outcome.
  void recordDownloadProvenance(const DownloadOutcome &o);

  /// Which download source the page is targeting.
  enum class DownloadSource { NoIntro, Redump };
  [[nodiscard]] DownloadSource currentDownloadSource() const;
  void refreshDownloadButtonEnabled();

  void setBusy(bool busy);
  // Nav-shell assembly (Kartend-m6qsb.17).
  QWidget *buildAuditPage();
  QWidget *buildLibraryPage();
  QWidget *buildDownloadPage();
  /// RomVault-style browser page (Kartend-34lab). Lazily refreshed the first
  /// time the user switches to it (and on each subsequent switch).
  DatAuditBrowserPage *m_browserPage = nullptr;
  void addNavEntry(const QString &label, const QStringList &iconNames, int pageIndex);
  void onNavRowChanged();
  void applyUniformSizing();
  void persistGeometry();
  void restoreGeometry_();
  void setDownloadBusy(bool busy);
  void rebuildSetCheckboxes(); // from m_dailyForm
  void loadProfiles();
  void syncUiFromProfile();
  /// Overwrite m_currentProfile's DAT list + scan root from the live linked
  /// collection (the INI is canonical for linked profiles, Kartend-m6qsb.2).
  /// No-op when unlinked or when the link doesn't resolve (cached lists stay
  /// as a read-only fallback).
  void applyCollectionDerivation();
  /// Lock the DAT/scan-root editors while a linked profile is selected —
  /// those lists are derived, and edits here would be silently overwritten
  /// on the next open.
  void updateLinkedUiState();
  /// Probe the first scan root's folder structure and surface the result as
  /// a banner suggestion (Kartend-m6qsb.6). Confirm-based: nothing changes
  /// until the user clicks Apply. @p autoTriggered suppresses the
  /// nothing-to-report banner for the silent open-time probe.
  void runLayoutDetection(bool autoTriggered);
  void applyDetectedLayout();
  [[nodiscard]] DatAuditProfile::Profile uiProfile() const;
  bool persistProfile(DatAuditProfile::Profile &p); // insert when id<0, else update
  void selectProfileById(qint64 id);
  void updateSummary(const DatAudit::AuditSummary &summary);
  [[nodiscard]] QStringList datPaths() const;
  [[nodiscard]] QStringList scanRoots() const;
  [[nodiscard]] bool hasResults() const;
  /// AuditRow behind a view (proxy) index, mapped back to the source model.
  [[nodiscard]] const DatAudit::AuditRow *rowForProxyIndex(const QModelIndex &proxyIndex) const;
  void exportTo(const QString &caption, const QString &filter, const QByteArray &bytes);

  QComboBox *m_profileCombo = nullptr;
  QPushButton *m_newProfileButton = nullptr;
  QPushButton *m_editProfileButton = nullptr;
  QPushButton *m_duplicateProfileButton = nullptr;
  QPushButton *m_renameProfileButton = nullptr;
  QPushButton *m_deleteProfileButton = nullptr;
  QPushButton *m_libraryButton = nullptr;
  std::function<void()> m_libraryOpener;
  QListWidget *m_datList = nullptr;
  QListWidget *m_rootList = nullptr;
  QPushButton *m_addDatButton = nullptr;
  QPushButton *m_removeDatButton = nullptr;
  QPushButton *m_addRootButton = nullptr;
  QPushButton *m_removeRootButton = nullptr;
  QLabel *m_linkedHint = nullptr;
  QPushButton *m_detectLayoutButton = nullptr;
  QLabel *m_layoutBanner = nullptr;
  QPushButton *m_applyLayoutButton = nullptr;
  QPushButton *m_dismissLayoutButton = nullptr;
  DatAudit::LayoutDetection m_pendingDetection;
  QPushButton *m_runButton = nullptr;
  QPushButton *m_cancelButton = nullptr;
  QProgressBar *m_progress = nullptr;
  QLabel *m_summaryLabel = nullptr;
  QProgressBar *m_completionBar = nullptr;
  QComboBox *m_filterCombo = nullptr;
  QLineEdit *m_searchEdit = nullptr;
  QTableView *m_table = nullptr;
  QSortFilterProxyModel *m_proxy = nullptr;
  QPushButton *m_fixButton = nullptr;
  QPushButton *m_exportCsvButton = nullptr;
  QPushButton *m_exportFixdatButton = nullptr;
  QPushButton *m_exportMissButton = nullptr;

  DatAudit::DatAuditModel *m_model = nullptr;
  QFutureWatcher<DatAudit::AuditOutput> m_watcher;
  std::shared_ptr<std::atomic<bool>> m_cancel;
  DatAuditProfile::Profile m_currentProfile;        ///< Settings of the selected/working profile.
  QList<CollectionConfig> *m_collections = nullptr; ///< Borrowed from MainWindow; not owned.
  bool m_running = false;

  // Nav shell (Kartend-m6qsb.17).
  QSplitter *m_splitter = nullptr;
  QListWidget *m_nav = nullptr;
  QStackedWidget *m_pages = nullptr;
  QLabel *m_contextIcon = nullptr;
  QLabel *m_contextTitle = nullptr;

  // Library page (Kartend-m6qsb.5).
  QLabel *m_libraryPathLabel = nullptr;
  QPushButton *m_libraryReviewButton = nullptr;
  QPushButton *m_checkUpdatesButton = nullptr; ///< Check downloaded DATs for newer revisions (.31)
  QFutureWatcher<UpdateRunResult> m_updateWatcher;
  std::shared_ptr<std::atomic<bool>> m_updateCancel;
  QPushButton *m_importZipButton = nullptr;
  QPushButton *m_importFolderButton = nullptr;
  std::function<QString()> m_getLibraryPath;
  std::function<void(const QString &)> m_saveLibraryPath;
  std::function<void(const QString &)> m_importPack;
  std::function<void(const QString &)> m_openScraperForCollection;

  // Download page (Kartend-m6qsb.16 No-Intro, .23 Redump).
  QComboBox *m_dlSourceCombo = nullptr; ///< No-Intro | Redump
  QGroupBox *m_noIntroBox = nullptr;    ///< No-Intro system/dat-type/pack widgets
  QGroupBox *m_setsBox = nullptr;       ///< No-Intro set checkboxes
  QGroupBox *m_redumpBox = nullptr;     ///< Redump system picker
  QLineEdit *m_dlSystemEdit = nullptr;
  QPushButton *m_dlLoadButton = nullptr;
  QWidget *m_dlSetsContainer = nullptr;
  QVBoxLayout *m_dlSetsLayout = nullptr;
  QList<QCheckBox *> m_dlSetChecks;
  QComboBox *m_dlDatTypeCombo = nullptr;
  QLabel *m_dlPackLabel = nullptr;
  QComboBox *m_redumpSystemCombo = nullptr;
  QPushButton *m_redumpRefreshButton = nullptr;
  QPushButton *m_dlDownloadButton = nullptr;
  QPushButton *m_dlCancelButton = nullptr;
  QProgressBar *m_dlProgress = nullptr;
  QLabel *m_dlStatus = nullptr;
  NoIntroParse::DailyForm m_dailyForm;
  QFutureWatcher<DailyFormOutcome> m_dlFormWatcher;
  QFutureWatcher<RedumpSystemsOutcome> m_redumpSystemsWatcher;
  QFutureWatcher<DownloadOutcome> m_dlWatcher;
  std::shared_ptr<std::atomic<bool>> m_dlCancel;
  bool m_downloading = false;
};

#endif // DATAUDITDIALOG_H
