#include "splashpanel.h"

#include "collectionutils.h"
#include "settingsformbinding.h"
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

void SplashPanel::setSettings(GeneralSettings *settings) {
  m_settings = settings;
  refresh();
}

void SplashPanel::refresh() {
  if (!m_settings) {
    return;
  }
  SettingsFormBinding::loadInto(ui->bootSplashCheckBox, m_settings->bootSplashEnabled);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashCheckBox,
                                m_settings->resumeFocusSplashEnabled);
  SettingsFormBinding::loadInto(ui->bootSplashTitleLineEdit, m_settings->bootSplashTitle);
  SettingsFormBinding::loadInto(ui->bootSplashSubtitleLineEdit, m_settings->bootSplashSubtitle);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashTitleLineEdit,
                                m_settings->resumeFocusSplashTitle);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashSubtitleLineEdit,
                                m_settings->resumeFocusSplashSubtitle);
}

void SplashPanel::writeBack() {
  if (!m_settings) {
    return;
  }
  m_settings->bootSplashEnabled = ui->bootSplashCheckBox->isChecked();
  m_settings->resumeFocusSplashEnabled = ui->resumeFocusSplashCheckBox->isChecked();
  m_settings->bootSplashTitle = ui->bootSplashTitleLineEdit->text().trimmed();
  m_settings->bootSplashSubtitle = ui->bootSplashSubtitleLineEdit->text().trimmed();
  m_settings->resumeFocusSplashTitle = ui->resumeFocusSplashTitleLineEdit->text().trimmed();
  m_settings->resumeFocusSplashSubtitle = ui->resumeFocusSplashSubtitleLineEdit->text().trimmed();
  emit changed();
}
