#include "controlspanel.h"

#include "collection/generalsettings.h"
#include "gamepadcapturecontroller.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_controlspanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <Qt>

ControlsPanel::ControlsPanel(QWidget *parent) : QWidget(parent), ui(new Ui::ControlsPanel) {
  ui->setupUi(this);
  populateArtworkCycleModifierCombo();
  connectChangeSignals();
}

ControlsPanel::~ControlsPanel() {
  delete ui;
}

void ControlsPanel::setModel(SettingsModel *model) {
  m_model = model;
  refresh();
}

void ControlsPanel::refresh() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;

  auto setKeyEdit = [](QKeySequenceEdit *edit, int key) {
    QSignalBlocker blocker(edit);
    edit->setKeySequence(QKeySequence(key));
  };
  setKeyEdit(ui->keyNavUpEdit, s->keyNavUp);
  setKeyEdit(ui->keyNavDownEdit, s->keyNavDown);
  setKeyEdit(ui->keyNavLeftEdit, s->keyNavLeft);
  setKeyEdit(ui->keyNavRightEdit, s->keyNavRight);
  setKeyEdit(ui->keyConfirmEdit, s->keyConfirm);
  setKeyEdit(ui->keyBackEdit, s->keyBack);
  setKeyEdit(ui->keySearchEdit, s->keySearch);
  setKeyEdit(ui->keyHomeViewEdit, s->keyHomeView);

  SettingsFormBinding::loadInto(ui->gamepadUseDpadCheckBox, s->gamepadUseDpad);
  SettingsFormBinding::loadInto(ui->gamepadUseLeftStickCheckBox, s->gamepadUseLeftStick);
  SettingsFormBinding::loadInto(ui->gamepadConfirmButtonLineEdit, s->gamepadConfirmButton);
  SettingsFormBinding::loadInto(ui->gamepadBackButtonLineEdit, s->gamepadBackButton);
  SettingsFormBinding::loadInto(ui->gamepadToggleSidebarButtonLineEdit,
                                s->gamepadToggleSidebarButton);

  {
    QSignalBlocker blocker(ui->artworkCycleModifierComboBox);
    const int comboIdx = ui->artworkCycleModifierComboBox->findData(s->artworkCycleModifier);
    ui->artworkCycleModifierComboBox->setCurrentIndex(comboIdx >= 0 ? comboIdx : 0);
  }
}

void ControlsPanel::installGamepadCaptureController(GamepadCaptureController *controller) {
  if (!controller) {
    return;
  }
  // Disconnect any prior wiring so a re-install drops the previous
  // controller's hooks.
  ui->detectGamepadConfirmButtonButton->disconnect();
  ui->detectGamepadBackButtonButton->disconnect();
  ui->detectGamepadToggleSidebarButtonButton->disconnect();
  connect(ui->detectGamepadConfirmButtonButton, &QPushButton::clicked, controller,
          [controller]() { controller->start(GamepadCaptureController::Target::Confirm); });
  connect(ui->detectGamepadBackButtonButton, &QPushButton::clicked, controller,
          [controller]() { controller->start(GamepadCaptureController::Target::Back); });
  connect(ui->detectGamepadToggleSidebarButtonButton, &QPushButton::clicked, controller,
          [controller]() { controller->start(GamepadCaptureController::Target::ToggleSidebar); });
}

QLineEdit *ControlsPanel::gamepadConfirmLineEdit() const {
  return ui->gamepadConfirmButtonLineEdit;
}
QLineEdit *ControlsPanel::gamepadBackLineEdit() const {
  return ui->gamepadBackButtonLineEdit;
}
QLineEdit *ControlsPanel::gamepadToggleSidebarLineEdit() const {
  return ui->gamepadToggleSidebarButtonLineEdit;
}
QPushButton *ControlsPanel::detectConfirmButton() const {
  return ui->detectGamepadConfirmButtonButton;
}
QPushButton *ControlsPanel::detectBackButton() const {
  return ui->detectGamepadBackButtonButton;
}
QPushButton *ControlsPanel::detectToggleSidebarButton() const {
  return ui->detectGamepadToggleSidebarButtonButton;
}
QCheckBox *ControlsPanel::useDpadCheckBox() const {
  return ui->gamepadUseDpadCheckBox;
}
QCheckBox *ControlsPanel::useLeftStickCheckBox() const {
  return ui->gamepadUseLeftStickCheckBox;
}

