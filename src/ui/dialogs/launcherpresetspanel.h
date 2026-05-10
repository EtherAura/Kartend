#ifndef LAUNCHERPRESETSPANEL_H
#define LAUNCHERPRESETSPANEL_H

#include "collectionutils.h"
#include <QList>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class LauncherPresetsPanel;
}
QT_END_NAMESPACE

/// Standalone panel widget for the global "Launcher Presets" tab in
/// SettingsDialog. Observes a QList<LauncherPreset>* installed by the host
/// dialog so per-collection launcher controls can keep reading from the same
/// list without going through this panel.
class LauncherPresetsPanel : public QWidget {
  Q_OBJECT
public:
  explicit LauncherPresetsPanel(QWidget *parent = nullptr);
  ~LauncherPresetsPanel() override;

  /// Install a pointer to the live preset list. The pointer must outlive the
  /// panel (the host SettingsDialog retains ownership). Triggers a refresh.
  void setPresets(QList<LauncherPreset> *presets);

  /// Re-hydrate the list view from the pointed-to presets. Safe to call
  /// repeatedly; preserves the current selection row when possible.
  void refresh();

signals:
  /// Emitted after any user-driven mutation (add / edit / remove). The
  /// pointed-to list has already been updated; observers should re-run their
  /// own change-detection (e.g. SettingsDialog::checkForChanges).
  void presetsChanged();

private slots:
  void onAdd();
  void onEdit();
  void onRemove();
  void onSelectionChanged();

private:
  void updateButtonsState();

  Ui::LauncherPresetsPanel *ui;
  QList<LauncherPreset> *m_presets = nullptr;
};

#endif // LAUNCHERPRESETSPANEL_H
