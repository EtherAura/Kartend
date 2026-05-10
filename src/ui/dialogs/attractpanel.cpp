#include "attractpanel.h"

#include "collectionutils.h"
#include "settingsformbinding.h"
#include "ui_attractpanel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>

AttractPanel::AttractPanel(QWidget *parent) : QWidget(parent), ui(new Ui::AttractPanel) {
  ui->setupUi(this);

  const auto onSpin = QOverload<int>::of(&QSpinBox::valueChanged);
  const auto onDoubleSpin = QOverload<double>::of(&QDoubleSpinBox::valueChanged);

  connect(ui->attractModeCheckBox, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  connect(ui->attractIdleTimeoutSpinBox, onSpin, this, [this](int) { writeBack(); });
  connect(ui->attractAutoScrollCheckBox, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  connect(ui->attractScrollSpeedSpinBox, onDoubleSpin, this, [this](double) { writeBack(); });
  connect(ui->attractAdvanceSelectionCheckBox, &QCheckBox::toggled, this,
          [this](bool) { writeBack(); });
  connect(ui->attractAdvanceIntervalSpinBox, onSpin, this, [this](int) { writeBack(); });
  connect(ui->attractAdvanceRandomCheckBox, &QCheckBox::toggled, this,
          [this](bool) { writeBack(); });
}

AttractPanel::~AttractPanel() {
  delete ui;
}

void AttractPanel::setSettings(GeneralSettings *settings) {
  m_settings = settings;
  refresh();
}

void AttractPanel::refresh() {
  if (!m_settings) {
    return;
  }
  SettingsFormBinding::loadInto(ui->attractModeCheckBox, m_settings->attractModeEnabled);
  SettingsFormBinding::loadInto(ui->attractIdleTimeoutSpinBox,
                                m_settings->attractModeIdleTimeoutSec);
  SettingsFormBinding::loadInto(ui->attractAutoScrollCheckBox,
                                m_settings->attractModeAutoScrollEnabled);
  // QDoubleSpinBox needs its own loadInto overload (already provided in
  // SettingsFormBinding) — let it block its valueChanged signal during the
  // programmatic set.
  SettingsFormBinding::loadInto(ui->attractScrollSpeedSpinBox, m_settings->attractModeScrollSpeed);
  SettingsFormBinding::loadInto(ui->attractAdvanceSelectionCheckBox,
                                m_settings->attractModeAdvanceSelectionEnabled);
  SettingsFormBinding::loadInto(ui->attractAdvanceIntervalSpinBox,
                                m_settings->attractModeAdvanceSelectionIntervalSec);
  SettingsFormBinding::loadInto(ui->attractAdvanceRandomCheckBox,
                                m_settings->attractModeAdvanceSelectionRandom);
}

void AttractPanel::writeBack() {
  if (!m_settings) {
    return;
  }
  m_settings->attractModeEnabled = ui->attractModeCheckBox->isChecked();
  m_settings->attractModeIdleTimeoutSec = ui->attractIdleTimeoutSpinBox->value();
  m_settings->attractModeAutoScrollEnabled = ui->attractAutoScrollCheckBox->isChecked();
  m_settings->attractModeScrollSpeed = ui->attractScrollSpeedSpinBox->value();
  m_settings->attractModeAdvanceSelectionEnabled = ui->attractAdvanceSelectionCheckBox->isChecked();
  m_settings->attractModeAdvanceSelectionIntervalSec = ui->attractAdvanceIntervalSpinBox->value();
  m_settings->attractModeAdvanceSelectionRandom = ui->attractAdvanceRandomCheckBox->isChecked();
  emit changed();
}
