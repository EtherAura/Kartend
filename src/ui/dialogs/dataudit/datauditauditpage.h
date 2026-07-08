#ifndef DATAUDITAUDITPAGE_H
#define DATAUDITAUDITPAGE_H

#include <functional>

#include <QList>
#include <QWidget>

#include "datauditlayout.h"            // DatAudit::LayoutDetection (value member)
#include "datauditprofile.h"           // DatAuditProfile::Profile (value member)
#include "datauditprofilecontroller.h" // DatAuditProfileController (value member)
#include "datauditruncontroller.h"     // DatAuditRunController (value member)
#include "datauditrunner.h" // DatAudit::AuditProgress / AuditOutput / AuditSummary / AuditRow

struct CollectionConfig; // defined as a struct in collectionconfig.h (-Wmismatched-tags)
class DatAuditFixDialog;
class DatAuditProfilePanel;
class DatAuditProfileStore;
class QCheckBox;
class QComboBox;
class QLabel;
class QLayout;
class QLineEdit;
class QListWidget;
class QModelIndex;
class QPoint;
class QProgressBar;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;

namespace DatAudit {
class DatAuditModel;
}

/// The "Audit" stacked page of the DAT Manager (Kartend-oa0lu): the scan/DAT
/// inputs + layout detection, the off-thread run (owns DatAuditRunController),
/// the results table + filter/export, and the Fix flow. Profile selection +
/// CRUD live in the embedded DatAuditProfilePanel — the page adopts each
/// announced profile as its working copy (adoptProfile) and hands the panel its
/// on-screen state back through provider callbacks. Construction is split into
/// the sibling TU datauditauditpage_build.cpp (the build* helpers). The dialog
/// injects the shared DatAuditProfileStore + the collection list + the
/// quarantine/scraper providers, and mediates the browser page via
/// reauditProfile/fixProfile + the browserNodeRefreshRequested signal.
class DatAuditAuditPage : public QWidget {
  Q_OBJECT

public:
  explicit DatAuditAuditPage(DatAuditProfileStore &profileStore, QWidget *parent = nullptr);

  void setCollections(QList<CollectionConfig> *collections);
  void setQuarantineDefaultProvider(std::function<QString()> provider);
  void setScraperOpener(std::function<void(const QString &)> opener);

  /// Open the audit page aimed at a collection (links to or seeds a profile).
  void openForCollection(const QString &collectionUuid, const QString &collectionName,
                         const QString &mediaDir, const QStringList &datPaths);
  /// Browser context-menu entry points (Kartend-7iqhl.2).
  void reauditProfile(qint64 profileId);
  void fixProfile(qint64 profileId);

signals:
  /// A browser-initiated audit/fix changed this profile's snapshot — the dialog
  /// rebuilds the browser tree and re-selects the node.
  void browserNodeRefreshRequested(qint64 profileId);
  /// Live progress of a browser-initiated re-audit, mirrored onto the browser
  /// page's inline bar by the dialog (Kartend-7iqhl.3).
  void browserAuditProgress(int filesDone, int filesTotal);
  void browserAuditRunningChanged(bool running);

private:
  // Construction (sibling TU datauditauditpage_build.cpp).
  QWidget *buildAuditPage(DatAuditProfileStore &profileStore);
  QLayout *buildInputsSection(QWidget *page);
  QLayout *buildLayoutBanner(QWidget *page);
  QWidget *buildResultsTable(QWidget *page);
  QLayout *buildExportRow(QWidget *page);
  void populateFilterCombo();
  void wireProfilePanel();
  void wireAuditActions();
  /// Adopt a profile the panel announced (selection / New / Edit / Duplicate)
  /// as the working profile: derive from the linked collection + re-sync UI.
  void adoptProfile(const DatAuditProfile::Profile &profile);
  bool loadProfileFromDb(qint64 id);
  void applyCollectionDerivation();
  void updateLinkedUiState();
  void runLayoutDetection(bool autoTriggered);
  void applyDetectedLayout();
  void syncUiFromProfile();
  [[nodiscard]] DatAuditProfile::Profile uiProfile() const;
  [[nodiscard]] QStringList datPaths() const;
  [[nodiscard]] QStringList scanRoots() const;
  [[nodiscard]] bool hasResults() const;
  [[nodiscard]] bool hasApplicableFixes() const;
  void onAddDat();
  void onRemoveDat();
  void onAddRoot();
  void onRemoveRoot();
  void onRun();
  void onCancel();
  void onAuditProgress(const DatAudit::AuditProgress &p);
  void onSnapshotPersisted(qint64 profileId, qint64 whenMs);
  void onAuditFinished(const DatAudit::AuditOutput &out);
  void onFilterChanged(int index);
  void updateSummary(const DatAudit::AuditSummary &s);
  [[nodiscard]] const DatAudit::AuditRow *rowForProxyIndex(const QModelIndex &proxyIndex) const;
  void onResultDoubleClicked(const QModelIndex &index);
  void onResultsContextMenu(const QPoint &pos);
  void setBusy(bool busy);
  void seedFixDialogDefaults(DatAuditFixDialog &dlg);
  void offerRescrapeAfterFix(const DatAuditFixDialog &dlg);
  void onFix();
  void exportTo(const QString &caption, const QString &filter, const QByteArray &bytes);
  void onExportCsv();
  void onExportFixdat();
  void onExportMissList();

  // Widgets + state (moved from DatAuditDialog, Kartend-oa0lu).
  /// Profile combo + CRUD row (owns the profile-CRUD controller; announces
  /// outcomes via profileChanged / unsavedSelected / profileDeleted).
  DatAuditProfilePanel *m_profilePanel = nullptr;
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
  QCheckBox *m_forceRehashCheck = nullptr;
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
  DatAuditRunController m_runController; ///< owns the QFutureWatcher + worker.
  /// Profile loads + audit-result snapshots over the dialog-owned store (shared
  /// by ref; the download service reads the same store). Kartend-n1hpy.3. The
  /// CRUD/persist flows use the panel's own controller instance — the
  /// controller is a stateless facade, so the two instances can't diverge.
  DatAuditProfileController m_profileController;
  DatAuditProfile::Profile m_currentProfile;
  QList<CollectionConfig> *m_collections = nullptr; ///< Borrowed from MainWindow; not owned.
  bool m_running = false;
  bool m_browserAuditPending = false;
  std::function<QString()> m_getQuarantineDefaultDir;
  std::function<void(const QString &)> m_openScraperForCollection;
};

#endif // DATAUDITAUDITPAGE_H
