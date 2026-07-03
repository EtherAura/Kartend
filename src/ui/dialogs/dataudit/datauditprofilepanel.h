#ifndef DATAUDITPROFILEPANEL_H
#define DATAUDITPROFILEPANEL_H

#include <functional>

#include <QList>
#include <QWidget>

#include "datauditprofile.h"           // DatAuditProfile::Profile (signal payload)
#include "datauditprofilecontroller.h" // DatAuditProfileController (value member)

struct CollectionConfig; // defined as a struct in collectionconfig.h (-Wmismatched-tags)
class DatAuditProfileStore;
class QComboBox;
class QPushButton;

/// The saved-profile row of the DAT-audit page: the profile combo plus the
/// New / Edit / Duplicate / Rename / Delete CRUD buttons, lifted off
/// DatAuditAuditPage so the page keeps only audit-run logic. The panel owns its
/// widgets and a DatAuditProfileController over the dialog-shared store, and
/// announces the outcome of every selection / CRUD flow via profileChanged /
/// unsavedSelected / profileDeleted. Flows that need the page's working profile
/// (the Edit / Duplicate seed, the Rename persist base) read it back through
/// the injected provider callbacks, so the working profile itself stays on the
/// page as the single source of truth.
class DatAuditProfilePanel : public QWidget {
  Q_OBJECT

public:
  explicit DatAuditProfilePanel(DatAuditProfileStore &profileStore, QWidget *parent = nullptr);

  /// Borrowed collection list handed to the profile editor dialog; not owned.
  void setCollections(QList<CollectionConfig> *collections);
  /// Seed for Edit / Duplicate: the page's working profile INCLUDING the live
  /// on-screen DAT / scan-folder lists (the page's uiProfile()).
  void setWorkingProfileProvider(std::function<DatAuditProfile::Profile()> provider);
  /// Base for Rename: the page's working profile WITHOUT the live list edits.
  /// Rename has never persisted unsaved list edits (the post-persist reload
  /// deliberately reverts them), so it must not pick them up here either.
  void setBaseProfileProvider(std::function<DatAuditProfile::Profile()> provider);

  /// Repopulate the combo from the store ("(unsaved)" row first, no signals).
  void reloadProfiles();
  /// Select a saved profile's row; runs the normal selection flow (a DB load +
  /// profileChanged) when the row actually changes. Unknown ids are a no-op.
  void selectProfileById(qint64 id);
  /// Select the "(unsaved)" row without announcing it — openForCollection seeds
  /// the page's working profile itself and must not have it cleared underneath.
  void selectUnsavedSilently();
  /// Persist @p p via the controller (insert assigns the new id back into it),
  /// reporting failure with a QMessageBox. Shared with the page for its
  /// confirmed-layout persist.
  bool persistProfile(DatAuditProfile::Profile &p);

signals:
  /// A saved profile was loaded (combo selection / New / Edit / Duplicate).
  /// The page adopts it as the working profile and re-syncs its inputs.
  void profileChanged(const DatAuditProfile::Profile &profile);
  /// The "(unsaved)" row was picked — the page clears the working profile but
  /// keeps its ad-hoc DAT / folder lists.
  void unsavedSelected();
  /// The selected profile was deleted — the page clears the working profile.
  void profileDeleted();

private:
  void buildUi();
  void onProfileSelected(int index);
  void onNewProfile();
  void onEditProfile();
  void onDuplicateProfile();
  void onRenameProfile();
  void onDeleteProfile();

  QComboBox *m_profileCombo = nullptr;
  QPushButton *m_newProfileButton = nullptr;
  QPushButton *m_editProfileButton = nullptr;
  QPushButton *m_duplicateProfileButton = nullptr;
  QPushButton *m_renameProfileButton = nullptr;
  QPushButton *m_deleteProfileButton = nullptr;

  /// Profile CRUD/persist over the dialog-owned store (shared by ref; the page
  /// keeps its own controller instance for audit-result snapshots).
  DatAuditProfileController m_controller;
  QList<CollectionConfig> *m_collections = nullptr; ///< Borrowed; not owned.
  std::function<DatAuditProfile::Profile()> m_workingProfile;
  std::function<DatAuditProfile::Profile()> m_baseProfile;
};

#endif // DATAUDITPROFILEPANEL_H
