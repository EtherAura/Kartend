#include "appearancelistpanel.h"

#include "settingsformbinding.h"
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

void AppearanceListPanel::load(const CollectionConfig &config) {
  SettingsFormBinding::loadInto(ui->listFontSizeSpinBox, config.listFontSize);
  SettingsFormBinding::loadInto(ui->listRowHeightSpinBox, config.listRowHeight);
}

void AppearanceListPanel::clear() {
  ui->listFontSizeSpinBox->setValue(12);
  ui->listRowHeightSpinBox->setValue(32);
}

void AppearanceListPanel::save(CollectionConfig &config) const {
  config.listFontSize = ui->listFontSizeSpinBox->value();
  config.listRowHeight = ui->listRowHeightSpinBox->value();
}

bool AppearanceListPanel::hasChanges(const CollectionConfig &o) const {
  return ui->listFontSizeSpinBox->value() != o.listFontSize ||
         ui->listRowHeightSpinBox->value() != o.listRowHeight;
}
