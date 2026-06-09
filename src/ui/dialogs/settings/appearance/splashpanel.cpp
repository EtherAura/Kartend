#include "splashpanel.h"

#include "collectiontypes.h"
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
  connect(ui->bootSplashCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->resumeFocusSplashCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->bootSplashTitleLineEdit, &QLineEdit::editingFinished, this, &SplashPanel::save);
  connect(ui->bootSplashSubtitleLineEdit, &QLineEdit::editingFinished, this,
          &SplashPanel::save);
  connect(ui->resumeFocusSplashTitleLineEdit, &QLineEdit::editingFinished, this,
          &SplashPanel::save);
  connect(ui->resumeFocusSplashSubtitleLineEdit, &QLineEdit::editingFinished, this,
          &SplashPanel::save);
}

SplashPanel::~SplashPanel() {
  delete ui;
}

void SplashPanel::setModel(SettingsModel *model) {
  m_model = model;
  load();
}

void SplashPanel::load() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  SettingsFormBinding::loadInto(ui->bootSplashCheckBox,
                                m_model->generalSettings->splash.bootSplashEnabled);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashCheckBox,
                                m_model->generalSettings->splash.resumeFocusSplashEnabled);
  SettingsFormBinding::loadInto(ui->bootSplashTitleLineEdit,
                                m_model->generalSettings->splash.bootSplashTitle);
  SettingsFormBinding::loadInto(ui->bootSplashSubtitleLineEdit,
                                m_model->generalSettings->splash.bootSplashSubtitle);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashTitleLineEdit,
                                m_model->generalSettings->splash.resumeFocusSplashTitle);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashSubtitleLineEdit,
                                m_model->generalSettings->splash.resumeFocusSplashSubtitle);
}

void SplashPanel::save() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  m_model->generalSettings->splash.bootSplashEnabled = ui->bootSplashCheckBox->isChecked();
  m_model->generalSettings->splash.resumeFocusSplashEnabled =
      ui->resumeFocusSplashCheckBox->isChecked();
  m_model->generalSettings->splash.bootSplashTitle = ui->bootSplashTitleLineEdit->text().trimmed();
  m_model->generalSettings->splash.bootSplashSubtitle =
      ui->bootSplashSubtitleLineEdit->text().trimmed();
  m_model->generalSettings->splash.resumeFocusSplashTitle =
      ui->resumeFocusSplashTitleLineEdit->text().trimmed();
  m_model->generalSettings->splash.resumeFocusSplashSubtitle =
      ui->resumeFocusSplashSubtitleLineEdit->text().trimmed();
  emit changed();
}
