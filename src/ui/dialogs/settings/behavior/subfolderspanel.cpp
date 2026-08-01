#include "subfolderspanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_subfolderspanel.h"

#include <QCheckBox>

SubfoldersPanel::SubfoldersPanel(QWidget *parent) : QWidget(parent), ui(new Ui::SubfoldersPanel) {
  ui->setupUi(this);

  // Toggle the dependent-options widget alongside the include checkbox so
  // the dependent flags can't be set while the include flag is off.
  connect(ui->includeContentSubfoldersCheckBox, &QCheckBox::toggled, this, [this](bool) {
    updateOptionsVisibility();
    emit changed();
  });
  for (auto *box : {ui->showAllSubfolderItemsCheckBox, ui->hideSubfolderTitlesCheckBox,
                    ui->showHiddenFoldersCheckBox, ui->includeArtworkSubfoldersCheckBox}) {
    connect(box, &QCheckBox::toggled, this, [this](bool) { emit changed(); });
  }

  updateOptionsVisibility();
}

SubfoldersPanel::~SubfoldersPanel() {
  delete ui;
}

void SubfoldersPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void SubfoldersPanel::load() {
  const CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  const CollectionConfig &config = *current;
  SettingsFormBinding::loadInto(ui->includeContentSubfoldersCheckBox,
                                config.folderBrowsing.includeContentSubfolders);
  SettingsFormBinding::loadInto(ui->showAllSubfolderItemsCheckBox,
                                config.folderBrowsing.showAllSubfolderItems);
  SettingsFormBinding::loadInto(ui->hideSubfolderTitlesCheckBox,
                                config.folderBrowsing.hideSubfolderTitles);
  SettingsFormBinding::loadInto(ui->showHiddenFoldersCheckBox,
                                config.folderBrowsing.showHiddenFolders);
  SettingsFormBinding::loadInto(ui->includeArtworkSubfoldersCheckBox,
                                config.folderBrowsing.includeArtworkSubfolders);
  updateOptionsVisibility();
}

void SubfoldersPanel::clear() {
  ui->includeContentSubfoldersCheckBox->setChecked(false);
  ui->showAllSubfolderItemsCheckBox->setChecked(false);
  ui->hideSubfolderTitlesCheckBox->setChecked(false);
  ui->showHiddenFoldersCheckBox->setChecked(false);
  ui->includeArtworkSubfoldersCheckBox->setChecked(false);
  updateOptionsVisibility();
}

void SubfoldersPanel::save() {
  CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  CollectionConfig &config = *current;
  config.folderBrowsing.includeContentSubfolders =
      ui->includeContentSubfoldersCheckBox->isChecked();
  config.folderBrowsing.showAllSubfolderItems = ui->showAllSubfolderItemsCheckBox->isChecked();
  config.folderBrowsing.hideSubfolderTitles = ui->hideSubfolderTitlesCheckBox->isChecked();
  config.folderBrowsing.showHiddenFolders = ui->showHiddenFoldersCheckBox->isChecked();
  config.folderBrowsing.includeArtworkSubfolders =
      ui->includeArtworkSubfoldersCheckBox->isChecked();
}

bool SubfoldersPanel::isContentSubfoldersIncluded() const {
  return ui->includeContentSubfoldersCheckBox->isChecked();
}

void SubfoldersPanel::updateOptionsVisibility() {
  ui->subfolderOptionsWidget->setVisible(ui->includeContentSubfoldersCheckBox->isChecked());
}
