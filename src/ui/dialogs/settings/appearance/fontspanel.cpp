#include "fontspanel.h"

#include "collectiontypes.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_fontspanel.h"

#include <QApplication>
#include <QFontDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

FontsPanel::FontsPanel(QWidget *parent) : QWidget(parent), ui(new Ui::FontsPanel) {
  ui->setupUi(this);

  connect(ui->browseGlobalUiFontButton, &QPushButton::clicked, this, &FontsPanel::onPick);
  connect(ui->globalUiFontFamilyEdit, &QLineEdit::editingFinished, this, &FontsPanel::save);
  connect(ui->globalUiFontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int) { save(); });
}

FontsPanel::~FontsPanel() {
  delete ui;
}

void FontsPanel::setModel(SettingsModel *model) {
  m_model = model;
  load();
}

void FontsPanel::load() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  // SettingsFormBinding::loadInto already wraps each setter in a QSignalBlocker,
  // so the editingFinished / valueChanged → writeBack pipeline stays quiet
  // during programmatic hydration.
  SettingsFormBinding::loadInto(ui->globalUiFontFamilyEdit,
                                m_model->generalSettings->appearance.globalUiFontFamily);
  SettingsFormBinding::loadInto(ui->globalUiFontSizeSpinBox,
                                m_model->generalSettings->appearance.globalUiFontPointSize);
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
  save();
}

void FontsPanel::save() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  m_model->generalSettings->appearance.globalUiFontFamily =
      ui->globalUiFontFamilyEdit->text().trimmed();
  m_model->generalSettings->appearance.globalUiFontPointSize = ui->globalUiFontSizeSpinBox->value();
  emit changed();
}
