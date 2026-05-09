#include "gamepadcapturecontroller.h"

#include "gamepadmanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

namespace {
GamepadManager *resolveGamepadManager(QObject *parent) {
  auto *mainWindow = qobject_cast<MainWindow *>(parent);
  if (!mainWindow || !mainWindow->getInteractionManager() ||
      !mainWindow->getInteractionManager()->gamepadManager()) {
    return nullptr;
  }
  return mainWindow->getInteractionManager()->gamepadManager();
}
} // namespace

GamepadCaptureController::GamepadCaptureController(SettingsDialog *host)
    : QObject(host), m_host(host) {}

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
  if (!m_host) {
    return;
  }
  Ui::SettingsDialog *ui = m_host->ui;
  if (m_target == Target::Confirm) {
    if (ui->gamepadConfirmButtonLineEdit) {
      ui->gamepadConfirmButtonLineEdit->setText(buttonName);
    }
  } else if (m_target == Target::Back) {
    if (ui->gamepadBackButtonLineEdit) {
      ui->gamepadBackButtonLineEdit->setText(buttonName);
    }
  } else if (m_target == Target::ToggleSidebar) {
    if (ui->gamepadToggleSidebarButtonLineEdit) {
      ui->gamepadToggleSidebarButtonLineEdit->setText(buttonName);
    }
  }

  stop();
  m_host->checkForChanges();
}

void GamepadCaptureController::refreshUi() {
  if (!m_host) {
    return;
  }
  Ui::SettingsDialog *ui = m_host->ui;
  const bool capturingConfirm = (m_target == Target::Confirm);
  const bool capturingBack = (m_target == Target::Back);
  const bool capturingToggleSidebar = (m_target == Target::ToggleSidebar);
  const bool capturingAny = capturingConfirm || capturingBack || capturingToggleSidebar;

  if (ui->detectGamepadConfirmButtonButton) {
    ui->detectGamepadConfirmButtonButton->setText(capturingConfirm ? tr("Press button...")
                                                                   : tr("Detect..."));
    ui->detectGamepadConfirmButtonButton->setEnabled(!capturingBack && !capturingToggleSidebar);
  }
  if (ui->detectGamepadBackButtonButton) {
    ui->detectGamepadBackButtonButton->setText(capturingBack ? tr("Press button...")
                                                             : tr("Detect..."));
    ui->detectGamepadBackButtonButton->setEnabled(!capturingConfirm && !capturingToggleSidebar);
  }
  if (ui->detectGamepadToggleSidebarButtonButton) {
    ui->detectGamepadToggleSidebarButtonButton->setText(
        capturingToggleSidebar ? tr("Press button...") : tr("Detect..."));
    ui->detectGamepadToggleSidebarButtonButton->setEnabled(!capturingConfirm && !capturingBack);
  }

  if (ui->gamepadConfirmButtonLineEdit) {
    ui->gamepadConfirmButtonLineEdit->setPlaceholderText(capturingConfirm ? tr("Press any button")
                                                                          : QString());
  }
  if (ui->gamepadBackButtonLineEdit) {
    ui->gamepadBackButtonLineEdit->setPlaceholderText(capturingBack ? tr("Press any button")
                                                                    : QString());
  }
  if (ui->gamepadToggleSidebarButtonLineEdit) {
    ui->gamepadToggleSidebarButtonLineEdit->setPlaceholderText(
        capturingToggleSidebar ? tr("Press any button") : QString());
  }

  // While capturing, prevent the sibling D-Pad / left-stick checkboxes
  // from being changed — toggling them mid-capture would be confusing
  // because the capture state machine doesn't react to those events.
  if (ui->gamepadUseDpadCheckBox) {
    ui->gamepadUseDpadCheckBox->setEnabled(!capturingAny);
  }
  if (ui->gamepadUseLeftStickCheckBox) {
    ui->gamepadUseLeftStickCheckBox->setEnabled(!capturingAny);
  }
}
