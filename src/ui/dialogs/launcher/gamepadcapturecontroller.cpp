#include "gamepadcapturecontroller.h"

#include "gamepadmanager.h"
#include "imainwindow.h"
#include "interactionmanager.h"
#include "settingsdialog.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

namespace {
GamepadManager *resolveGamepadManager(QObject *parent) {
  auto *mainWindow = dynamic_cast<IMainWindow *>(parent);
  auto *interaction = mainWindow ? mainWindow->interactionManager() : nullptr;
  return interaction ? interaction->gamepadManager() : nullptr;
}
} // namespace

GamepadCaptureController::GamepadCaptureController(SettingsDialog *host)
    : QObject(host), m_host(host) {}

void GamepadCaptureController::setWidgets(const Bindings &bindings) {
  m_bindings = bindings;
  refreshUi();
}

void GamepadCaptureController::start(Target target) {
  GamepadManager *gamepad = resolveGamepadManager(m_host ? m_host->parent() : nullptr);
  if (!gamepad) {
    QMessageBox::information(m_host, tr("Gamepad"),
                             tr("Gamepad input is not available on this build/configuration."));
    return;
  }

  // Same-target toggle: hitting Detect twice should cancel rather than
  // re-arm the capture (matches the prior in-line behavior on
  // SettingsDialog).
  if (m_target == target) {
    stop();
    return;
  }

  stop();

  m_target = target;
  refreshUi();

  gamepad->beginBindingCapture();
  m_captureConnection = connect(gamepad, &GamepadManager::bindingCaptureButtonPressed, this,
                                [this](const QString &buttonName) { onButtonPressed(buttonName); });
}

void GamepadCaptureController::stop() {
  if (GamepadManager *gamepad = resolveGamepadManager(m_host ? m_host->parent() : nullptr)) {
    gamepad->endBindingCapture();
    QObject::disconnect(m_captureConnection);
    m_captureConnection = QMetaObject::Connection();
  }

  m_target = Target::None;
  refreshUi();
}

void GamepadCaptureController::onButtonPressed(const QString &buttonName) {
  if (m_target == Target::Confirm && m_bindings.confirmEdit) {
    m_bindings.confirmEdit->setText(buttonName);
  } else if (m_target == Target::Back && m_bindings.backEdit) {
    m_bindings.backEdit->setText(buttonName);
  } else if (m_target == Target::ToggleSidebar && m_bindings.toggleSidebarEdit) {
    m_bindings.toggleSidebarEdit->setText(buttonName);
  }

  stop();
  // No explicit dirty-check call: setText() above triggered QLineEdit::
  // textChanged on the bound edit, which runs through ControlsPanel::
  // writeBack and emits ControlsPanel::changed — the host dialog wires that
  // signal to checkForChanges already.
}

void GamepadCaptureController::refreshUi() {
  const bool capturingConfirm = (m_target == Target::Confirm);
  const bool capturingBack = (m_target == Target::Back);
  const bool capturingToggleSidebar = (m_target == Target::ToggleSidebar);
  const bool capturingAny = capturingConfirm || capturingBack || capturingToggleSidebar;

  if (m_bindings.detectConfirmButton) {
    m_bindings.detectConfirmButton->setText(capturingConfirm ? tr("Press button...")
                                                             : tr("Detect..."));
    m_bindings.detectConfirmButton->setEnabled(!capturingBack && !capturingToggleSidebar);
  }
  if (m_bindings.detectBackButton) {
    m_bindings.detectBackButton->setText(capturingBack ? tr("Press button...") : tr("Detect..."));
    m_bindings.detectBackButton->setEnabled(!capturingConfirm && !capturingToggleSidebar);
  }
  if (m_bindings.detectToggleSidebarButton) {
    m_bindings.detectToggleSidebarButton->setText(capturingToggleSidebar ? tr("Press button...")
                                                                         : tr("Detect..."));
    m_bindings.detectToggleSidebarButton->setEnabled(!capturingConfirm && !capturingBack);
  }

  if (m_bindings.confirmEdit) {
    m_bindings.confirmEdit->setPlaceholderText(capturingConfirm ? tr("Press any button")
                                                                : QString());
  }
  if (m_bindings.backEdit) {
    m_bindings.backEdit->setPlaceholderText(capturingBack ? tr("Press any button") : QString());
  }
  if (m_bindings.toggleSidebarEdit) {
    m_bindings.toggleSidebarEdit->setPlaceholderText(capturingToggleSidebar ? tr("Press any button")
                                                                            : QString());
  }

  // While capturing, prevent the sibling D-Pad / left-stick checkboxes
  // from being changed — toggling them mid-capture would be confusing
  // because the capture state machine doesn't react to those events.
  if (m_bindings.useDpadCheckBox) {
    m_bindings.useDpadCheckBox->setEnabled(!capturingAny);
  }
  if (m_bindings.useLeftStickCheckBox) {
    m_bindings.useLeftStickCheckBox->setEnabled(!capturingAny);
  }
}
