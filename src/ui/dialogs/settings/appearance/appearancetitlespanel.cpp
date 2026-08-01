#include "appearancetitlespanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_appearancetitlespanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QFontDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

AppearanceTitlesPanel::AppearanceTitlesPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::AppearanceTitlesPanel) {
  ui->setupUi(this);
  connect(ui->browseFontButton, &QPushButton::clicked, this, &AppearanceTitlesPanel::onBrowseFont);
  connect(ui->fontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int) { emit changed(); });
  connect(ui->customFontEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->hideTitlesCheckBox, &QCheckBox::toggled, this, [this](bool) { emit changed(); });
  connect(ui->hideSubcollectionTitlesCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
}

AppearanceTitlesPanel::~AppearanceTitlesPanel() {
  delete ui;
}

void AppearanceTitlesPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void AppearanceTitlesPanel::load() {
  const CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  const CollectionConfig &config = *current;
  SettingsFormBinding::loadInto(ui->fontSizeSpinBox, config.gridLayout.fontSize);
  SettingsFormBinding::loadInto(ui->customFontEdit, config.customFontFamily);
  SettingsFormBinding::loadInto(ui->hideTitlesCheckBox, config.hideTitles);
  SettingsFormBinding::loadInto(ui->hideSubcollectionTitlesCheckBox,
                                config.hideSubcollectionTitles);
}

void AppearanceTitlesPanel::clear() {
  ui->fontSizeSpinBox->setValue(12);
  ui->customFontEdit->clear();
  ui->hideTitlesCheckBox->setChecked(false);
  ui->hideSubcollectionTitlesCheckBox->setChecked(false);
}

void AppearanceTitlesPanel::save() {
  CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  CollectionConfig &config = *current;
  config.gridLayout.fontSize = ui->fontSizeSpinBox->value();
  config.customFontFamily = ui->customFontEdit->text().trimmed();
  config.hideTitles = ui->hideTitlesCheckBox->isChecked();
  config.hideSubcollectionTitles = ui->hideSubcollectionTitlesCheckBox->isChecked();
}

void AppearanceTitlesPanel::onBrowseFont() {
  bool ok = false;
  QFont currentFont = QApplication::font();
  if (ui->fontSizeSpinBox->value() > 0) {
    currentFont.setPointSize(ui->fontSizeSpinBox->value());
  }
  const QString currentFamily = ui->customFontEdit->text().trimmed();
  if (!currentFamily.isEmpty()) {
    currentFont.setFamily(currentFamily);
  }
  const QFont chosen = QFontDialog::getFont(&ok, currentFont, this, tr("Select Title Font"));
  if (ok) {
    ui->customFontEdit->setText(chosen.family());
  }
}
