#include "sidebarpanel.h"

#include "collectionutils.h"
#include "settingsmodel.h"
#include "ui_sidebarpanel.h"
#include "uiconstants.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

SidebarPanel::SidebarPanel(QWidget *parent) : QWidget(parent), ui(new Ui::SidebarPanel) {
  ui->setupUi(this);

  // Position-driven width/height visibility — Right/Left exposes Width,
  // Top/Bottom exposes Height. Lock checkbox stays visible regardless.
  connect(ui->sidebarPositionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { onPositionChanged(); });

  connect(ui->sidebarBackgroundTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SidebarPanel::onBackgroundTypeChanged);
  connect(ui->sidebarBackgroundPickButton, &QPushButton::clicked, this,
          &SidebarPanel::onBackgroundPick);

  wireColorPicker(ui->sidebarTextColorPickButton, ui->sidebarTextColorEdit,
                  tr("Select Details Pane Text Color"));
  wireColorPicker(ui->sidebarAccentColorPickButton, ui->sidebarAccentColorEdit,
                  tr("Select Details Pane Accent Color"));
  // Bubble pickers are RGB-only — opacity has its own spinbox.
  wireColorPicker(ui->sidebarHeaderBgPickButton, ui->sidebarHeaderBgEdit,
                  tr("Select Details Pane Header Bubble Color"));
  wireColorPicker(ui->sidebarSectionBgPickButton, ui->sidebarSectionBgEdit,
                  tr("Select Details Pane Section Bubble Color"));
  // Pattern overlay tint is alpha-aware — single combined "tint + dim" knob.
  wireAlphaColorPicker(ui->sidebarPatternColorPickButton, ui->sidebarPatternColorEdit,
                       tr("Select Details Pane Pattern Overlay Tint"));

  connect(ui->sidebarFontPickButton, &QPushButton::clicked, this, &SidebarPanel::onFontPick);

  connectChangeSignals();
  onPositionChanged();
}

SidebarPanel::~SidebarPanel() {
  delete ui;
}

void SidebarPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void SidebarPanel::load() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  ui->sidebarModeComboBox->setCurrentIndex(static_cast<int>(config.sidebar.sidebarMode));
  ui->sidebarPositionComboBox->setCurrentIndex(static_cast<int>(config.sidebar.sidebarPosition));
  ui->sidebarWidthSpinBox->setValue(config.sidebar.sidebarWidth);
  ui->sidebarHeightSpinBox->setValue(config.sidebar.sidebarHeight);
  ui->sidebarWidthLockedCheckBox->setChecked(config.sidebar.sidebarWidthLocked);
  ui->sidebarBackgroundTypeComboBox->setCurrentIndex(
      static_cast<int>(config.sidebar.sidebarBackgroundType));
  if (config.sidebar.sidebarBackgroundType == DetailsPaneBackgroundType::Image) {
    ui->sidebarBackgroundValueEdit->setText(config.sidebar.sidebarBackgroundImage);
  } else {
    ui->sidebarBackgroundValueEdit->setText(config.sidebar.sidebarBackgroundColor);
  }
  ui->sidebarPatternIntensitySpinBox->setValue(config.sidebar.sidebarPatternIntensity);
  ui->sidebarPatternColorEdit->setText(config.sidebar.sidebarPatternColor);
  ui->sidebarTextColorEdit->setText(config.sidebar.sidebarTextColor);
  ui->sidebarAccentColorEdit->setText(config.sidebar.sidebarAccentColor);
  ui->sidebarHeaderBgEdit->setText(config.sidebar.sidebarHeaderBgColor);
  ui->sidebarSectionBgEdit->setText(config.sidebar.sidebarSectionBgColor);
  ui->sidebarHeaderBgOpacitySpinBox->setValue(config.sidebar.sidebarHeaderBgOpacity);
  ui->sidebarSectionBgOpacitySpinBox->setValue(config.sidebar.sidebarSectionBgOpacity);
  ui->sidebarFontFamilyEdit->setText(config.sidebar.sidebarFontFamily);
  ui->sidebarFontSizeSpinBox->setValue(config.sidebar.sidebarFontPointSize);
  ui->sidebarActiveTabComboBox->setCurrentIndex(static_cast<int>(config.sidebar.sidebarActiveTab));
  ui->hideHorizontalScrollbarCheckBox->setChecked(config.gridLayout.hideHorizontalScrollbar);
  ui->hideVerticalScrollbarCheckBox->setChecked(config.gridLayout.hideVerticalScrollbar);
  ui->sidebarActiveCollectionLabel->setText(tr("Editing: %1").arg(config.name));
}