void ControlsPanel::writeBack() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;
  // Single-key extraction with fallback: leave the existing value when the
  // user clears the edit (so partial typing doesn't unbind the shortcut),
  // except for keyHomeView which intentionally allows 0 = unbound.
  auto singleKey = [](QKeySequenceEdit *edit, int fallback) {
    const QKeySequence seq = edit->keySequence();
    if (seq.isEmpty()) {
      return fallback;
    }
    const int keyOnly = static_cast<int>(seq[0].key());
    return (keyOnly != 0) ? keyOnly : fallback;
  };
  s->keyNavUp = singleKey(ui->keyNavUpEdit, s->keyNavUp);
  s->keyNavDown = singleKey(ui->keyNavDownEdit, s->keyNavDown);
  s->keyNavLeft = singleKey(ui->keyNavLeftEdit, s->keyNavLeft);
  s->keyNavRight = singleKey(ui->keyNavRightEdit, s->keyNavRight);
  s->keyConfirm = singleKey(ui->keyConfirmEdit, s->keyConfirm);
  s->keyBack = singleKey(ui->keyBackEdit, s->keyBack);
  s->keySearch = singleKey(ui->keySearchEdit, s->keySearch);
  s->keyHomeView = singleKey(ui->keyHomeViewEdit, 0);

  s->gamepadUseDpad = ui->gamepadUseDpadCheckBox->isChecked();
  s->gamepadUseLeftStick = ui->gamepadUseLeftStickCheckBox->isChecked();
  // Gamepad button strings: only commit non-empty values so a partial /
  // cancelled detect-button workflow doesn't blank the saved binding.
  const QString confirm = ui->gamepadConfirmButtonLineEdit->text().trimmed();
  if (!confirm.isEmpty()) {
    s->gamepadConfirmButton = confirm;
  }
  const QString back = ui->gamepadBackButtonLineEdit->text().trimmed();
  if (!back.isEmpty()) {
    s->gamepadBackButton = back;
  }
  const QString toggleSidebar = ui->gamepadToggleSidebarButtonLineEdit->text().trimmed();
  if (!toggleSidebar.isEmpty()) {
    s->gamepadToggleSidebarButton = toggleSidebar;
  }

  // Validate the combo's data is a known modifier; ignore stale entries
  // rather than persisting garbage.
  const int rawModifier = ui->artworkCycleModifierComboBox->currentData().toInt();
  switch (rawModifier) {
  case static_cast<int>(Qt::ShiftModifier):
  case static_cast<int>(Qt::ControlModifier):
  case static_cast<int>(Qt::AltModifier):
  case static_cast<int>(Qt::MetaModifier):
    s->artworkCycleModifier = rawModifier;
    break;
  default:
    break;
  }

  emit changed();
}

void ControlsPanel::connectChangeSignals() {
  for (auto *edit : {ui->keyNavUpEdit, ui->keyNavDownEdit, ui->keyNavLeftEdit, ui->keyNavRightEdit,
                     ui->keyConfirmEdit, ui->keyBackEdit, ui->keySearchEdit, ui->keyHomeViewEdit}) {
    connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
            [this](const QKeySequence &) { writeBack(); });
  }
  for (auto *edit : {ui->gamepadConfirmButtonLineEdit, ui->gamepadBackButtonLineEdit,
                     ui->gamepadToggleSidebarButtonLineEdit}) {
    connect(edit, &QLineEdit::textChanged, this, [this](const QString &) { writeBack(); });
  }
  connect(ui->gamepadUseDpadCheckBox, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  connect(ui->gamepadUseLeftStickCheckBox, &QCheckBox::toggled, this,
          [this](bool) { writeBack(); });
  connect(ui->artworkCycleModifierComboBox, &QComboBox::currentIndexChanged, this,
          [this](int) { writeBack(); });
}

void ControlsPanel::populateArtworkCycleModifierCombo() {
  // Order matches what users tend to reach for first; Meta last because
  // Win/Cmd is the most likely to clash with a global shortcut.
  ui->artworkCycleModifierComboBox->addItem(tr("Shift"), static_cast<int>(Qt::ShiftModifier));
  ui->artworkCycleModifierComboBox->addItem(tr("Ctrl"), static_cast<int>(Qt::ControlModifier));
  ui->artworkCycleModifierComboBox->addItem(tr("Alt"), static_cast<int>(Qt::AltModifier));
  ui->artworkCycleModifierComboBox->addItem(tr("Meta (Win/Cmd)"),
                                            static_cast<int>(Qt::MetaModifier));
}
