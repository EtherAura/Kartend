#ifndef LAUNCHERPRESETSPANEL_H
#define LAUNCHERPRESETSPANEL_H

#include "collection/launcherpreset.h"
#include "isettingspanel.h"
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
class LauncherPresetsPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LauncherPresetsPanel)
public:
  explicit LauncherPresetsPanel(QWidget *parent = nullptr);
  ~LauncherPresetsPanel() override;

  /// Install a pointer to the live preset list. The pointer must outlive the
  /// panel (the host SettingsDialog retains ownership). Triggers a refresh.
  void setPresets(QList<LauncherPreset> *presets);

  /// RetroArch install override (a retroarch.cfg file or a core
  /// directory) forwarded to the launcher editor so its Core picker
  /// can list installed cores. Empty auto-detects the standard path.
  void setRetroarchConfigOverride(const QString &configOverride);

  // ISettingsPanel (Kartend-ny2ki). load() was refresh() — re-hydrate the
  // list view from the pointed-to presets, preserving the selection row when
  // possible. save() is a no-op: this panel mutates the live preset list in
  // place on each add/edit/remove and emits presetsChanged(), so there is no
  // deferred flush. clear() is a no-op — the panel is backed by the single
  // live preset list.
  void load() override;
  void save() override {}
  void clear() override {}

signals:
  /// Emitted after any user-driven mutation (add / edit / remove). The
  /// pointed-to list has already been updated; observers should re-run their
  /// own change-detection (e.g. SettingsDialog::checkForChanges).
  void presetsChanged();

private slots:
  void onAdd();
  void onEdit();
  void onRemove();
  void onDetect();
  void onSelectionChanged();

private:
  void updateButtonsState();

  Ui::LauncherPresetsPanel *ui;
  QList<LauncherPreset> *m_presets = nullptr;
  /// RetroArch override passed through to each LauncherEditorDialog.
  QString m_retroarchOverride;
  /// Programmatically appended below the existing Add / Edit / Remove
  /// stack. Pops a multi-select picker of well-known media-player /
  /// reader / image / emulator binaries detected on the user's PATH.
  class QPushButton *m_detectButton = nullptr;
};

#endif // LAUNCHERPRESETSPANEL_H
