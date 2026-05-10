#include "controlspanel.h"

#include "collectionutils.h"
#include "gamepadcapturecontroller.h"
#include "settingsformbinding.h"
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

void ControlsPanel::setSettings(GeneralSettings *settings) {
  m_settings = settings;
  refresh();
}

void ControlsPanel::refresh() {
  if (!m_settings) {
    return;
  }

  auto setKeyEdit = [](QKeySequenceEdit *edit, int key) {
    QSignalBlocker blocker(edit);
    edit->setKeySequence(QKeySequence(key));
  };
  setKeyEdit(ui->keyNavUpEdit, m_settings->keyNavUp);
  setKeyEdit(ui->keyNavDownEdit, m_settings->keyNavDown);
  setKeyEdit(ui->keyNavLeftEdit, m_settings->keyNavLeft);
  setKeyEdit(ui->keyNavRightEdit, m_settings->keyNavRight);
  setKeyEdit(ui->keyConfirmEdit, m_settings->keyConfirm);
  setKeyEdit(ui->keyBackEdit, m_settings->keyBack);
  setKeyEdit(ui->keySearchEdit, m_settings->keySearch);
  setKeyEdit(ui->keyHomeViewEdit, m_settings->keyHomeView);

  SettingsFormBinding::loadInto(ui->gamepadUseDpadCheckBox, m_settings->gamepadUseDpad);
  SettingsFormBinding::loadInto(ui->gamepadUseLeftStickCheckBox, m_settings->gamepadUseLeftStick);
  SettingsFormBinding::loadInto(ui->gamepadConfirmButtonLineEdit, m_settings->gamepadConfirmButton);
  SettingsFormBinding::loadInto(ui->gamepadBackButtonLineEdit, m_settings->gamepadBackButton);
  SettingsFormBinding::loadInto(ui->gamepadToggleSidebarButtonLineEdit,
                                m_settings->gamepadToggleSidebarButton);

  {
    QSignalBlocker blocker(ui->artworkCycleModifierComboBox);
    const int comboIdx =
        ui->artworkCycleModifierComboBox->findData(m_settings->artworkCycleModifier);
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
  if (!m_settings) {
    return;
  }
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
  m_settings->keyNavUp = singleKey(ui->keyNavUpEdit, m_settings->keyNavUp);
  m_settings->keyNavDown = singleKey(ui->keyNavDownEdit, m_settings->keyNavDown);
  m_settings->keyNavLeft = singleKey(ui->keyNavLeftEdit, m_settings->keyNavLeft);
  m_settings->keyNavRight = singleKey(ui->keyNavRightEdit, m_settings->keyNavRight);
  m_settings->keyConfirm = singleKey(ui->keyConfirmEdit, m_settings->keyConfirm);
  m_settings->keyBack = singleKey(ui->keyBackEdit, m_settings->keyBack);
  m_settings->keySearch = singleKey(ui->keySearchEdit, m_settings->keySearch);
  m_settings->keyHomeView = singleKey(ui->keyHomeViewEdit, 0);

  m_settings->gamepadUseDpad = ui->gamepadUseDpadCheckBox->isChecked();
  m_settings->gamepadUseLeftStick = ui->gamepadUseLeftStickCheckBox->isChecked();
  // Gamepad button strings: only commit non-empty values so a partial /
  // cancelled detect-button workflow doesn't blank the saved binding.
  const QString confirm = ui->gamepadConfirmButtonLineEdit->text().trimmed();
  if (!confirm.isEmpty()) {
    m_settings->gamepadConfirmButton = confirm;
  }
  const QString back = ui->gamepadBackButtonLineEdit->text().trimmed();
  if (!back.isEmpty()) {
    m_settings->gamepadBackButton = back;
  }
  const QString toggleSidebar = ui->gamepadToggleSidebarButtonLineEdit->text().trimmed();
  if (!toggleSidebar.isEmpty()) {
    m_settings->gamepadToggleSidebarButton = toggleSidebar;
  }

  // Validate the combo's data is a known modifier; ignore stale entries
  // rather than persisting garbage.
  const int rawModifier = ui->artworkCycleModifierComboBox->currentData().toInt();
  switch (rawModifier) {
  case static_cast<int>(Qt::ShiftModifier):
  case static_cast<int>(Qt::ControlModifier):
  case static_cast<int>(Qt::AltModifier):
  case static_cast<int>(Qt::MetaModifier):
    m_settings->artworkCycleModifier = rawModifier;
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
