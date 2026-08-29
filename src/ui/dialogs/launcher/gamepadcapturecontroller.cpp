#include "gamepadcapturecontroller.h"

#include "applicationcontext.h"
#include "gamepadmanager.h"
#include "iinteractionmanager.h"
#include "settingsdialog.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

namespace {
GamepadManager *resolveGamepadManager(const ApplicationContext *ctx) {
  auto *interaction = ctx ? ctx->interactionManager() : nullptr;
  return interaction ? interaction->gamepadManager() : nullptr;
}
} // namespace

GamepadCaptureController::GamepadCaptureController(SettingsDialog *host,
                                                   const ApplicationContext *ctx)
    : QObject(host), m_host(host), m_ctx(ctx) {}

void GamepadCaptureController::setWidgets(const Bindings &bindings) {
  m_bindings = bindings;
  refreshUi();
}

void GamepadCaptureController::start(Target target) {
  GamepadManager *gamepad = resolveGamepadManager(m_ctx);
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
  if (GamepadManager *gamepad = resolveGamepadManager(m_ctx)) {
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
  } else if (m_target == Target::ToggleCollectionTree && m_bindings.toggleCollectionTreeEdit) {
    m_bindings.toggleCollectionTreeEdit->setText(buttonName);
  }

  stop();
  // No explicit dirty-check call: setText() above triggered QLineEdit::
  // textChanged on the bound edit, which runs through ControlsPanel::
  // writeBack and emits ControlsPanel::changed — the host dialog wires that
  // signal to checkForChanges already.
}

void GamepadCaptureController::refreshUi() {
  const bool capturingAny = (m_target != Target::None);

  // Kartend-3de0c: expressed per-target rather than as "not any of the
  // others". The old pairwise form needed every button's condition rewritten
  // each time a target was added, and a fourth would have made three of them
  // wrong in a way nothing would catch. The rule was always the same: while a
  // capture is live only its own button stays live, because clicking it again
  // is how you cancel (start() toggles off on the same target).
  const auto applyDetectButton = [&](QPushButton *button, Target target) {
    if (!button) return;
    const bool capturingThis = (m_target == target);
    button->setText(capturingThis ? tr("Press button...") : tr("Detect..."));
    button->setEnabled(!capturingAny || capturingThis);
  };
  applyDetectButton(m_bindings.detectConfirmButton, Target::Confirm);
  applyDetectButton(m_bindings.detectBackButton, Target::Back);
  applyDetectButton(m_bindings.detectToggleSidebarButton, Target::ToggleSidebar);
  applyDetectButton(m_bindings.detectToggleCollectionTreeButton, Target::ToggleCollectionTree);

  const auto applyEditPlaceholder = [&](QLineEdit *edit, Target target) {
    if (!edit) return;
    edit->setPlaceholderText(m_target == target ? tr("Press any button") : QString());
  };
  applyEditPlaceholder(m_bindings.confirmEdit, Target::Confirm);
  applyEditPlaceholder(m_bindings.backEdit, Target::Back);
  applyEditPlaceholder(m_bindings.toggleSidebarEdit, Target::ToggleSidebar);
  applyEditPlaceholder(m_bindings.toggleCollectionTreeEdit, Target::ToggleCollectionTree);

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
