#ifndef LAUNCHEREDITORDIALOG_H
#define LAUNCHEREDITORDIALOG_H

#include <QDialog>
#include <QList>

#include "collectionutils.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QFormLayout;
class QLineEdit;
class QWidget;
QT_END_NAMESPACE

/// Modal editor for a single LauncherConfig entry. Used by the
/// Settings dialog's Additional Launchers list to add or edit a launcher.
/// Validation is intentionally minimal here — the Save path on the parent
/// settings dialog re-runs PathUtils::validatePathSecurity on the launcher
/// and core paths before persistence, mirroring how the primary launcher is
/// validated.
///
/// when launched with a non-empty `availablePresets` list, the
/// dialog shows a "Use preset" combo. Picking a preset fills the form
/// fields from the preset and disables them; the saved LauncherConfig
/// carries the preset id so the reference round-trips. Picking "Inline" or
/// passing an empty preset list keeps the dialog in the legacy free-form
/// mode.
class LauncherEditorDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LauncherEditorDialog)
public:
  explicit LauncherEditorDialog(QWidget *parent, const LauncherConfig &initial,
                                const QString &title,
                                const QList<LauncherPreset> &availablePresets = {},
                                const QString &retroarchConfigOverride = QString());

  /// Returns the launcher as edited by the user. Trim is applied to all
  /// fields; empty `name` is preserved (the chooser falls back to the
  /// executable basename for display).
  [[nodiscard]] LauncherConfig launcher() const;

private slots:
  void onBrowseLauncher();
  void onBrowseCore();
  /// react to the user picking a preset in the combo —
  /// fills/clears the field values and toggles edit-ability.
  void onPresetChanged(int comboIndex);

private:
  /// Updates field text + enabled state to reflect the preset selection.
  /// `presetIndex` is an index into `m_availablePresets`; -1 means "Inline".
  void applyPresetSelection(int presetIndex);

  /// hide the Core Path row unless the launcher path resolves
  /// to retroarch. Other launchers don't take a libretro core, so the field
  /// is noise.
  void updateCoreRowVisibility();

  /// Fill the Core combo with libretro cores discovered in the
  /// RetroArch install (per the override / autodetect).
  void populateCoreCombo();

  QList<LauncherPreset> m_availablePresets;
  QFormLayout *m_form = nullptr;
  QComboBox *m_presetCombo = nullptr;
  QLineEdit *m_nameEdit = nullptr;
  QLineEdit *m_launcherEdit = nullptr;
  QLineEdit *m_coreEdit = nullptr;
  /// Dropdown of libretro cores detected in the RetroArch install;
  /// picking one fills m_coreEdit.
  QComboBox *m_coreCombo = nullptr;
  QWidget *m_coreRowWidget = nullptr;
  QLineEdit *m_paramsEdit = nullptr;
  /// RetroArch override (retroarch.cfg / core dir); empty = autodetect.
  QString m_retroarchOverride;
};

#endif // LAUNCHEREDITORDIALOG_H
