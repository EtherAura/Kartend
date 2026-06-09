#include "appearancecolorspanel.h"

#include "kdecolorscheme.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_appearancecolorspanel.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>

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
  connect(ui->vignetteEnabledCheckBox, &QCheckBox::toggled, this, [this](bool) { emit changed(); });
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

  // The color-scheme loader lives in its own "Color Scheme" group box in
  // appearancecolorspanel.ui — a real container, consistent with the other
  // sections. Wire its click here.
  connect(ui->loadColorSchemeButton, &QPushButton::clicked, this,
          &AppearanceColorsPanel::onLoadColorScheme);
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
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  if (config.background.backgroundType == BackgroundType::Video) {
    ui->backgroundVideoRadio->setChecked(true);
    ui->backgroundValueEdit->setText(config.background.backgroundVideo);
  } else if (config.background.backgroundType == BackgroundType::Image) {
    ui->backgroundImageRadio->setChecked(true);
    ui->backgroundValueEdit->setText(config.background.backgroundImage);
  } else {
    ui->backgroundColorRadio->setChecked(true);
    ui->backgroundValueEdit->setText(config.background.backgroundColor);
  }
  updateBackgroundButtonForType();

  ui->primaryColorEdit->setText(config.background.primaryColor);
  ui->tileColorEdit->setText(config.background.tileColor);
  ui->selectionColorEdit->setText(config.background.selectionColor);
  ui->listRowColorEdit->setText(config.listView.listRowColor);
  ui->listAltRowColorEdit->setText(config.listView.listAltRowColor);
  ui->vignetteEnabledCheckBox->setChecked(config.background.vignetteEnabled);
  ui->vignetteIntensitySpinBox->setValue(config.background.vignetteIntensity);
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

void AppearanceColorsPanel::save() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  if (ui->backgroundVideoRadio->isChecked()) {
    config.background.backgroundType = BackgroundType::Video;
  } else if (ui->backgroundImageRadio->isChecked()) {
    config.background.backgroundType = BackgroundType::Image;
  } else {
    config.background.backgroundType = BackgroundType::Color;
  }

  const QString value = ui->backgroundValueEdit->text().trimmed();
  // Only the field matching the active type holds a value; the other two
  // are cleared so an old value can't bleed back when the user toggles.
  if (config.background.backgroundType == BackgroundType::Video) {
    config.background.backgroundVideo = value;
    config.background.backgroundColor.clear();
    config.background.backgroundImage.clear();
  } else if (config.background.backgroundType == BackgroundType::Image) {
    config.background.backgroundImage = value;
    config.background.backgroundColor.clear();
    config.background.backgroundVideo.clear();
  } else {
    config.background.backgroundColor = value;
    config.background.backgroundImage.clear();
    config.background.backgroundVideo.clear();
  }

  config.background.primaryColor = ui->primaryColorEdit->text().trimmed();
  config.background.tileColor = ui->tileColorEdit->text().trimmed();
  config.background.selectionColor = ui->selectionColorEdit->text().trimmed();
  config.listView.listRowColor = ui->listRowColorEdit->text().trimmed();
  config.listView.listAltRowColor = ui->listAltRowColorEdit->text().trimmed();
  config.background.vignetteEnabled = ui->vignetteEnabledCheckBox->isChecked();
  config.background.vignetteIntensity = ui->vignetteIntensitySpinBox->value();
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
  if (currentType != o.background.backgroundType) return true;

  const QString currentValue = ui->backgroundValueEdit->text().trimmed();
  if (currentType == BackgroundType::Video) {
    if (currentValue != o.background.backgroundVideo) return true;
  } else if (currentType == BackgroundType::Image) {
    if (currentValue != o.background.backgroundImage) return true;
  } else {
    if (currentValue != o.background.backgroundColor) return true;
  }

  if (ui->primaryColorEdit->text().trimmed() != o.background.primaryColor) return true;
  if (ui->tileColorEdit->text().trimmed() != o.background.tileColor) return true;
  if (ui->selectionColorEdit->text().trimmed() != o.background.selectionColor) return true;
  if (ui->listRowColorEdit->text().trimmed() != o.listView.listRowColor) return true;
  if (ui->listAltRowColorEdit->text().trimmed() != o.listView.listAltRowColor) return true;
  if (ui->vignetteEnabledCheckBox->isChecked() != o.background.vignetteEnabled) return true;
  if (ui->vignetteIntensitySpinBox->value() != o.background.vignetteIntensity) return true;
  return false;
}

