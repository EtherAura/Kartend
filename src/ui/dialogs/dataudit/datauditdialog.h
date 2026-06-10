#ifndef DATAUDITDIALOG_H
#define DATAUDITDIALOG_H

#include <atomic>
#include <memory>

#include <QDialog>
#include <QFutureWatcher>

#include "datauditprofile.h"
#include "datauditrunner.h"

class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableView;
struct CollectionConfig;

namespace DatAudit {
class DatAuditModel;
}

/// The DAT Audit sub-window. Mirrors the scraper's standalone-window UX: the
/// user adds one or more DAT catalogues and one or more scan folders, runs an
/// audit (off the UI thread, cancelable), and gets a filterable results table
/// with a Have/Missing/Wrong-name/Unknown summary, plus CSV / fixdat /
/// miss-list exports.
///
/// v1 scope: this window drives the audit engine + exports. Saved profiles,
/// the fix preview/apply UI, the tree view, and the file-centric vs
/// completeness framing toggle are tracked as follow-ups; the engine, the fix
/// engine, the profile store, and the results model they need are already in
/// place.
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
  void onFix();
  void onExportCsv();
  void onExportFixdat();
  void onExportMissList();

private:
  void setBusy(bool busy);
  void loadProfiles();
  void syncUiFromProfile();
  [[nodiscard]] DatAuditProfile::Profile uiProfile() const;
  bool persistProfile(DatAuditProfile::Profile &p); // insert when id<0, else update
  void selectProfileById(qint64 id);
  void updateSummary(const DatAudit::AuditSummary &summary);
  [[nodiscard]] QStringList datPaths() const;
  [[nodiscard]] QStringList scanRoots() const;
  [[nodiscard]] bool hasResults() const;
  void exportTo(const QString &caption, const QString &filter, const QByteArray &bytes);

  QComboBox *m_profileCombo = nullptr;
  QPushButton *m_newProfileButton = nullptr;
  QPushButton *m_editProfileButton = nullptr;
  QPushButton *m_duplicateProfileButton = nullptr;
  QPushButton *m_renameProfileButton = nullptr;
  QPushButton *m_deleteProfileButton = nullptr;
  QListWidget *m_datList = nullptr;
  QListWidget *m_rootList = nullptr;
  QPushButton *m_runButton = nullptr;
  QPushButton *m_cancelButton = nullptr;
  QProgressBar *m_progress = nullptr;
  QLabel *m_summaryLabel = nullptr;
  QComboBox *m_filterCombo = nullptr;
  QTableView *m_table = nullptr;
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
};

#endif // DATAUDITDIALOG_H
