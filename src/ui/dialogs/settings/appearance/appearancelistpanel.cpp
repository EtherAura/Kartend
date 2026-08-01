#include "appearancelistpanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_appearancelistpanel.h"

#include <QSpinBox>

AppearanceListPanel::AppearanceListPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::AppearanceListPanel) {
  ui->setupUi(this);
  const auto onSpin = QOverload<int>::of(&QSpinBox::valueChanged);
  connect(ui->listFontSizeSpinBox, onSpin, this, [this](int) { emit changed(); });
  connect(ui->listRowHeightSpinBox, onSpin, this, [this](int) { emit changed(); });
}

AppearanceListPanel::~AppearanceListPanel() {
  delete ui;
}

void AppearanceListPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void AppearanceListPanel::load() {
  const CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  const CollectionConfig &config = *current;
  SettingsFormBinding::loadInto(ui->listFontSizeSpinBox, config.listView.listFontSize);
  SettingsFormBinding::loadInto(ui->listRowHeightSpinBox, config.listView.listRowHeight);
}

void AppearanceListPanel::clear() {
  ui->listFontSizeSpinBox->setValue(12);
  ui->listRowHeightSpinBox->setValue(32);
}

void AppearanceListPanel::save() {
  CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  CollectionConfig &config = *current;
  config.listView.listFontSize = ui->listFontSizeSpinBox->value();
  config.listView.listRowHeight = ui->listRowHeightSpinBox->value();
}