void AppearanceColorsPanel::refresh() {
  if (!m_model || !m_model->generalSettings) return;
  SettingsFormBinding::loadInto(ui->titleSaturationSpinBox,
                                m_model->generalSettings->appearance.titleTintSaturation);
  SettingsFormBinding::loadInto(ui->titleLightnessSpinBox,
                                m_model->generalSettings->appearance.titleTintLightness);
  SettingsFormBinding::loadInto(ui->baseColorEdit,
                                m_model->generalSettings->appearance.titleBaseColor);
}

void AppearanceColorsPanel::writeBackGlobals() {
  if (!m_model || !m_model->generalSettings) return;
  m_model->generalSettings->appearance.titleTintSaturation = ui->titleSaturationSpinBox->value();
  m_model->generalSettings->appearance.titleTintLightness = ui->titleLightnessSpinBox->value();
  m_model->generalSettings->appearance.titleBaseColor = ui->baseColorEdit->text().trimmed();
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

void AppearanceColorsPanel::onLoadColorScheme() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }

  const QList<KdeColorScheme::SchemeInfo> schemes = KdeColorScheme::discover();
  if (schemes.isEmpty()) {
    QMessageBox::information(this, tr("No color schemes available"),
                             tr("No bundled or system color schemes were discovered. On KDE Plasma "
                                "systems, schemes normally live under /usr/share/color-schemes/."));
    return;
  }

  // Build the picker list with a "(bundled)" suffix on Kartend's own
  // schemes so users can tell at a glance which entries ship with the
  // app vs which come from the system. Indexes line up with the
  // discover() result so the chosen index resolves back to a SchemeInfo.
  QStringList labels;
  labels.reserve(schemes.size());
  for (const auto &s : schemes) {
    labels.append(s.isBundled ? s.displayName + tr(" (bundled)") : s.displayName);
  }

  bool ok = false;
  const QString chosen = QInputDialog::getItem(
      this, tr("Load color scheme"), tr("Pick a color scheme to apply to this collection:"), labels,
      0, /*editable=*/false, &ok);
  if (!ok || chosen.isEmpty()) {
    return;
  }
  const int idx = labels.indexOf(chosen);
  if (idx < 0 || idx >= schemes.size()) {
    return;
  }

  auto loaded = KdeColorScheme::load(schemes[idx].filePath);
  if (loaded.isError()) {
    QMessageBox::warning(this, tr("Could not load color scheme"), loaded.error().message);
    return;
  }
  const auto &scheme = loaded.value();

  // Apply to the working CollectionConfig + GeneralSettings, then refresh
  // the UI line edits so the user sees the new values without an
  // intermediate Save+Reload. Per-field changed() signals are emitted
  // automatically by setText() so the dirty-tracking machinery picks
  // the swap up.
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  KdeColorScheme::applyToCollection(scheme, config);
  if (m_model->generalSettings) {
    KdeColorScheme::applyToGeneralSettings(scheme, *m_model->generalSettings);
  }

  // Push the new color values into the form. load() reads from the
  // working CollectionConfig + generalSettings pointer that the apply
  // helpers just mutated, so this mirrors the post-Apply visual state.
  load();

  // Title base color uses live-save semantics (host applies
  // ItemWidget::setTitleBaseColor on baseColorChanged); fire the signal
  // so a scheme with a Selection/BackgroundNormal entry takes effect on
  // the grid immediately.
  if (m_model->generalSettings && !m_model->generalSettings->appearance.titleBaseColor.isEmpty()) {
    emit baseColorChanged(m_model->generalSettings->appearance.titleBaseColor);
  }
  emit changed();
}
