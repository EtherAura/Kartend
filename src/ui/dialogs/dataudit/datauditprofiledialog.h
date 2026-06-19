#ifndef DATAUDITPROFILEDIALOG_H
#define DATAUDITPROFILEDIALOG_H

#include <QDialog>

#include "datauditprofile.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
struct CollectionConfig;

/// Modal editor for one DAT-audit profile (Kartend-bmj1ko). A pure value
/// editor: seeded with a Profile, it edits every user-facing field and returns
/// the result via profile() on accept. Persistence is the caller's job
/// (DatAuditDialog saves through DatAuditProfile::insert/update), so this
/// dialog holds no DB connection. The seed's id / timestamps are carried
/// through untouched; collectionUuid is now editable via the "Linked
/// collection" picker (Kartend-x9mkif.3) — pass the live collection list so the
/// picker can offer them (null/empty just yields "(none)").
class DatAuditProfileDialog : public QDialog {
  Q_OBJECT
public:
  explicit DatAuditProfileDialog(const DatAuditProfile::Profile &seed,
                                 const QList<CollectionConfig> *collections = nullptr,
                                 QWidget *parent = nullptr);

  /// The edited profile; valid after exec() returns QDialog::Accepted.
  [[nodiscard]] DatAuditProfile::Profile profile() const;

private slots:
  void onAddDat();
  void onAddRoot();
  void onAddRegion();
  void onMoveRegionUp();
  void onMoveRegionDown();
  void onBrowseManagedRoot();
  void onBrowseQuarantineRoot();
  void onFixModeChanged();
  /// React to the Linked-collection picker: linking seeds the DAT list and scan
  /// folder from the collection only when the profile has none (seed-once), then
  /// both stay user-editable — nothing is locked. "(none)" leaves the lists for
  /// the user to edit; an unresolvable stored link keeps the seed's cached lists.
  void onLinkedCollectionChanged();
  void accept() override;

private:
  static void removeSelected(QListWidget *list);

  DatAuditProfile::Profile m_seed;                        // preserves id / timestamps
  const QList<CollectionConfig> *m_collections = nullptr; // borrowed; may be null

  QLineEdit *m_name = nullptr;
  QComboBox *m_collection = nullptr; // optional collection link; data() holds the UUID
  QListWidget *m_datList = nullptr;
  QListWidget *m_rootList = nullptr;
  QPushButton *m_addDatButton = nullptr;
  QPushButton *m_removeDatButton = nullptr;
  QPushButton *m_addRootButton = nullptr;
  QPushButton *m_removeRootButton = nullptr;
  QLabel *m_linkedHint = nullptr;
  QCheckBox *m_onePerGame = nullptr;
  QComboBox *m_mergeMode = nullptr;    // clone/parent merge mode (Kartend-m6qsb.29)
  QListWidget *m_regionList = nullptr; // priority order, top = most preferred
  QComboBox *m_regionToAdd = nullptr;
  QPlainTextEdit *m_ignoreRules = nullptr; // one glob per line
  QRadioButton *m_fixInPlace = nullptr;
  QRadioButton *m_fixManaged = nullptr;
  QLineEdit *m_managedRoot = nullptr;
  QPushButton *m_managedBrowse = nullptr;
  QLineEdit *m_quarantineRoot = nullptr;
  QPushButton *m_quarantineBrowse = nullptr;
};

#endif // DATAUDITPROFILEDIALOG_H
