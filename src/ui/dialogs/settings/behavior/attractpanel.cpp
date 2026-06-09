#include "attractpanel.h"

#include "collectiontypes.h"
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

  connect(ui->attractModeCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->attractIdleTimeoutSpinBox, onSpin, this, [this](int) { save(); });
  connect(ui->attractAutoScrollCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->attractScrollSpeedSpinBox, onDoubleSpin, this, [this](double) { save(); });
  connect(ui->attractAdvanceSelectionCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->attractAdvanceIntervalSpinBox, onSpin, this, [this](int) { save(); });
  connect(ui->attractAdvanceRandomCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
}

AttractPanel::~AttractPanel() {
  delete ui;
}

void AttractPanel::setModel(SettingsModel *model) {
  m_model = model;
  load();
}

void AttractPanel::load() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  SettingsFormBinding::loadInto(ui->attractModeCheckBox,
                                m_model->generalSettings->attract.attractModeEnabled);
  SettingsFormBinding::loadInto(ui->attractIdleTimeoutSpinBox,
                                m_model->generalSettings->attract.attractModeIdleTimeoutSec);
  SettingsFormBinding::loadInto(ui->attractAutoScrollCheckBox,
                                m_model->generalSettings->attract.attractModeAutoScrollEnabled);
  // QDoubleSpinBox needs its own loadInto overload (already provided in
  // SettingsFormBinding) — let it block its valueChanged signal during the
  // programmatic set.
  SettingsFormBinding::loadInto(ui->attractScrollSpeedSpinBox,
                                m_model->generalSettings->attract.attractModeScrollSpeed);
  SettingsFormBinding::loadInto(
      ui->attractAdvanceSelectionCheckBox,
      m_model->generalSettings->attract.attractModeAdvanceSelectionEnabled);
  SettingsFormBinding::loadInto(
      ui->attractAdvanceIntervalSpinBox,
      m_model->generalSettings->attract.attractModeAdvanceSelectionIntervalSec);
  SettingsFormBinding::loadInto(
      ui->attractAdvanceRandomCheckBox,
      m_model->generalSettings->attract.attractModeAdvanceSelectionRandom);
}

void AttractPanel::save() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  m_model->generalSettings->attract.attractModeEnabled = ui->attractModeCheckBox->isChecked();
  m_model->generalSettings->attract.attractModeIdleTimeoutSec =
      ui->attractIdleTimeoutSpinBox->value();
  m_model->generalSettings->attract.attractModeAutoScrollEnabled =
      ui->attractAutoScrollCheckBox->isChecked();
  m_model->generalSettings->attract.attractModeScrollSpeed = ui->attractScrollSpeedSpinBox->value();
  m_model->generalSettings->attract.attractModeAdvanceSelectionEnabled =
      ui->attractAdvanceSelectionCheckBox->isChecked();
  m_model->generalSettings->attract.attractModeAdvanceSelectionIntervalSec =
      ui->attractAdvanceIntervalSpinBox->value();
  m_model->generalSettings->attract.attractModeAdvanceSelectionRandom =
      ui->attractAdvanceRandomCheckBox->isChecked();
  emit changed();
}
