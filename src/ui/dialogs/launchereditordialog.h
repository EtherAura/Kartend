#ifndef LAUNCHEREDITORDIALOG_H
#define LAUNCHEREDITORDIALOG_H

#include <QDialog>

#include "collectionutils.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

/// Modal editor for a single LauncherConfig entry (Kartend-bdl). Used by the
/// Settings dialog's Additional Launchers list to add or edit a launcher.
/// Validation is intentionally minimal here — the Save path on the parent
/// settings dialog re-runs PathUtils::validatePathSecurity on the launcher
/// and core paths before persistence, mirroring how the primary launcher is
/// validated.
class LauncherEditorDialog : public QDialog {
  Q_OBJECT
public:
  explicit LauncherEditorDialog(QWidget *parent, const LauncherConfig &initial,
                                const QString &title);

  /// Returns the launcher as edited by the user. Trim is applied to all
  /// fields; empty `name` is preserved (the chooser falls back to the
  /// executable basename for display).
  [[nodiscard]] LauncherConfig launcher() const;

private slots:
  void onBrowseLauncher();
  void onBrowseCore();

private:
  QLineEdit *m_nameEdit = nullptr;
  QLineEdit *m_launcherEdit = nullptr;
  QLineEdit *m_coreEdit = nullptr;
  QLineEdit *m_paramsEdit = nullptr;
};

#endif // LAUNCHEREDITORDIALOG_H
