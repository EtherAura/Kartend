#include "appearancelayoutpanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_appearancelayoutpanel.h"
#include "uiconstants/grid.h"
#include "uiconstants/viewport.h"

#include <QComboBox>
#include <QSpinBox>

AppearanceLayoutPanel::AppearanceLayoutPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::AppearanceLayoutPanel) {
  ui->setupUi(this);
  const auto onSpin = QOverload<int>::of(&QSpinBox::valueChanged);
  const auto onCombo = QOverload<int>::of(&QComboBox::currentIndexChanged);

  connect(ui->viewTypeComboBox, onCombo, this, [this](int) { emit changed(); });
  connect(ui->horizontalAlignmentComboBox, onCombo, this, [this](int) { emit changed(); });
  connect(ui->gridWidthSpinBox, onSpin, this, [this](int) { emit changed(); });
  connect(ui->gridWidthSidebarHiddenSpinBox, onSpin, this, [this](int) { emit changed(); });
  connect(ui->horizontalGridHeightSpinBox, onSpin, this, [this](int) { emit changed(); });
  connect(ui->horizontalGridHeightSidebarHiddenSpinBox, onSpin, this,
          [this](int) { emit changed(); });
  connect(ui->horizontalSpacingSpinBox, onSpin, this, [this](int) { emit changed(); });
  connect(ui->verticalSpacingSpinBox, onSpin, this, [this](int) { emit changed(); });
  connect(ui->itemWidthSpinBox, onSpin, this, [this](int) { emit changed(); });
  connect(ui->itemHeightSpinBox, onSpin, this, [this](int) { emit changed(); });
  connect(ui->cornerRadiusSpinBox, onSpin, this, [this](int) { emit changed(); });
}

AppearanceLayoutPanel::~AppearanceLayoutPanel() {
  delete ui;
}

void AppearanceLayoutPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void AppearanceLayoutPanel::load() {
  const CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  const CollectionConfig &config = *current;
  SettingsFormBinding::loadIntoIndex(ui->viewTypeComboBox, static_cast<int>(config.viewType));
  SettingsFormBinding::loadIntoIndex(ui->horizontalAlignmentComboBox,
                                     static_cast<int>(config.horizontalAlignment));
  SettingsFormBinding::loadInto(ui->gridWidthSpinBox, config.gridLayout.gridWidth);
  SettingsFormBinding::loadInto(ui->gridWidthSidebarHiddenSpinBox,
                                config.gridLayout.gridWidthSidebarHidden);
  SettingsFormBinding::loadInto(ui->horizontalGridHeightSpinBox,
                                config.gridLayout.horizontalGridHeight);
  SettingsFormBinding::loadInto(ui->horizontalGridHeightSidebarHiddenSpinBox,
                                config.gridLayout.horizontalGridHeightSidebarHidden);
  SettingsFormBinding::loadInto(ui->horizontalSpacingSpinBox,
                                spacingInternalToUi(config.gridLayout.horizontalSpacing));
  SettingsFormBinding::loadInto(ui->verticalSpacingSpinBox,
                                spacingInternalToUi(config.gridLayout.verticalSpacing));
  SettingsFormBinding::loadInto(ui->itemWidthSpinBox, config.gridLayout.itemWidth);
  SettingsFormBinding::loadInto(ui->itemHeightSpinBox, config.gridLayout.itemHeight);
  SettingsFormBinding::loadInto(ui->cornerRadiusSpinBox, config.gridLayout.cornerRadius);
}

void AppearanceLayoutPanel::clear() {
  ui->viewTypeComboBox->setCurrentIndex(0);
  ui->horizontalAlignmentComboBox->setCurrentIndex(0);
  ui->gridWidthSpinBox->setValue(UIConstants::Grid::DEFAULT_WIDTH);
  ui->gridWidthSidebarHiddenSpinBox->setValue(0);
  ui->horizontalGridHeightSpinBox->setValue(0);
  ui->horizontalGridHeightSidebarHiddenSpinBox->setValue(0);
  ui->horizontalSpacingSpinBox->setValue(spacingInternalToUi(UIConstants::Grid::SPACING));
  ui->verticalSpacingSpinBox->setValue(spacingInternalToUi(UIConstants::Grid::SPACING));
  ui->itemWidthSpinBox->setValue(200);
  ui->itemHeightSpinBox->setValue(300);
  ui->cornerRadiusSpinBox->setValue(0);
}

void AppearanceLayoutPanel::save() {
  CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  CollectionConfig &config = *current;
  config.viewType = static_cast<ViewType>(ui->viewTypeComboBox->currentIndex());
  config.horizontalAlignment =
      static_cast<HorizontalAlignment>(ui->horizontalAlignmentComboBox->currentIndex());
  config.gridLayout.gridWidth = ui->gridWidthSpinBox->value();
  config.gridLayout.gridWidthSidebarHidden = ui->gridWidthSidebarHiddenSpinBox->value();
  config.gridLayout.horizontalGridHeight = ui->horizontalGridHeightSpinBox->value();
  config.gridLayout.horizontalGridHeightSidebarHidden =
      ui->horizontalGridHeightSidebarHiddenSpinBox->value();
  config.gridLayout.horizontalSpacing = spacingUiToInternal(ui->horizontalSpacingSpinBox->value());
  config.gridLayout.verticalSpacing = spacingUiToInternal(ui->verticalSpacingSpinBox->value());
  config.gridLayout.itemWidth = ui->itemWidthSpinBox->value();
  config.gridLayout.itemHeight = ui->itemHeightSpinBox->value();
  config.gridLayout.cornerRadius = ui->cornerRadiusSpinBox->value();
}

QSpinBox *AppearanceLayoutPanel::gridWidthSpinBox() const {
  return ui->gridWidthSpinBox;
}
QSpinBox *AppearanceLayoutPanel::gridWidthSidebarHiddenSpinBox() const {
  return ui->gridWidthSidebarHiddenSpinBox;
}
QSpinBox *AppearanceLayoutPanel::horizontalGridHeightSpinBox() const {
  return ui->horizontalGridHeightSpinBox;
}
QSpinBox *AppearanceLayoutPanel::horizontalGridHeightSidebarHiddenSpinBox() const {
  return ui->horizontalGridHeightSidebarHiddenSpinBox;
}
QSpinBox *AppearanceLayoutPanel::horizontalSpacingSpinBox() const {
  return ui->horizontalSpacingSpinBox;
}
QSpinBox *AppearanceLayoutPanel::verticalSpacingSpinBox() const {
  return ui->verticalSpacingSpinBox;
}
QComboBox *AppearanceLayoutPanel::viewTypeComboBox() const {
  return ui->viewTypeComboBox;
}

int AppearanceLayoutPanel::spacingInternalToUi(int spacing) {
  return spacing - UIConstants::Viewport::SPACING_MIN;
}
int AppearanceLayoutPanel::spacingUiToInternal(int spacing) {
  return spacing + UIConstants::Viewport::SPACING_MIN;
}
