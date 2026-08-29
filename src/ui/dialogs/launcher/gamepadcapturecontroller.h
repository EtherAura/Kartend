#ifndef GAMEPADCAPTURECONTROLLER_H
#define GAMEPADCAPTURECONTROLLER_H

#include <QMetaObject>
#include <QObject>

struct ApplicationContext;
class SettingsDialog;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QString;

/// Owns the SettingsDialog's gamepad button-capture state machine: the
/// active capture target, the GamepadManager binding-capture connection,
/// and the per-tick UI sync (button labels, line-edit placeholders, lock
/// state of the sibling D-Pad / left-stick checkboxes while a capture is
/// in flight).
///
/// Coupling: takes its host SettingsDialog plus the ApplicationContext at
/// construction. The GamepadManager is resolved through
/// ctx->interactionManager()->gamepadManager() (Kartend-qjtz), not by
/// dynamic_cast-ing the host's parent to IMainWindow. Widgets are bound
/// explicitly via setWidgets(); the panel that owns them (ControlsPanel)
/// hands them across.
class GamepadCaptureController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(GamepadCaptureController)

public:
  enum class Target { None, Confirm, Back, ToggleSidebar, ToggleCollectionTree };

  /// Widget bag passed across the panel/controller boundary. Members may
  /// be null during the brief construction window before setWidgets() runs;
  /// the controller no-ops on null entries.
  struct Bindings {
    QLineEdit *confirmEdit = nullptr;
    QLineEdit *backEdit = nullptr;
    QLineEdit *toggleSidebarEdit = nullptr;
    QLineEdit *toggleCollectionTreeEdit = nullptr;
    QPushButton *detectConfirmButton = nullptr;
    QPushButton *detectBackButton = nullptr;
    QPushButton *detectToggleSidebarButton = nullptr;
    QPushButton *detectToggleCollectionTreeButton = nullptr;
    QCheckBox *useDpadCheckBox = nullptr;
    QCheckBox *useLeftStickCheckBox = nullptr;
  };

  explicit GamepadCaptureController(SettingsDialog *host, const ApplicationContext *ctx);

  /// Install widget pointers. Triggers a refresh so the buttons reflect
  /// the current target state.
  void setWidgets(const Bindings &bindings);

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
  const ApplicationContext *m_ctx = nullptr;
  Bindings m_bindings;
  Target m_target = Target::None;
  QMetaObject::Connection m_captureConnection;
};

#endif // GAMEPADCAPTURECONTROLLER_H
