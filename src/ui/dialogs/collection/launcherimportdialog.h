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
class QLabel;
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
  int gameCount = 0; ///< Count at the narrowest scope (installed).
  /// Counts at the wider scopes. Equal to gameCount for sources that only
  /// ever see installed applications (everything but Steam).
  int ownedGameCount = 0;
  int recognizedGameCount = 0;
  bool alreadyImported = false;
};

/// How much of a launcher's library to import. Mirrors
/// LauncherImportService::ImportScope but stays ui-local for the same reason
/// LauncherImportSourceRow does — the controller maps between them
/// (Kartend-el5st).
enum class LauncherImportScopeChoice {
  InstalledOnly,
  Owned,
  AllRecognized,
};

/// "Import from Launcher…" source picker (Kartend-wuq2c): one checkbox per
/// detected launcher with its game count, plus a scope selector that widens
/// Steam beyond installed games (Kartend-el5st) and relabels the counts as it
/// changes. Undetected launchers
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

  /// The chosen breadth. Only Steam varies with it; the picker says so, and
  /// the per-source counts in the rows follow the selection live.
  [[nodiscard]] LauncherImportScopeChoice selectedScope() const;

  /// Existing collections offered as the parent for newly created
  /// collections: (display name, uuid) pairs, mirroring
  /// CreateCollectionDialog::setParentCollectionOptions. A "Top level"
  /// entry is always present and is the default.
  void setParentCollectionOptions(const QList<QPair<QString, QString>> &options);
  /// Uuid of the chosen parent, or empty for top level.
  [[nodiscard]] QString parentCollectionUuid() const;

private:
  void updateImportEnabled();
  /// Re-renders every row's label + default check state for the current
  /// scope, so switching the tier updates the counts in place.
  void relabelRows();
  [[nodiscard]] int countForScope(const LauncherImportSourceRow &row) const;

  QVBoxLayout *m_rowsLayout = nullptr;
  QComboBox *m_parentCombo = nullptr;
  QComboBox *m_scopeCombo = nullptr;
  QLabel *m_scopeHint = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
  QList<QPair<QString, QCheckBox *>> m_checks; ///< source id → its checkbox
  QList<LauncherImportSourceRow> m_rows;       ///< as given to setSources
};

#endif // LAUNCHERIMPORTDIALOG_H
