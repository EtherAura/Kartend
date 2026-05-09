#ifndef GAMEPADCAPTURECONTROLLER_H
#define GAMEPADCAPTURECONTROLLER_H

#include <QMetaObject>
#include <QObject>

class SettingsDialog;
class QString;

/// Owns the SettingsDialog's gamepad button-capture state machine: the
/// active capture target, the GamepadManager binding-capture connection,
/// and the per-tick UI sync (button labels, line-edit placeholders, lock
/// state of the sibling checkboxes while a capture is in flight).
///
/// Coupling: takes its host SettingsDialog at construction time. The
/// helper reaches into the host's ui->* widgets to apply line-edit
/// updates and button label/enable transitions, and into MainWindow ->
/// InteractionManager -> GamepadManager to begin/end the capture pass.
/// Friend-declared in SettingsDialog so the line-edit + button widgets
/// stay encapsulated.
class GamepadCaptureController : public QObject {
  Q_OBJECT

public:
  enum class Target { None, Confirm, Back, ToggleSidebar };

  explicit GamepadCaptureController(SettingsDialog *host);

  /// Begin capturing for @p target. Calling with the same target as the
  /// active capture toggles it off (matches the prior in-line behavior).
  /// Pops a QMessageBox warning when the gamepad backend is unavailable.
  void start(Target target);

  /// End any in-flight capture, disconnect the binding-capture handler,
  /// and refresh the UI so the line edits / detect buttons return to
  /// their idle state.
  void stop();

  /// Refresh button labels, line-edit placeholders, and the lock state
  /// of the sibling D-Pad / left-stick checkboxes. Idempotent.
  void refreshUi();

  [[nodiscard]] Target activeTarget() const { return m_target; }

private:
  void onButtonPressed(const QString &buttonName);

  SettingsDialog *m_host;
  Target m_target = Target::None;
  QMetaObject::Connection m_captureConnection;
};

#endif // GAMEPADCAPTURECONTROLLER_H
