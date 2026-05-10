#include "appearancecolorspanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_appearancecolorspanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>

AppearanceColorsPanel::AppearanceColorsPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::AppearanceColorsPanel) {
  ui->setupUi(this);

  const auto onSpin = QOverload<int>::of(&QSpinBox::valueChanged);

  // Per-collection inputs → changed() for dirty detection.
  connect(ui->backgroundColorRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked) updateBackgroundButtonForType();
    emit changed();
  });
  connect(ui->backgroundImageRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked) updateBackgroundButtonForType();
    emit changed();
  });
  connect(ui->backgroundVideoRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked) updateBackgroundButtonForType();
    emit changed();
  });
  connect(ui->backgroundValueEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->primaryColorEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->tileColorEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->selectionColorEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->listRowColorEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->listAltRowColorEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->vignetteEnabledCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
  connect(ui->vignetteIntensitySpinBox, onSpin, this, [this](int) { emit changed(); });

  // Global title-tint spin boxes → write through to GeneralSettings pointee +
  // changed() (deferred-save like the rest of the dialog).
  connect(ui->titleSaturationSpinBox, onSpin, this, [this](int) {
    writeBackGlobals();
    emit changed();
  });
  connect(ui->titleLightnessSpinBox, onSpin, this, [this](int) {
    writeBackGlobals();
    emit changed();
  });

  // baseColorEdit: write through + baseColorChanged() so host live-saves and
  // applies ItemWidget::setTitleBaseColor immediately.
  connect(ui->baseColorEdit, &QLineEdit::editingFinished, this, [this]() {
    writeBackGlobals();
    emit baseColorChanged(ui->baseColorEdit->text().trimmed());
  });

  // Browse buttons.
  connect(ui->browseBackgroundButton, &QPushButton::clicked, this,
          &AppearanceColorsPanel::onBrowseBackground);
  connect(ui->browseColorButton, &QPushButton::clicked, this,
          &AppearanceColorsPanel::onBrowseBaseColor);
  connect(ui->browsePrimaryColorButton, &QPushButton::clicked, this,
          &AppearanceColorsPanel::onBrowsePrimaryColor);
  connect(ui->browseTileColorButton, &QPushButton::clicked, this,
          &AppearanceColorsPanel::onBrowseTileColor);
  connect(ui->browseSelectionColorButton, &QPushButton::clicked, this,
          &AppearanceColorsPanel::onBrowseSelectionColor);
  connect(ui->browseListRowColorButton, &QPushButton::clicked, this,
          &AppearanceColorsPanel::onBrowseListRowColor);
  connect(ui->browseListAltRowColorButton, &QPushButton::clicked, this,
          &AppearanceColorsPanel::onBrowseListAltRowColor);
}

AppearanceColorsPanel::~AppearanceColorsPanel() {
  delete ui;
}

void AppearanceColorsPanel::setModel(SettingsModel *model) {
  m_model = model;
  refresh();
}

void AppearanceColorsPanel::load() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 ||
      *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  if (config.backgroundType == BackgroundType::Video) {
    ui->backgroundVideoRadio->setChecked(true);
    ui->backgroundValueEdit->setText(config.backgroundVideo);
  } else if (config.backgroundType == BackgroundType::Image) {
    ui->backgroundImageRadio->setChecked(true);
    ui->backgroundValueEdit->setText(config.backgroundImage);
  } else {
    ui->backgroundColorRadio->setChecked(true);
    ui->backgroundValueEdit->setText(config.backgroundColor);
  }
  updateBackgroundButtonForType();

  ui->primaryColorEdit->setText(config.primaryColor);
  ui->tileColorEdit->setText(config.tileColor);
  ui->selectionColorEdit->setText(config.selectionColor);
  ui->listRowColorEdit->setText(config.listRowColor);
  ui->listAltRowColorEdit->setText(config.listAltRowColor);
  ui->vignetteEnabledCheckBox->setChecked(config.vignetteEnabled);
  ui->vignetteIntensitySpinBox->setValue(config.vignetteIntensity);
}

void AppearanceColorsPanel::clear() {
  ui->backgroundColorRadio->setChecked(true);
  ui->backgroundValueEdit->clear();
  updateBackgroundButtonForType();
  ui->primaryColorEdit->clear();
  ui->tileColorEdit->clear();
  ui->selectionColorEdit->clear();
  ui->listRowColorEdit->clear();
  ui->listAltRowColorEdit->clear();
  ui->vignetteEnabledCheckBox->setChecked(false);
  ui->vignetteIntensitySpinBox->setValue(60);
}

void AppearanceColorsPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 ||
      *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  if (ui->backgroundVideoRadio->isChecked()) {
    config.backgroundType = BackgroundType::Video;
  } else if (ui->backgroundImageRadio->isChecked()) {
    config.backgroundType = BackgroundType::Image;
  } else {
    config.backgroundType = BackgroundType::Color;
  }

  const QString value = ui->backgroundValueEdit->text().trimmed();
  // Only the field matching the active type holds a value; the other two
  // are cleared so an old value can't bleed back when the user toggles.
  if (config.backgroundType == BackgroundType::Video) {
    config.backgroundVideo = value;
    config.backgroundColor.clear();
    config.backgroundImage.clear();
  } else if (config.backgroundType == BackgroundType::Image) {
    config.backgroundImage = value;
    config.backgroundColor.clear();
    config.backgroundVideo.clear();
  } else {
    config.backgroundColor = value;
    config.backgroundImage.clear();
    config.backgroundVideo.clear();
  }

  config.primaryColor = ui->primaryColorEdit->text().trimmed();
  config.tileColor = ui->tileColorEdit->text().trimmed();
  config.selectionColor = ui->selectionColorEdit->text().trimmed();
  config.listRowColor = ui->listRowColorEdit->text().trimmed();
  config.listAltRowColor = ui->listAltRowColorEdit->text().trimmed();
  config.vignetteEnabled = ui->vignetteEnabledCheckBox->isChecked();
  config.vignetteIntensity = ui->vignetteIntensitySpinBox->value();
}

bool AppearanceColorsPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  BackgroundType currentType = BackgroundType::Color;
  if (ui->backgroundVideoRadio->isChecked()) {
    currentType = BackgroundType::Video;
  } else if (ui->backgroundImageRadio->isChecked()) {
    currentType = BackgroundType::Image;
  }
  if (currentType != o.backgroundType) return true;

  const QString currentValue = ui->backgroundValueEdit->text().trimmed();
  if (currentType == BackgroundType::Video) {
    if (currentValue != o.backgroundVideo) return true;
  } else if (currentType == BackgroundType::Image) {
    if (currentValue != o.backgroundImage) return true;
  } else {
    if (currentValue != o.backgroundColor) return true;
  }

  if (ui->primaryColorEdit->text().trimmed() != o.primaryColor) return true;
  if (ui->tileColorEdit->text().trimmed() != o.tileColor) return true;
  if (ui->selectionColorEdit->text().trimmed() != o.selectionColor) return true;
  if (ui->listRowColorEdit->text().trimmed() != o.listRowColor) return true;
  if (ui->listAltRowColorEdit->text().trimmed() != o.listAltRowColor) return true;
  if (ui->vignetteEnabledCheckBox->isChecked() != o.vignetteEnabled) return true;
  if (ui->vignetteIntensitySpinBox->value() != o.vignetteIntensity) return true;
  return false;
}

void AppearanceColorsPanel::refresh() {
  if (!m_model || !m_model->generalSettings) return;
  SettingsFormBinding::loadInto(ui->titleSaturationSpinBox,
                                m_model->generalSettings->titleTintSaturation);
  SettingsFormBinding::loadInto(ui->titleLightnessSpinBox,
                                m_model->generalSettings->titleTintLightness);
  SettingsFormBinding::loadInto(ui->baseColorEdit, m_model->generalSettings->titleBaseColor);
}

void AppearanceColorsPanel::writeBackGlobals() {
  if (!m_model || !m_model->generalSettings) return;
  m_model->generalSettings->titleTintSaturation = ui->titleSaturationSpinBox->value();
  m_model->generalSettings->titleTintLightness = ui->titleLightnessSpinBox->value();
  m_model->generalSettings->titleBaseColor = ui->baseColorEdit->text().trimmed();
}

void AppearanceColorsPanel::updateBackgroundButtonForType() {
  if (ui->backgroundColorRadio->isChecked()) {
    ui->browseBackgroundButton->setText(tr("Pick..."));
    ui->browseBackgroundButton->setToolTip(tr("Select background color"));
    ui->backgroundValueEdit->setPlaceholderText(tr("Default"));
    ui->backgroundValueEdit->setToolTip(tr("Background color (hex format like #FF5500)"));
  } else if (ui->backgroundImageRadio->isChecked()) {
    ui->browseBackgroundButton->setText(tr("Browse..."));
    ui->browseBackgroundButton->setToolTip(tr("Select background image"));
    ui->backgroundValueEdit->setPlaceholderText(tr("None"));
    ui->backgroundValueEdit->setToolTip(tr("Path to background image file"));
  } else if (ui->backgroundVideoRadio->isChecked()) {
    ui->browseBackgroundButton->setText(tr("Browse..."));
    ui->browseBackgroundButton->setToolTip(tr("Select background video"));
    ui->backgroundValueEdit->setPlaceholderText(tr("None"));
    ui->backgroundValueEdit->setToolTip(tr("Path to a looping muted background video"));
  }
}