void SidebarPanel::clear() {
  ui->sidebarModeComboBox->setCurrentIndex(0);
  ui->sidebarPositionComboBox->setCurrentIndex(0);
  ui->sidebarWidthSpinBox->setValue(UIConstants::DetailsPane::FIXED_WIDTH);
  ui->sidebarHeightSpinBox->setValue(UIConstants::DetailsPane::FIXED_HEIGHT);
  ui->sidebarWidthLockedCheckBox->setChecked(true);
  ui->sidebarBackgroundTypeComboBox->setCurrentIndex(0);
  ui->sidebarBackgroundValueEdit->clear();
  ui->sidebarPatternIntensitySpinBox->setValue(50);
  ui->sidebarPatternColorEdit->clear();
  ui->sidebarTextColorEdit->clear();
  ui->sidebarAccentColorEdit->clear();
  ui->sidebarHeaderBgEdit->clear();
  ui->sidebarSectionBgEdit->clear();
  ui->sidebarHeaderBgOpacitySpinBox->setValue(200);
  ui->sidebarSectionBgOpacitySpinBox->setValue(170);
  ui->sidebarFontFamilyEdit->clear();
  ui->sidebarFontSizeSpinBox->setValue(0);
  ui->sidebarActiveTabComboBox->setCurrentIndex(0);
  ui->hideHorizontalScrollbarCheckBox->setChecked(false);
  ui->hideVerticalScrollbarCheckBox->setChecked(false);
  ui->sidebarActiveCollectionLabel->setText(tr("Editing: (no collection selected)"));
}

void SidebarPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  config.sidebar.sidebarMode =
      static_cast<DetailsPaneMode>(ui->sidebarModeComboBox->currentIndex());
  config.sidebar.sidebarPosition =
      static_cast<DetailsPanePosition>(ui->sidebarPositionComboBox->currentIndex());
  config.sidebar.sidebarWidth = ui->sidebarWidthSpinBox->value();
  config.sidebar.sidebarHeight = ui->sidebarHeightSpinBox->value();
  config.sidebar.sidebarWidthLocked = ui->sidebarWidthLockedCheckBox->isChecked();
  config.sidebar.sidebarBackgroundType =
      static_cast<DetailsPaneBackgroundType>(ui->sidebarBackgroundTypeComboBox->currentIndex());
  const QString bgValue = ui->sidebarBackgroundValueEdit->text().trimmed();
  if (config.sidebar.sidebarBackgroundType == DetailsPaneBackgroundType::Image) {
    config.sidebar.sidebarBackgroundImage = bgValue;
    config.sidebar.sidebarBackgroundColor.clear();
  } else {
    // Pattern mode shares the bg-color slot with Color mode; the pattern
    // stroke color comes from sidebarPatternColorEdit.
    config.sidebar.sidebarBackgroundColor = bgValue;
    config.sidebar.sidebarBackgroundImage.clear();
  }
  config.sidebar.sidebarPatternIntensity = ui->sidebarPatternIntensitySpinBox->value();
  config.sidebar.sidebarPatternColor = ui->sidebarPatternColorEdit->text().trimmed();
  config.sidebar.sidebarTextColor = ui->sidebarTextColorEdit->text().trimmed();
  config.sidebar.sidebarAccentColor = ui->sidebarAccentColorEdit->text().trimmed();
  config.sidebar.sidebarHeaderBgColor = ui->sidebarHeaderBgEdit->text().trimmed();
  config.sidebar.sidebarSectionBgColor = ui->sidebarSectionBgEdit->text().trimmed();
  config.sidebar.sidebarHeaderBgOpacity = ui->sidebarHeaderBgOpacitySpinBox->value();
  config.sidebar.sidebarSectionBgOpacity = ui->sidebarSectionBgOpacitySpinBox->value();
  config.sidebar.sidebarFontFamily = ui->sidebarFontFamilyEdit->text().trimmed();
  config.sidebar.sidebarFontPointSize = ui->sidebarFontSizeSpinBox->value();
  config.sidebar.sidebarActiveTab =
      static_cast<DetailsPaneTab>(ui->sidebarActiveTabComboBox->currentIndex());
  config.gridLayout.hideHorizontalScrollbar = ui->hideHorizontalScrollbarCheckBox->isChecked();
  config.gridLayout.hideVerticalScrollbar = ui->hideVerticalScrollbarCheckBox->isChecked();
}

