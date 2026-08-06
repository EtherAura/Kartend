#ifndef LAUNCHERIMPORTDIALOG_H
#define LAUNCHERIMPORTDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QVBoxLayout;
QT_END_NAMESPACE

/// Row model for one importable launcher source. Deliberately ui-local so
/// the dialog carries no dependency on the data-layer import service — the
/// LauncherImportController maps LauncherImportService::SourceInfo (plus its
/// own already-imported knowledge) into these.
struct LauncherImportSourceRow {
  QString id;
  QString displayName;
  bool available = false;
  int gameCount = 0;
  bool alreadyImported = false;
};

/// "Import from Launcher…" source picker (Kartend-wuq2c): one checkbox per
/// detected launcher with its installed-game count. Undetected launchers
/// show disabled so the user learns what Kartend looked for; sources that
/// already have a collection can be re-checked to trigger a re-sync. The
/// dialog only picks — detection, sync, and collection creation stay in the
/// controller.
class LauncherImportDialog : public QDialog {
  Q_OBJECT
public:
  explicit LauncherImportDialog(QWidget *parent = nullptr);

  void setSources(const QList<LauncherImportSourceRow> &rows);
  [[nodiscard]] QStringList selectedSourceIds() const;

  /// Existing collections offered as the parent for newly created
  /// collections: (display name, uuid) pairs, mirroring
  /// CreateCollectionDialog::setParentCollectionOptions. A "Top level"
  /// entry is always present and is the default.
  void setParentCollectionOptions(const QList<QPair<QString, QString>> &options);
  /// Uuid of the chosen parent, or empty for top level.
  [[nodiscard]] QString parentCollectionUuid() const;

private:
  void updateImportEnabled();

  QVBoxLayout *m_rowsLayout = nullptr;
  QComboBox *m_parentCombo = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
  QList<QPair<QString, QCheckBox *>> m_checks; ///< source id → its checkbox
};

#endif // LAUNCHERIMPORTDIALOG_H
