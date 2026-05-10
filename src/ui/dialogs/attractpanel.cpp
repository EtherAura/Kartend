#include "attractpanel.h"

#include "collectionutils.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
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

void AttractPanel::setModel(SettingsModel *model) {
  m_model = model;
  refresh();
}

void AttractPanel::refresh() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  SettingsFormBinding::loadInto(ui->attractModeCheckBox,
                                m_model->generalSettings->attractModeEnabled);
  SettingsFormBinding::loadInto(ui->attractIdleTimeoutSpinBox,
                                m_model->generalSettings->attractModeIdleTimeoutSec);
  SettingsFormBinding::loadInto(ui->attractAutoScrollCheckBox,
                                m_model->generalSettings->attractModeAutoScrollEnabled);
  // QDoubleSpinBox needs its own loadInto overload (already provided in
  // SettingsFormBinding) — let it block its valueChanged signal during the
  // programmatic set.
  SettingsFormBinding::loadInto(ui->attractScrollSpeedSpinBox,
                                m_model->generalSettings->attractModeScrollSpeed);
  SettingsFormBinding::loadInto(ui->attractAdvanceSelectionCheckBox,
                                m_model->generalSettings->attractModeAdvanceSelectionEnabled);
  SettingsFormBinding::loadInto(ui->attractAdvanceIntervalSpinBox,
                                m_model->generalSettings->attractModeAdvanceSelectionIntervalSec);
  SettingsFormBinding::loadInto(ui->attractAdvanceRandomCheckBox,
                                m_model->generalSettings->attractModeAdvanceSelectionRandom);
}

void AttractPanel::writeBack() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  m_model->generalSettings->attractModeEnabled = ui->attractModeCheckBox->isChecked();
  m_model->generalSettings->attractModeIdleTimeoutSec = ui->attractIdleTimeoutSpinBox->value();
  m_model->generalSettings->attractModeAutoScrollEnabled =
      ui->attractAutoScrollCheckBox->isChecked();
  m_model->generalSettings->attractModeScrollSpeed = ui->attractScrollSpeedSpinBox->value();
  m_model->generalSettings->attractModeAdvanceSelectionEnabled =
      ui->attractAdvanceSelectionCheckBox->isChecked();
  m_model->generalSettings->attractModeAdvanceSelectionIntervalSec =
      ui->attractAdvanceIntervalSpinBox->value();
  m_model->generalSettings->attractModeAdvanceSelectionRandom =
      ui->attractAdvanceRandomCheckBox->isChecked();
  emit changed();
}
