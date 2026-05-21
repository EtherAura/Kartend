#ifndef CONTROLSPANEL_H
#define CONTROLSPANEL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ControlsPanel;
}
class QCheckBox;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

struct SettingsModel;
class GamepadCaptureController;

/// Standalone panel widget for the global "Controls" sub-tab in
/// SettingsDialog. Owns three field groups — Keyboard (8 QKeySequenceEdit
/// shortcuts for nav / confirm / back / search / home view), Gamepad (3
/// detect-able button bindings + 2 axis-source toggles), and Mouse (the
/// artwork-cycle modifier combo). Observes a GeneralSettings* installed by
/// the host dialog and emits changed() on every mutation; deferred-save.
///
/// Gamepad-capture handoff: the host SettingsDialog owns the
/// GamepadCaptureController (it needs MainWindow's GamepadManager to drive
/// the capture loop). The panel exposes its detect buttons / line edits /
/// sibling checkboxes via accessors so the controller can refresh their UI
/// during a capture pass without the panel and controller coupling
/// directly.
class ControlsPanel : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ControlsPanel)
public:
  explicit ControlsPanel(QWidget *parent = nullptr);
  ~ControlsPanel() override;

  void setModel(SettingsModel *model);
  void refresh();

  /// Wire detect-button clicks to @p controller's start() with the
  /// corresponding Target. Idempotent if installed multiple times — only
  /// the most recent controller receives clicks.
  void installGamepadCaptureController(GamepadCaptureController *controller);

  // Accessors for GamepadCaptureController to refresh UI state during a
  // capture pass.
  [[nodiscard]] QLineEdit *gamepadConfirmLineEdit() const;
  [[nodiscard]] QLineEdit *gamepadBackLineEdit() const;
  [[nodiscard]] QLineEdit *gamepadToggleSidebarLineEdit() const;
  [[nodiscard]] QPushButton *detectConfirmButton() const;
  [[nodiscard]] QPushButton *detectBackButton() const;
  [[nodiscard]] QPushButton *detectToggleSidebarButton() const;
  [[nodiscard]] QCheckBox *useDpadCheckBox() const;
  [[nodiscard]] QCheckBox *useLeftStickCheckBox() const;

signals:
  void changed();

private:
  void writeBack();
  void connectChangeSignals();
  void populateArtworkCycleModifierCombo();

  Ui::ControlsPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // CONTROLSPANEL_H
