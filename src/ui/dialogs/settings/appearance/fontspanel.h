#ifndef FONTSPANEL_H
#define FONTSPANEL_H

#include "isettingspanel.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class FontsPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the global "Application Font" sub-tab in
/// SettingsDialog. Observes a SettingsModel* installed by the host dialog
/// and mutates the pointed-to globalUiFont* fields directly on user input,
/// emitting changed() so the host can persist + apply (saveGeneralSettings +
/// applyGlobalUiFont) in lockstep — preserving the dialog's existing
/// "live save" semantics for these controls.
class FontsPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(FontsPanel)
public:
  explicit FontsPanel(QWidget *parent = nullptr);
  ~FontsPanel() override;

  /// Install a pointer to the live settings model. The pointer must
  /// outlive the panel. Triggers a refresh so the form fields reflect the
  /// pointed-to values immediately.
  void setModel(SettingsModel *model);

  // ISettingsPanel (Kartend-ny2ki). load() was refresh(); save() was the
  // private writeBack(); clear() is a no-op — this global panel is always
  // backed by the single live GeneralSettings model.
  void load() override;
  void save() override;
  void clear() override {}

signals:
  /// Emitted after any user-driven mutation (font family edit, size spin, or
  /// font picker accept). The pointed-to settings have already been updated;
  /// observers should persist + apply in response.
  void changed();

private slots:
  void onPick();

private:
  Ui::FontsPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // FONTSPANEL_H
