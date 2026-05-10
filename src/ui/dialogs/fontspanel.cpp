#include "fontspanel.h"

#include "collectionutils.h"
#include "settingsformbinding.h"
#include "ui_fontspanel.h"

#include <QApplication>
#include <QFontDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

FontsPanel::FontsPanel(QWidget *parent) : QWidget(parent), ui(new Ui::FontsPanel) {
  ui->setupUi(this);

  connect(ui->browseGlobalUiFontButton, &QPushButton::clicked, this, &FontsPanel::onPick);
  connect(ui->globalUiFontFamilyEdit, &QLineEdit::editingFinished, this, &FontsPanel::writeBack);
  connect(ui->globalUiFontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int) { writeBack(); });
}

FontsPanel::~FontsPanel() {
  delete ui;
}

void FontsPanel::setSettings(GeneralSettings *settings) {
  m_settings = settings;
  refresh();
}

void FontsPanel::refresh() {
  if (!m_settings) {
    return;
  }
  // SettingsFormBinding::loadInto already wraps each setter in a QSignalBlocker,
  // so the editingFinished / valueChanged → writeBack pipeline stays quiet
  // during programmatic hydration.
  SettingsFormBinding::loadInto(ui->globalUiFontFamilyEdit, m_settings->globalUiFontFamily);
  SettingsFormBinding::loadInto(ui->globalUiFontSizeSpinBox, m_settings->globalUiFontPointSize);
}

void FontsPanel::onPick() {
  bool ok = false;
  QFont currentFont = QApplication::font();
  const QString currentFamily = ui->globalUiFontFamilyEdit->text().trimmed();
  if (!currentFamily.isEmpty()) {
    currentFont.setFamily(currentFamily);
  }
  if (ui->globalUiFontSizeSpinBox->value() > 0) {
    currentFont.setPointSize(ui->globalUiFontSizeSpinBox->value());
  }
  const QFont chosen = QFontDialog::getFont(&ok, currentFont, this, tr("Select Application Font"));
  if (!ok) {
    return;
  }
  ui->globalUiFontFamilyEdit->setText(chosen.family());
  if (chosen.pointSize() > 0) {
    ui->globalUiFontSizeSpinBox->setValue(chosen.pointSize());
  }
  // Both setters above re-emit the field-change signals which would each
  // trigger writeBack; do it explicitly here to guarantee a single coherent
  // changed() emission with the new family+size pair atomically applied.
  writeBack();
}

void FontsPanel::writeBack() {
  if (!m_settings) {
    return;
  }
  m_settings->globalUiFontFamily = ui->globalUiFontFamilyEdit->text().trimmed();
  m_settings->globalUiFontPointSize = ui->globalUiFontSizeSpinBox->value();
  emit changed();
}
