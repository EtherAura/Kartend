#include "launchertabpanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_launchertabpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

LauncherTabPanel::LauncherTabPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::LauncherTabPanel) {
  ui->setupUi(this);

  // Per-field changed() emission for simple data fields. Add/edit/remove
  // additional-launcher button connections + default-combo + browse buttons
  // remain wired by the host dialog.
  connect(ui->launcherLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->coreLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->launchParamsLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->launcherNameLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->extractArchivesCheckBox, &QCheckBox::toggled, this, [this](bool) {
    updateExtractedExtensionVisibility();
    emit changed();
  });
  connect(ui->extractedExtensionLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });

  updateExtractedExtensionVisibility();
}

LauncherTabPanel::~LauncherTabPanel() {
  delete ui;
}

void LauncherTabPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void LauncherTabPanel::load() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 ||
      *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  SettingsFormBinding::loadInto(ui->launcherLineEdit, config.launcherPath);
  SettingsFormBinding::loadInto(ui->coreLineEdit, config.corePath);
  SettingsFormBinding::loadInto(ui->launchParamsLineEdit, config.launchParameters);
  SettingsFormBinding::loadInto(ui->launcherNameLineEdit, config.launcherName);
  SettingsFormBinding::loadInto(ui->extractArchivesCheckBox, config.extractArchives);
  SettingsFormBinding::loadInto(ui->extractedExtensionLineEdit, config.extractedExtension);
  updateExtractedExtensionVisibility();
}

void LauncherTabPanel::clear() {
  ui->launcherLineEdit->clear();
  ui->coreLineEdit->clear();
  ui->launchParamsLineEdit->clear();
  ui->launcherNameLineEdit->clear();
  ui->extractArchivesCheckBox->setChecked(false);
  ui->extractedExtensionLineEdit->clear();
  updateExtractedExtensionVisibility();
}

void LauncherTabPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 ||
      *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  // launcherPath / corePath / launchParameters preserve exact text (no
  // trim) — paths might legitimately contain trailing spaces in obscure
  // setups.
  config.launcherPath = ui->launcherLineEdit->text();
  config.corePath = ui->coreLineEdit->text();
  config.launchParameters = ui->launchParamsLineEdit->text();
  // launcherName is the display label, trim whitespace.
  config.launcherName = ui->launcherNameLineEdit->text().trimmed();
  config.extractArchives = ui->extractArchivesCheckBox->isChecked();
  config.extractedExtension = ui->extractedExtensionLineEdit->text();
}

bool LauncherTabPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  if (ui->launcherLineEdit->text() != o.launcherPath) return true;
  if (ui->coreLineEdit->text() != o.corePath) return true;
  if (ui->launchParamsLineEdit->text() != o.launchParameters) return true;
  if (ui->launcherNameLineEdit->text().trimmed() != o.launcherName) return true;
  if (ui->extractArchivesCheckBox->isChecked() != o.extractArchives) return true;
  if (ui->extractedExtensionLineEdit->text() != o.extractedExtension) return true;
  return false;
}

void LauncherTabPanel::updateExtractedExtensionVisibility() {
  // Launch Extension is meaningful only when extraction is enabled.
  const bool enabled = ui->extractArchivesCheckBox->isChecked();
  ui->label_extractedExtension->setVisible(enabled);
  ui->extractedExtensionLineEdit->setVisible(enabled);
}

QLineEdit *LauncherTabPanel::launcherLineEdit() const {
  return ui->launcherLineEdit;
}
QLineEdit *LauncherTabPanel::coreLineEdit() const {
  return ui->coreLineEdit;
}
QLineEdit *LauncherTabPanel::launchParamsLineEdit() const {
  return ui->launchParamsLineEdit;
}
QLineEdit *LauncherTabPanel::launcherNameLineEdit() const {
  return ui->launcherNameLineEdit;
}
QPushButton *LauncherTabPanel::browseLauncherButton() const {
  return ui->browseLauncherButton;
}
QPushButton *LauncherTabPanel::browseCoreButton() const {
  return ui->browseCoreButton;
}
QCheckBox *LauncherTabPanel::extractArchivesCheckBox() const {
  return ui->extractArchivesCheckBox;
}
QLineEdit *LauncherTabPanel::extractedExtensionLineEdit() const {
  return ui->extractedExtensionLineEdit;
}
QGroupBox *LauncherTabPanel::launcherArchiveGroupBox() const {
  return ui->launcherArchiveGroupBox;
}
QListWidget *LauncherTabPanel::additionalLaunchersList() const {
  return ui->additionalLaunchersList;
}
QPushButton *LauncherTabPanel::addAdditionalLauncherButton() const {
  return ui->addAdditionalLauncherButton;
}
QPushButton *LauncherTabPanel::editAdditionalLauncherButton() const {
  return ui->editAdditionalLauncherButton;
}
QPushButton *LauncherTabPanel::removeAdditionalLauncherButton() const {
  return ui->removeAdditionalLauncherButton;
}
QComboBox *LauncherTabPanel::defaultLauncherComboBox() const {
  return ui->defaultLauncherComboBox;
}
QLabel *LauncherTabPanel::labelCore() const {
  return ui->label_core;
}
QLabel *LauncherTabPanel::labelExtractedExtension() const {
  return ui->label_extractedExtension;
}