bool SidebarPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  // Background value compares against image OR color depending on the type
  // — same dual-purpose semantics as save().
  const QString bgOrig = o.sidebar.sidebarBackgroundType == DetailsPaneBackgroundType::Image
                             ? o.sidebar.sidebarBackgroundImage
                             : o.sidebar.sidebarBackgroundColor;
  if (ui->sidebarBackgroundValueEdit->text().trimmed() != bgOrig) return true;

  if (ui->sidebarModeComboBox->currentIndex() != static_cast<int>(o.sidebar.sidebarMode))
    return true;
  if (ui->sidebarPositionComboBox->currentIndex() != static_cast<int>(o.sidebar.sidebarPosition))
    return true;
  if (ui->sidebarWidthSpinBox->value() != o.sidebar.sidebarWidth) return true;
  if (ui->sidebarHeightSpinBox->value() != o.sidebar.sidebarHeight) return true;
  if (ui->sidebarWidthLockedCheckBox->isChecked() != o.sidebar.sidebarWidthLocked) return true;
  if (ui->sidebarBackgroundTypeComboBox->currentIndex() !=
      static_cast<int>(o.sidebar.sidebarBackgroundType))
    return true;
  if (ui->sidebarPatternIntensitySpinBox->value() != o.sidebar.sidebarPatternIntensity) return true;
  if (ui->sidebarPatternColorEdit->text().trimmed() != o.sidebar.sidebarPatternColor) return true;
  if (ui->sidebarTextColorEdit->text().trimmed() != o.sidebar.sidebarTextColor) return true;
  if (ui->sidebarAccentColorEdit->text().trimmed() != o.sidebar.sidebarAccentColor) return true;
  if (ui->sidebarHeaderBgEdit->text().trimmed() != o.sidebar.sidebarHeaderBgColor) return true;
  if (ui->sidebarSectionBgEdit->text().trimmed() != o.sidebar.sidebarSectionBgColor) return true;
  if (ui->sidebarHeaderBgOpacitySpinBox->value() != o.sidebar.sidebarHeaderBgOpacity) return true;
  if (ui->sidebarSectionBgOpacitySpinBox->value() != o.sidebar.sidebarSectionBgOpacity) return true;
  if (ui->sidebarFontFamilyEdit->text().trimmed() != o.sidebar.sidebarFontFamily) return true;
  if (ui->sidebarFontSizeSpinBox->value() != o.sidebar.sidebarFontPointSize) return true;
  if (ui->sidebarActiveTabComboBox->currentIndex() != static_cast<int>(o.sidebar.sidebarActiveTab))
    return true;
  if (ui->hideHorizontalScrollbarCheckBox->isChecked() != o.gridLayout.hideHorizontalScrollbar)
    return true;
  if (ui->hideVerticalScrollbarCheckBox->isChecked() != o.gridLayout.hideVerticalScrollbar)
    return true;
  return false;
}

void SidebarPanel::onPositionChanged() {
  const auto pos = static_cast<DetailsPanePosition>(ui->sidebarPositionComboBox->currentIndex());
  const bool horizontalDock = CollectionUtils::isDetailsPaneHorizontal(pos);
  ui->label_sidebarWidth->setVisible(!horizontalDock);
  ui->sidebarWidthSpinBox->setVisible(!horizontalDock);
  ui->label_sidebarHeight->setVisible(horizontalDock);
  ui->sidebarHeightSpinBox->setVisible(horizontalDock);
}

void SidebarPanel::onBackgroundTypeChanged(int index) {
  if (index == static_cast<int>(DetailsPaneBackgroundType::Image)) {
    ui->sidebarBackgroundPickButton->setText(tr("Browse..."));
    ui->sidebarBackgroundValueEdit->setPlaceholderText(tr("None"));
  } else if (index == static_cast<int>(DetailsPaneBackgroundType::Pattern)) {
    ui->sidebarBackgroundPickButton->setText(tr("Pick..."));
    ui->sidebarBackgroundValueEdit->setPlaceholderText(tr("(unused for pattern)"));
  } else {
    ui->sidebarBackgroundPickButton->setText(tr("Pick..."));
    ui->sidebarBackgroundValueEdit->setPlaceholderText(tr("Default"));
  }
}

void SidebarPanel::onBackgroundPick() {
  const int mode = ui->sidebarBackgroundTypeComboBox->currentIndex();
  if (mode == static_cast<int>(DetailsPaneBackgroundType::Image)) {
    const QString currentPath = ui->sidebarBackgroundValueEdit->text().trimmed();
    const QString startDir =
        currentPath.isEmpty() ? QDir::homePath() : QFileInfo(currentPath).absolutePath();
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Select Details Pane Background Image"), startDir,
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
    if (!filePath.isEmpty()) {
      ui->sidebarBackgroundValueEdit->setText(filePath);
    }
  } else {
    QColor currentColor = Qt::white;
    const QString currentValue = ui->sidebarBackgroundValueEdit->text().trimmed();
    if (!currentValue.isEmpty()) {
      currentColor = QColor(currentValue);
    }
    const QColor color =
        QColorDialog::getColor(currentColor, this, tr("Select Details Pane Background Color"));
    if (color.isValid()) {
      ui->sidebarBackgroundValueEdit->setText(color.name());
    }
  }
}

