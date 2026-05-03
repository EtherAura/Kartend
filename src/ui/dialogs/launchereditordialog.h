#ifndef LAUNCHEREDITORDIALOG_H
#define LAUNCHEREDITORDIALOG_H

#include <QDialog>
#include <QList>

#include "collectionutils.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
QT_END_NAMESPACE

/// Modal editor for a single LauncherConfig entry (Kartend-bdl). Used by the
/// Settings dialog's Additional Launchers list to add or edit a launcher.
/// Validation is intentionally minimal here — the Save path on the parent
/// settings dialog re-runs PathUtils::validatePathSecurity on the launcher
/// and core paths before persistence, mirroring how the primary launcher is
/// validated.
///
/// Kartend-p1jd: when launched with a non-empty `availablePresets` list, the
/// dialog shows a "Use preset" combo. Picking a preset fills the form
/// fields from the preset and disables them; the saved LauncherConfig
/// carries the preset id so the reference round-trips. Picking "Inline" or
/// passing an empty preset list keeps the dialog in the legacy free-form
/// mode.
class LauncherEditorDialog : public QDialog {
  Q_OBJECT
public:
  explicit LauncherEditorDialog(QWidget *parent, const LauncherConfig &initial,
                                const QString &title,
                                const QList<LauncherPreset> &availablePresets = {});

  /// Returns the launcher as edited by the user. Trim is applied to all
  /// fields; empty `name` is preserved (the chooser falls back to the
  /// executable basename for display).
  [[nodiscard]] LauncherConfig launcher() const;

private slots:
  void onBrowseLauncher();
  void onBrowseCore();
  /// Kartend-p1jd: react to the user picking a preset in the combo —
  /// fills/clears the field values and toggles edit-ability.
  void onPresetChanged(int comboIndex);

private:
  /// Updates field text + enabled state to reflect the preset selection.
  /// `presetIndex` is an index into `m_availablePresets`; -1 means "Inline".
  void applyPresetSelection(int presetIndex);

  QList<LauncherPreset> m_availablePresets;
  QComboBox *m_presetCombo = nullptr;
  QLineEdit *m_nameEdit = nullptr;
  QLineEdit *m_launcherEdit = nullptr;
  QLineEdit *m_coreEdit = nullptr;
  QLineEdit *m_paramsEdit = nullptr;
};

#endif // LAUNCHEREDITORDIALOG_H
