#include "configurationpanel.h"

#include "extensionutils.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_configurationpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>

ConfigurationPanel::ConfigurationPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::ConfigurationPanel) {
  ui->setupUi(this);

  // Per-field changed() emission. Parent combo + linked-parents + recursive-
  // import are not wired here — the host dialog owns those connections.
  connect(ui->collectionTypeComboBox, &QComboBox::currentTextChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->mediaDirLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->fileExtensionsLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->expandModeCheckBox, &QCheckBox::toggled, this, [this](bool) { emit changed(); });
  connect(ui->showAllSubcollectionItemsCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
}

ConfigurationPanel::~ConfigurationPanel() {
  delete ui;
}

void ConfigurationPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void ConfigurationPanel::load() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 ||
      *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  // collectionTypeComboBox is populated separately via setKnownTypes; just
  // set the current text here.
  {
    QSignalBlocker blocker(ui->collectionTypeComboBox);
    ui->collectionTypeComboBox->setCurrentText(config.type);
  }
  SettingsFormBinding::loadInto(ui->mediaDirLineEdit, config.mediaDirectory);
  SettingsFormBinding::loadInto(ui->fileExtensionsLineEdit, config.extensions.join(", "));
  SettingsFormBinding::loadInto(ui->expandModeCheckBox, config.expandMode);
  SettingsFormBinding::loadInto(ui->showAllSubcollectionItemsCheckBox,
                                config.showAllSubcollectionItems);
}

void ConfigurationPanel::clear() {
  {
    QSignalBlocker blocker(ui->collectionTypeComboBox);
    ui->collectionTypeComboBox->clear();
  }
  ui->mediaDirLineEdit->clear();
  ui->fileExtensionsLineEdit->clear();
  ui->expandModeCheckBox->setChecked(false);
  ui->showAllSubcollectionItemsCheckBox->setChecked(false);
}

void ConfigurationPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 ||
      *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  // Read free-form type label from the editable combobox. currentText()
  // (rather than currentIndex()) lets a freshly typed value not yet
  // committed via Enter round-trip; trimming prevents accidental padding
  // from fragmenting the type set across collections.
  config.type = ui->collectionTypeComboBox->currentText().trimmed();
  // Media dir preserves exact text (no trim) — matches legacy behavior.
  config.mediaDirectory = ui->mediaDirLineEdit->text();
  config.extensions = ExtensionUtils::parseUserExtensionList(ui->fileExtensionsLineEdit->text());
  config.expandMode = ui->expandModeCheckBox->isChecked();
  config.showAllSubcollectionItems = ui->showAllSubcollectionItemsCheckBox->isChecked();
}

bool ConfigurationPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  if (ui->collectionTypeComboBox->currentText().trimmed() != o.type) return true;
  if (ui->mediaDirLineEdit->text() != o.mediaDirectory) return true;
  if (ui->expandModeCheckBox->isChecked() != o.expandMode) return true;
  if (ui->showAllSubcollectionItemsCheckBox->isChecked() != o.showAllSubcollectionItems)
    return true;
  // Compare against the parsed form so cosmetic comma-spacing tweaks don't
  // register as dirty.
  const QStringList parsed =
      ExtensionUtils::parseUserExtensionList(ui->fileExtensionsLineEdit->text());
  if (parsed != o.extensions) return true;
  return false;
}

void ConfigurationPanel::setKnownTypes(const QStringList &knownTypes, const QString &currentText) {
  QSignalBlocker blocker(ui->collectionTypeComboBox);
  ui->collectionTypeComboBox->clear();
  ui->collectionTypeComboBox->addItems(knownTypes);
  ui->collectionTypeComboBox->setCurrentText(currentText);
}

QComboBox *ConfigurationPanel::parentCollectionComboBox() const {
  return ui->parentCollectionComboBox;
}
QPushButton *ConfigurationPanel::editLinkedParentsButton() const {
  return ui->editLinkedParentsButton;
}
QPushButton *ConfigurationPanel::recursiveImportContentButton() const {
  return ui->recursiveImportContentButton;
}
QPushButton *ConfigurationPanel::browseMediaDirButton() const {
  return ui->browseMediaDirButton;
}
QLineEdit *ConfigurationPanel::mediaDirLineEdit() const {
  return ui->mediaDirLineEdit;
}
QLineEdit *ConfigurationPanel::fileExtensionsLineEdit() const {
  return ui->fileExtensionsLineEdit;
}
QComboBox *ConfigurationPanel::collectionTypeComboBox() const {
  return ui->collectionTypeComboBox;
}