void AppearanceColorsPanel::onBrowseBackground() {
  if (ui->backgroundVideoRadio->isChecked()) {
    const QString currentPath = ui->backgroundValueEdit->text().trimmed();
    const QString startDir =
        currentPath.isEmpty() ? QDir::homePath() : QFileInfo(currentPath).absolutePath();
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Select Background Video"), startDir,
        tr("Videos (*.mp4 *.webm *.mkv *.mov *.avi *.m4v);;All Files (*)"));
    if (!filePath.isEmpty()) {
      ui->backgroundValueEdit->setText(filePath);
    }
    return;
  }
  if (ui->backgroundColorRadio->isChecked()) {
    QColor currentColor = Qt::white;
    const QString currentValue = ui->backgroundValueEdit->text().trimmed();
    if (!currentValue.isEmpty()) {
      currentColor = QColor(currentValue);
    }
    QColor color = QColorDialog::getColor(currentColor, this, tr("Select Background Color"));
    if (color.isValid()) {
      ui->backgroundValueEdit->setText(color.name());
    }
  } else {
    const QString currentPath = ui->backgroundValueEdit->text().trimmed();
    const QString startDir =
        currentPath.isEmpty() ? QDir::homePath() : QFileInfo(currentPath).absolutePath();
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Select Background Image"), startDir,
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
    if (!filePath.isEmpty()) {
      ui->backgroundValueEdit->setText(filePath);
    }
  }
}

void AppearanceColorsPanel::onBrowseBaseColor() {
  QColor currentColor = Qt::white;
  const QString currentValue = ui->baseColorEdit->text().trimmed();
  if (!currentValue.isEmpty()) {
    currentColor = QColor(currentValue);
  }
  QColor color = QColorDialog::getColor(currentColor, this, tr("Select Base Color"));
  if (color.isValid()) {
    ui->baseColorEdit->setText(color.name());
    writeBackGlobals();
    emit baseColorChanged(color.name());
  }
}

void AppearanceColorsPanel::onBrowsePrimaryColor() {
  QColor currentColor = Qt::gray;
  const QString currentValue = ui->primaryColorEdit->text().trimmed();
  if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
    currentColor = QColor(currentValue);
  }
  QColor color = QColorDialog::getColor(currentColor, this, tr("Select Primary Color"));
  if (color.isValid()) {
    ui->primaryColorEdit->setText(color.name());
  }
}

void AppearanceColorsPanel::onBrowseTileColor() {
  QColor currentColor = Qt::gray;
  const QString currentValue = ui->tileColorEdit->text().trimmed();
  if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
    currentColor = QColor(currentValue);
  }
  QColor color = QColorDialog::getColor(currentColor, this, tr("Select Tile Color"));
  if (color.isValid()) {
    ui->tileColorEdit->setText(color.name());
  }
}

void AppearanceColorsPanel::onBrowseSelectionColor() {
  QColor currentColor = Qt::gray;
  const QString currentValue = ui->selectionColorEdit->text().trimmed();
  if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
    currentColor = QColor(currentValue);
  }
  QColor color = QColorDialog::getColor(currentColor, this, tr("Select Selection Color"));
  if (color.isValid()) {
    ui->selectionColorEdit->setText(color.name());
  }
}

void AppearanceColorsPanel::onBrowseListRowColor() {
  QColor currentColor = Qt::gray;
  const QString currentValue = ui->listRowColorEdit->text().trimmed();
  if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
    currentColor = QColor(currentValue);
  }
  QColor color = QColorDialog::getColor(currentColor, this, tr("Select Row Color"));
  if (color.isValid()) {
    ui->listRowColorEdit->setText(color.name());
  }
}

void AppearanceColorsPanel::onBrowseListAltRowColor() {
  QColor currentColor = Qt::gray;
  const QString currentValue = ui->listAltRowColorEdit->text().trimmed();
  if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
    currentColor = QColor(currentValue);
  }
  QColor color = QColorDialog::getColor(currentColor, this, tr("Select Alternate Row Color"));
  if (color.isValid()) {
    ui->listAltRowColorEdit->setText(color.name());
  }
}
