#include "appearanceeffectspanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_appearanceeffectspanel.h"

#include <QCheckBox>
#include <QSpinBox>

AppearanceEffectsPanel::AppearanceEffectsPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::AppearanceEffectsPanel) {
  ui->setupUi(this);
  const auto onSpin = QOverload<int>::of(&QSpinBox::valueChanged);
  connect(ui->wallpaperParallaxCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
  connect(ui->parallaxStrengthSpinBox, onSpin, this, [this](int) { emit changed(); });
  connect(ui->toolbarBackdropBlurCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
  connect(ui->backdropBlurRadiusSpinBox, onSpin, this, [this](int) { emit changed(); });
}

AppearanceEffectsPanel::~AppearanceEffectsPanel() {
  delete ui;
}

void AppearanceEffectsPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void AppearanceEffectsPanel::load() {
  const CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  const CollectionConfig &config = *current;
  SettingsFormBinding::loadInto(ui->wallpaperParallaxCheckBox, config.background.wallpaperParallax);
  SettingsFormBinding::loadInto(ui->parallaxStrengthSpinBox, config.background.parallaxStrength);
  SettingsFormBinding::loadInto(ui->toolbarBackdropBlurCheckBox,
                                config.background.toolbarBackdropBlur);
  SettingsFormBinding::loadInto(ui->backdropBlurRadiusSpinBox,
                                config.background.backdropBlurRadius);
}

void AppearanceEffectsPanel::clear() {
  ui->wallpaperParallaxCheckBox->setChecked(false);
  ui->parallaxStrengthSpinBox->setValue(30);
  ui->toolbarBackdropBlurCheckBox->setChecked(false);
  ui->backdropBlurRadiusSpinBox->setValue(12);
}

void AppearanceEffectsPanel::save() {
  CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  CollectionConfig &config = *current;
  config.background.wallpaperParallax = ui->wallpaperParallaxCheckBox->isChecked();
  config.background.parallaxStrength = ui->parallaxStrengthSpinBox->value();
  config.background.toolbarBackdropBlur = ui->toolbarBackdropBlurCheckBox->isChecked();
  config.background.backdropBlurRadius = ui->backdropBlurRadiusSpinBox->value();
}

