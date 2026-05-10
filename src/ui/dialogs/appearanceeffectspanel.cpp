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
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  SettingsFormBinding::loadInto(ui->wallpaperParallaxCheckBox, config.wallpaperParallax);
  SettingsFormBinding::loadInto(ui->parallaxStrengthSpinBox, config.parallaxStrength);
  SettingsFormBinding::loadInto(ui->toolbarBackdropBlurCheckBox, config.toolbarBackdropBlur);
  SettingsFormBinding::loadInto(ui->backdropBlurRadiusSpinBox, config.backdropBlurRadius);
}

void AppearanceEffectsPanel::clear() {
  ui->wallpaperParallaxCheckBox->setChecked(false);
  ui->parallaxStrengthSpinBox->setValue(30);
  ui->toolbarBackdropBlurCheckBox->setChecked(false);
  ui->backdropBlurRadiusSpinBox->setValue(12);
}

void AppearanceEffectsPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  config.wallpaperParallax = ui->wallpaperParallaxCheckBox->isChecked();
  config.parallaxStrength = ui->parallaxStrengthSpinBox->value();
  config.toolbarBackdropBlur = ui->toolbarBackdropBlurCheckBox->isChecked();
  config.backdropBlurRadius = ui->backdropBlurRadiusSpinBox->value();
}

bool AppearanceEffectsPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  if (ui->wallpaperParallaxCheckBox->isChecked() != o.wallpaperParallax) return true;
  if (ui->parallaxStrengthSpinBox->value() != o.parallaxStrength) return true;
  if (ui->toolbarBackdropBlurCheckBox->isChecked() != o.toolbarBackdropBlur) return true;
  if (ui->backdropBlurRadiusSpinBox->value() != o.backdropBlurRadius) return true;
  return false;
}