void SidebarPanel::onFontPick() {
  bool ok = false;
  QFont currentFont = QApplication::font();
  const QString currentFamily = ui->sidebarFontFamilyEdit->text().trimmed();
  if (!currentFamily.isEmpty()) {
    currentFont.setFamily(currentFamily);
  }
  if (ui->sidebarFontSizeSpinBox->value() > 0) {
    currentFont.setPointSize(ui->sidebarFontSizeSpinBox->value());
  }
  const QFont chosen = QFontDialog::getFont(&ok, currentFont, this, tr("Select Details Pane Font"));
  if (ok) {
    ui->sidebarFontFamilyEdit->setText(chosen.family());
    if (chosen.pointSize() > 0) {
      ui->sidebarFontSizeSpinBox->setValue(chosen.pointSize());
    }
  }
}

void SidebarPanel::wireColorPicker(QPushButton *button, QLineEdit *edit, const QString &title) {
  connect(button, &QPushButton::clicked, this, [this, edit, title]() {
    QColor current = Qt::gray;
    const QString existing = edit->text().trimmed();
    if (!existing.isEmpty()) {
      current = QColor(existing);
    }
    const QColor picked = QColorDialog::getColor(current, this, title);
    if (picked.isValid()) {
      edit->setText(picked.name());
    }
  });
}

void SidebarPanel::wireAlphaColorPicker(QPushButton *button, QLineEdit *edit,
                                        const QString &title) {
  connect(button, &QPushButton::clicked, this, [this, edit, title]() {
    QColor current = QColor(0, 0, 0, 96);
    const QString existing = edit->text().trimmed();
    if (!existing.isEmpty()) {
      const QColor parsed(existing);
      if (parsed.isValid()) current = parsed;
    }
    const QColor picked =
        QColorDialog::getColor(current, this, title, QColorDialog::ShowAlphaChannel);
    if (picked.isValid()) {
      // name(QColor::HexArgb) preserves alpha so the round-trip matches what
      // the user picked instead of dropping to opaque.
      edit->setText(picked.name(QColor::HexArgb));
    }
  });
}

void SidebarPanel::connectChangeSignals() {
  const auto onCombo = QOverload<int>::of(&QComboBox::currentIndexChanged);
  const auto onSpin = QOverload<int>::of(&QSpinBox::valueChanged);

  connect(ui->sidebarModeComboBox, onCombo, this, &SidebarPanel::changed);
  connect(ui->sidebarPositionComboBox, onCombo, this, &SidebarPanel::changed);
  connect(ui->sidebarWidthSpinBox, onSpin, this, &SidebarPanel::changed);
  connect(ui->sidebarHeightSpinBox, onSpin, this, &SidebarPanel::changed);
  connect(ui->sidebarWidthLockedCheckBox, &QCheckBox::toggled, this, &SidebarPanel::changed);
  connect(ui->sidebarBackgroundTypeComboBox, onCombo, this, &SidebarPanel::changed);
  connect(ui->sidebarBackgroundValueEdit, &QLineEdit::textChanged, this, &SidebarPanel::changed);
  connect(ui->sidebarPatternIntensitySpinBox, onSpin, this, &SidebarPanel::changed);
  connect(ui->sidebarPatternColorEdit, &QLineEdit::textChanged, this, &SidebarPanel::changed);
  connect(ui->sidebarTextColorEdit, &QLineEdit::textChanged, this, &SidebarPanel::changed);
  connect(ui->sidebarAccentColorEdit, &QLineEdit::textChanged, this, &SidebarPanel::changed);
  connect(ui->sidebarHeaderBgEdit, &QLineEdit::textChanged, this, &SidebarPanel::changed);
  connect(ui->sidebarSectionBgEdit, &QLineEdit::textChanged, this, &SidebarPanel::changed);
  connect(ui->sidebarHeaderBgOpacitySpinBox, onSpin, this, &SidebarPanel::changed);
  connect(ui->sidebarSectionBgOpacitySpinBox, onSpin, this, &SidebarPanel::changed);
  connect(ui->sidebarFontFamilyEdit, &QLineEdit::textChanged, this, &SidebarPanel::changed);
  connect(ui->sidebarFontSizeSpinBox, onSpin, this, &SidebarPanel::changed);
  connect(ui->sidebarActiveTabComboBox, onCombo, this, &SidebarPanel::changed);
  connect(ui->hideHorizontalScrollbarCheckBox, &QCheckBox::toggled, this, &SidebarPanel::changed);
  connect(ui->hideVerticalScrollbarCheckBox, &QCheckBox::toggled, this, &SidebarPanel::changed);
}
