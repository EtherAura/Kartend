#include "splashpanel.h"

#include "collectionutils.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_splashpanel.h"

#include <QCheckBox>
#include <QLineEdit>

SplashPanel::SplashPanel(QWidget *parent) : QWidget(parent), ui(new Ui::SplashPanel) {
  ui->setupUi(this);

  // Checkbox toggles fire on every state change; line edits persist on
  // editing-finished (focus-out / Enter) so partial typing doesn't hammer
  // the config file mid-keystroke. Same cadence the standalone live-save
  // wiring used before this panel existed.
  connect(ui->bootSplashCheckBox, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  connect(ui->resumeFocusSplashCheckBox, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  connect(ui->bootSplashTitleLineEdit, &QLineEdit::editingFinished, this, &SplashPanel::writeBack);
  connect(ui->bootSplashSubtitleLineEdit, &QLineEdit::editingFinished, this,
          &SplashPanel::writeBack);
  connect(ui->resumeFocusSplashTitleLineEdit, &QLineEdit::editingFinished, this,
          &SplashPanel::writeBack);
  connect(ui->resumeFocusSplashSubtitleLineEdit, &QLineEdit::editingFinished, this,
          &SplashPanel::writeBack);
}

SplashPanel::~SplashPanel() {
  delete ui;
}

void SplashPanel::setModel(SettingsModel *model) {
  m_model = model;
  refresh();
}

void SplashPanel::refresh() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  SettingsFormBinding::loadInto(ui->bootSplashCheckBox,
                                m_model->generalSettings->bootSplashEnabled);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashCheckBox,
                                m_model->generalSettings->resumeFocusSplashEnabled);
  SettingsFormBinding::loadInto(ui->bootSplashTitleLineEdit,
                                m_model->generalSettings->bootSplashTitle);
  SettingsFormBinding::loadInto(ui->bootSplashSubtitleLineEdit,
                                m_model->generalSettings->bootSplashSubtitle);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashTitleLineEdit,
                                m_model->generalSettings->resumeFocusSplashTitle);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashSubtitleLineEdit,
                                m_model->generalSettings->resumeFocusSplashSubtitle);
}

void SplashPanel::writeBack() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  m_model->generalSettings->bootSplashEnabled = ui->bootSplashCheckBox->isChecked();
  m_model->generalSettings->resumeFocusSplashEnabled = ui->resumeFocusSplashCheckBox->isChecked();
  m_model->generalSettings->bootSplashTitle = ui->bootSplashTitleLineEdit->text().trimmed();
  m_model->generalSettings->bootSplashSubtitle = ui->bootSplashSubtitleLineEdit->text().trimmed();
  m_model->generalSettings->resumeFocusSplashTitle =
      ui->resumeFocusSplashTitleLineEdit->text().trimmed();
  m_model->generalSettings->resumeFocusSplashSubtitle =
      ui->resumeFocusSplashSubtitleLineEdit->text().trimmed();
  emit changed();
}
