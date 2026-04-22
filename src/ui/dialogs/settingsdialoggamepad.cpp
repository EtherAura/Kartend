// Sibling translation unit for SettingsDialog: gamepad button capture.
#include <QKeySequence>

#include "gamepadmanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include "ui_settingsdialog.h"

void SettingsDialog::startGamepadButtonCapture(GamepadCaptureTarget target) {
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if (!mainWindow || !mainWindow->getInteractionManager() ||
      !mainWindow->getInteractionManager()->gamepadManager()) {
    QMessageBox::information(
        this, tr("Gamepad"),
        tr("Gamepad input is not available on this build/configuration."));
    return;
  }

  if (m_gamepadCaptureTarget == target) {
    stopGamepadButtonCapture();
    return;
  }

  stopGamepadButtonCapture();

  m_gamepadCaptureTarget = target;
  updateGamepadCaptureUi();

  auto *gamepadManager = mainWindow->getInteractionManager()->gamepadManager();
  gamepadManager->beginBindingCapture();
  m_gamepadCaptureConnection =
      connect(gamepadManager, &GamepadManager::bindingCaptureButtonPressed,
              this, [this](const QString &buttonName) {
                onGamepadCaptureButtonPressed(buttonName);
              });
}

void SettingsDialog::stopGamepadButtonCapture() {
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if (mainWindow && mainWindow->getInteractionManager() &&
      mainWindow->getInteractionManager()->gamepadManager()) {
    auto *gamepadManager =
        mainWindow->getInteractionManager()->gamepadManager();
    gamepadManager->endBindingCapture();
    QObject::disconnect(m_gamepadCaptureConnection);
    m_gamepadCaptureConnection = QMetaObject::Connection();
  }

  m_gamepadCaptureTarget = GamepadCaptureTarget::None;
  updateGamepadCaptureUi();
}

void SettingsDialog::onGamepadCaptureButtonPressed(const QString &buttonName) {
  if (m_gamepadCaptureTarget == GamepadCaptureTarget::Confirm) {
    if (ui->gamepadConfirmButtonLineEdit) {
      ui->gamepadConfirmButtonLineEdit->setText(buttonName);
    }
  } else if (m_gamepadCaptureTarget == GamepadCaptureTarget::Back) {
    if (ui->gamepadBackButtonLineEdit) {
      ui->gamepadBackButtonLineEdit->setText(buttonName);
    }
  } else if (m_gamepadCaptureTarget == GamepadCaptureTarget::ToggleSidebar) {
    if (ui->gamepadToggleSidebarButtonLineEdit) {
      ui->gamepadToggleSidebarButtonLineEdit->setText(buttonName);
    }
  }

  stopGamepadButtonCapture();
  checkForChanges();
}

void SettingsDialog::updateGamepadCaptureUi() {
  const bool capturingConfirm =
      (m_gamepadCaptureTarget == GamepadCaptureTarget::Confirm);
  const bool capturingBack =
      (m_gamepadCaptureTarget == GamepadCaptureTarget::Back);
  const bool capturingToggleSidebar =
      (m_gamepadCaptureTarget == GamepadCaptureTarget::ToggleSidebar);
  const bool capturingAny =
      capturingConfirm || capturingBack || capturingToggleSidebar;

  if (ui->detectGamepadConfirmButtonButton) {
    ui->detectGamepadConfirmButtonButton->setText(
        capturingConfirm ? tr("Press button...") : tr("Detect..."));
    ui->detectGamepadConfirmButtonButton->setEnabled(!capturingBack &&
                                                     !capturingToggleSidebar);
  }
  if (ui->detectGamepadBackButtonButton) {
    ui->detectGamepadBackButtonButton->setText(
        capturingBack ? tr("Press button...") : tr("Detect..."));
    ui->detectGamepadBackButtonButton->setEnabled(!capturingConfirm &&
                                                  !capturingToggleSidebar);
  }
  if (ui->detectGamepadToggleSidebarButtonButton) {
    ui->detectGamepadToggleSidebarButtonButton->setText(
        capturingToggleSidebar ? tr("Press button...") : tr("Detect..."));
    ui->detectGamepadToggleSidebarButtonButton->setEnabled(!capturingConfirm &&
                                                           !capturingBack);
  }

  if (ui->gamepadConfirmButtonLineEdit) {
    ui->gamepadConfirmButtonLineEdit->setPlaceholderText(
        capturingConfirm ? tr("Press any button") : QString());
  }
  if (ui->gamepadBackButtonLineEdit) {
    ui->gamepadBackButtonLineEdit->setPlaceholderText(
        capturingBack ? tr("Press any button") : QString());
  }
  if (ui->gamepadToggleSidebarButtonLineEdit) {
    ui->gamepadToggleSidebarButtonLineEdit->setPlaceholderText(
        capturingToggleSidebar ? tr("Press any button") : QString());
  }

  if (capturingAny) {
    // While capturing, prevent checkbox changes from being confusing.
    if (ui->gamepadUseDpadCheckBox) {
      ui->gamepadUseDpadCheckBox->setEnabled(false);
    }
    if (ui->gamepadUseLeftStickCheckBox) {
      ui->gamepadUseLeftStickCheckBox->setEnabled(false);
    }
  } else {
    if (ui->gamepadUseDpadCheckBox) {
      ui->gamepadUseDpadCheckBox->setEnabled(true);
    }
    if (ui->gamepadUseLeftStickCheckBox) {
      ui->gamepadUseLeftStickCheckBox->setEnabled(true);
    }
  }
}

// Sets up signal/slot connections and ensures tree updates immediately after
// saving changes
