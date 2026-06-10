#ifndef DATAUDITPROFILEDIALOG_H
#define DATAUDITPROFILEDIALOG_H

#include <QDialog>

#include "datauditprofile.h"

class QCheckBox;
class QComboBox;
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
  void onFixModeChanged();
  void accept() override;

private:
  static void removeSelected(QListWidget *list);

  DatAuditProfile::Profile m_seed; // preserves id / timestamps

  QLineEdit *m_name = nullptr;
  QComboBox *m_collection = nullptr; // optional collection link; data() holds the UUID
  QListWidget *m_datList = nullptr;
  QListWidget *m_rootList = nullptr;
  QComboBox *m_mergeMode = nullptr;
  QCheckBox *m_onePerGame = nullptr;
  QListWidget *m_regionList = nullptr; // priority order, top = most preferred
  QComboBox *m_regionToAdd = nullptr;
  QPlainTextEdit *m_ignoreRules = nullptr; // one glob per line
  QRadioButton *m_fixInPlace = nullptr;
  QRadioButton *m_fixManaged = nullptr;
  QLineEdit *m_managedRoot = nullptr;
  QPushButton *m_managedBrowse = nullptr;
};

#endif // DATAUDITPROFILEDIALOG_H
