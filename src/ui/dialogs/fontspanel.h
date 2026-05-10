#ifndef FONTSPANEL_H
#define FONTSPANEL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class FontsPanel;
}
QT_END_NAMESPACE

struct GeneralSettings;

/// Standalone panel widget for the global "Application Font" sub-tab in
/// SettingsDialog. Observes a GeneralSettings* installed by the host dialog
/// and mutates the pointed-to globalUiFont* fields directly on user input,
/// emitting changed() so the host can persist + apply (saveGeneralSettings +
/// applyGlobalUiFont) in lockstep — preserving the dialog's existing
/// "live save" semantics for these controls.
class FontsPanel : public QWidget {
  Q_OBJECT
public:
  explicit FontsPanel(QWidget *parent = nullptr);
  ~FontsPanel() override;

  /// Install a pointer to the live general settings. The pointer must
  /// outlive the panel. Triggers a refresh so the form fields reflect the
  /// pointed-to values immediately.
  void setSettings(GeneralSettings *settings);

  /// Re-hydrate the form fields from the pointed-to settings. Safe to call
  /// repeatedly. Suppresses the change signal during the rehydrate.
  void refresh();

signals:
  /// Emitted after any user-driven mutation (font family edit, size spin, or
  /// font picker accept). The pointed-to settings have already been updated;
  /// observers should persist + apply in response.
  void changed();

private slots:
  void onPick();

private:
  void writeBack();

  Ui::FontsPanel *ui;
  GeneralSettings *m_settings = nullptr;
};

#endif // FONTSPANEL_H
