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
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  SettingsFormBinding::loadInto(ui->listFontSizeSpinBox, config.listView.listFontSize);
  SettingsFormBinding::loadInto(ui->listRowHeightSpinBox, config.listView.listRowHeight);
}

void AppearanceListPanel::clear() {
  ui->listFontSizeSpinBox->setValue(12);
  ui->listRowHeightSpinBox->setValue(32);
}

void AppearanceListPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  config.listView.listFontSize = ui->listFontSizeSpinBox->value();
  config.listView.listRowHeight = ui->listRowHeightSpinBox->value();
}

bool AppearanceListPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  return ui->listFontSizeSpinBox->value() != o.listView.listFontSize ||
         ui->listRowHeightSpinBox->value() != o.listView.listRowHeight;
}
