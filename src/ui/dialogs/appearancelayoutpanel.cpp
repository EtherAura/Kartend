#include "appearancelayoutpanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_appearancelayoutpanel.h"
#include "uiconstants.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>

AppearanceLayoutPanel::AppearanceLayoutPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::AppearanceLayoutPanel) {
  ui->setupUi(this);
  const auto onSpin = QOverload<int>::of(&QSpinBox::valueChanged);
  const auto onCombo = QOverload<int>::of(&QComboBox::currentIndexChanged);

  connect(ui->viewTypeComboBox, onCombo, this, [this](int) { emit changed(); });
  connect(ui->hideMissingArtworkCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
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
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  SettingsFormBinding::loadIntoIndex(ui->viewTypeComboBox, static_cast<int>(config.viewType));
  SettingsFormBinding::loadInto(ui->hideMissingArtworkCheckBox, config.hideMissingArtwork);
  SettingsFormBinding::loadIntoIndex(ui->horizontalAlignmentComboBox,
                                     static_cast<int>(config.horizontalAlignment));
  SettingsFormBinding::loadInto(ui->gridWidthSpinBox, config.gridWidth);
  SettingsFormBinding::loadInto(ui->gridWidthSidebarHiddenSpinBox, config.gridWidthSidebarHidden);
  SettingsFormBinding::loadInto(ui->horizontalGridHeightSpinBox, config.horizontalGridHeight);
  SettingsFormBinding::loadInto(ui->horizontalGridHeightSidebarHiddenSpinBox,
                                config.horizontalGridHeightSidebarHidden);
  SettingsFormBinding::loadInto(ui->horizontalSpacingSpinBox,
                                spacingInternalToUi(config.horizontalSpacing));
  SettingsFormBinding::loadInto(ui->verticalSpacingSpinBox,
                                spacingInternalToUi(config.verticalSpacing));
  SettingsFormBinding::loadInto(ui->itemWidthSpinBox, config.itemWidth);
  SettingsFormBinding::loadInto(ui->itemHeightSpinBox, config.itemHeight);
  SettingsFormBinding::loadInto(ui->cornerRadiusSpinBox, config.cornerRadius);
}

void AppearanceLayoutPanel::clear() {
  ui->viewTypeComboBox->setCurrentIndex(0);
  ui->hideMissingArtworkCheckBox->setChecked(false);
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

void AppearanceLayoutPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  config.viewType = static_cast<ViewType>(ui->viewTypeComboBox->currentIndex());
  config.hideMissingArtwork = ui->hideMissingArtworkCheckBox->isChecked();
  config.horizontalAlignment =
      static_cast<HorizontalAlignment>(ui->horizontalAlignmentComboBox->currentIndex());
  config.gridWidth = ui->gridWidthSpinBox->value();
  config.gridWidthSidebarHidden = ui->gridWidthSidebarHiddenSpinBox->value();
  config.horizontalGridHeight = ui->horizontalGridHeightSpinBox->value();
  config.horizontalGridHeightSidebarHidden = ui->horizontalGridHeightSidebarHiddenSpinBox->value();
  config.horizontalSpacing = spacingUiToInternal(ui->horizontalSpacingSpinBox->value());
  config.verticalSpacing = spacingUiToInternal(ui->verticalSpacingSpinBox->value());
  config.itemWidth = ui->itemWidthSpinBox->value();
  config.itemHeight = ui->itemHeightSpinBox->value();
  config.cornerRadius = ui->cornerRadiusSpinBox->value();
}

bool AppearanceLayoutPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  if (ui->viewTypeComboBox->currentIndex() != static_cast<int>(o.viewType)) return true;
  if (ui->hideMissingArtworkCheckBox->isChecked() != o.hideMissingArtwork) return true;
  if (ui->horizontalAlignmentComboBox->currentIndex() != static_cast<int>(o.horizontalAlignment))
    return true;
  if (ui->gridWidthSpinBox->value() != o.gridWidth) return true;
  if (ui->gridWidthSidebarHiddenSpinBox->value() != o.gridWidthSidebarHidden) return true;
  if (ui->horizontalGridHeightSpinBox->value() != o.horizontalGridHeight) return true;
  if (ui->horizontalGridHeightSidebarHiddenSpinBox->value() != o.horizontalGridHeightSidebarHidden)
    return true;
  if (spacingUiToInternal(ui->horizontalSpacingSpinBox->value()) != o.horizontalSpacing)
    return true;
  if (spacingUiToInternal(ui->verticalSpacingSpinBox->value()) != o.verticalSpacing) return true;
  if (ui->itemWidthSpinBox->value() != o.itemWidth) return true;
  if (ui->itemHeightSpinBox->value() != o.itemHeight) return true;
  if (ui->cornerRadiusSpinBox->value() != o.cornerRadius) return true;
  return false;
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
