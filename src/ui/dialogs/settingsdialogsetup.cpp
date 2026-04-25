// Sibling translation unit for SettingsDialog.
// Extracted from settingsdialogform.cpp during LOC-reduction refactor.
// These remain SettingsDialog members; this is a translation-unit split.
#include <algorithm>
#include <functional>
#include <QAbstractItemView>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPixmapCache>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <set>

#include "extensionutils.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

void SettingsDialog::setupBasicUIConnections() {
  connect(ui->saveCollectionButton, &QPushButton::clicked, this, [this]() {
    if (currentCollectionIndex < 0 || currentCollectionIndex >= collections.size()) {
      return;
    }
    if (!ui->gridWidthSpinBox) {
      return;
    }

    int editedIndex = currentCollectionIndex;
    handleSaveCollection(editedIndex);
  });
}

void SettingsDialog::handleSaveCollection(int editedIndex, bool refreshTree) {
  int newGridWidth = ui->gridWidthSpinBox->value();
  bool isActive =
      (editedIndex == originalCurrentCollectionIndex && originalCurrentCollectionIndex >= 0 &&
       originalCurrentCollectionIndex < collections.size());
  bool gridWidthChangedFlag = (newGridWidth != originalCollection.gridWidth);
  if (isActive && gridWidthChangedFlag) {
    m_gridWidthChangedForActiveCollection = true;
    m_newGridWidthForActiveCollection = newGridWidth;
  }

  // Check if database-affecting fields changed before saving
  // These fields affect the UUID or database content and require a rescan
  QString newName = originalCollection.name;
  if (collectionIndexToItem.contains(editedIndex) && collectionIndexToItem[editedIndex]) {
    newName = collectionIndexToItem[editedIndex]->text(0);
  }
  QString newMediaDir = ui->mediaDirLineEdit ? ui->mediaDirLineEdit->text().trimmed()
                                             : originalCollection.mediaDirectory;
  QString newExtensions = ui->fileExtensionsLineEdit ? ui->fileExtensionsLineEdit->text().trimmed()
                                                     : originalCollection.extensions.join(", ");
  bool newIncludeSubfolders = ui->includeContentSubfoldersCheckBox
                                  ? ui->includeContentSubfoldersCheckBox->isChecked()
                                  : originalCollection.includeContentSubfolders;

  bool databaseFieldsChanged =
      (newName != originalCollection.name) || (newMediaDir != originalCollection.mediaDirectory) ||
      (newExtensions != originalCollection.extensions.join(", ")) ||
      (newIncludeSubfolders != originalCollection.includeContentSubfolders);

  if (databaseFieldsChanged) {
    m_rescanRequired.insert(editedIndex);
  }

  saveCollectionFromUI(editedIndex);
  originalCollection = collections[editedIndex];

  // Also save general settings (e.g., titleTintSaturation, titleTintLightness)
  saveGeneralSettingsFromUI();
  m_originalGeneralSettings = m_generalSettings;

  m_collectionSaved = true;
  updateSaveButtonStyle();
  emit collectionSaved(collections);
  if (isActive && gridWidthChangedFlag) {
    emitGridWidthChanged();
  }

  if (!refreshTree) {
    return;
  }

  QSignalBlocker blocker(collectionTreeWidget);
  // Rebuild tree to reflect parent changes immediately and reselect the
  // edited collection
  updateCollectionTreeWidget();
  expandPathToCollection(editedIndex);
  if (collectionIndexToItem.contains(editedIndex)) {
    QTreeWidgetItem *item = collectionIndexToItem[editedIndex];
    if (item) {
      collectionTreeWidget->setCurrentItem(item);
      item->setSelected(true);
    }
  }
  loadCollectionToUI(editedIndex);
}

void SettingsDialog::setupFormFieldConnections() {
  if (ui->launcherLineEdit) {
    connect(ui->launcherLineEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
    connect(ui->launcherLineEdit, &QLineEdit::textChanged, this,
            [this](const QString &text) { updateUIForLauncherType(text); });
  }
  if (ui->coreLineEdit) {
    connect(ui->coreLineEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
  }
  if (ui->launchParamsLineEdit) {
    connect(ui->launchParamsLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->extractArchivesCheckBox) {
    connect(ui->extractArchivesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
    connect(ui->extractArchivesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::onExtractArchivesToggled);
  }
  if (ui->extractedExtensionLineEdit) {
    connect(ui->extractedExtensionLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->mediaDirLineEdit) {
    connect(ui->mediaDirLineEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
    connect(ui->mediaDirLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::onContentDirectoryChanged);
  }
  if (ui->artworkDirLineEdit) {
    connect(ui->artworkDirLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->includeContentSubfoldersCheckBox) {
    connect(ui->includeContentSubfoldersCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
    connect(ui->includeContentSubfoldersCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::onIncludeSubfoldersToggled);
  }
  if (ui->showAllSubfolderItemsCheckBox) {
    connect(ui->showAllSubfolderItemsCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideSubfolderTitlesCheckBox) {
    connect(ui->hideSubfolderTitlesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->showHiddenFoldersCheckBox) {
    connect(ui->showHiddenFoldersCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->includeArtworkSubfoldersCheckBox) {
    connect(ui->includeArtworkSubfoldersCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->fileExtensionsLineEdit) {
    connect(ui->fileExtensionsLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->gridWidthSpinBox) {
    connect(ui->gridWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::onGridWidthChanged);
  }
  if (ui->showAllSubcollectionItemsCheckBox) {
    connect(ui->showAllSubcollectionItemsCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->horizontalAlignmentComboBox) {
    connect(ui->horizontalAlignmentComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::checkForChanges);
  }
  if (ui->viewTypeComboBox) {
    connect(ui->viewTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->parentCollectionComboBox) {
    connect(ui->parentCollectionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarModeComboBox) {
    connect(ui->sidebarModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideHorizontalScrollbarCheckBox) {
    connect(ui->hideHorizontalScrollbarCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideVerticalScrollbarCheckBox) {
    connect(ui->hideVerticalScrollbarCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideTitlesCheckBox) {
    connect(ui->hideTitlesCheckBox, &QCheckBox::toggled, this, &SettingsDialog::checkForChanges);
  }
  if (ui->hideSubcollectionTitlesCheckBox) {
    connect(ui->hideSubcollectionTitlesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->itemWidthSpinBox) {
    connect(ui->itemWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->itemHeightSpinBox) {
    connect(ui->itemHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->fontSizeSpinBox) {
    connect(ui->fontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->cornerRadiusSpinBox) {
    connect(ui->cornerRadiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  // Color field connections
  if (ui->primaryColorEdit) {
    connect(ui->primaryColorEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
  }
  if (ui->tileColorEdit) {
    connect(ui->tileColorEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
  }
  if (ui->selectionColorEdit) {
    connect(ui->selectionColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  // List mode field connections
  if (ui->listFontSizeSpinBox) {
    connect(ui->listFontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->listRowHeightSpinBox) {
    connect(ui->listRowHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->listRowColorEdit) {
    connect(ui->listRowColorEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
  }
  if (ui->listAltRowColorEdit) {
    connect(ui->listAltRowColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  // Background field connections
  if (ui->backgroundValueEdit) {
    connect(ui->backgroundValueEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->backgroundColorRadio) {
    connect(ui->backgroundColorRadio, &QRadioButton::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->backgroundImageRadio) {
    connect(ui->backgroundImageRadio, &QRadioButton::toggled, this,
            &SettingsDialog::checkForChanges);
  }
}

void SettingsDialog::setupSpacingConnections() {
  if (ui->horizontalSpacingSpinBox) {
    connect(ui->horizontalSpacingSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
    connect(ui->horizontalSpacingSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this]() { handleSpacingChanged(); });
  }
  if (ui->verticalSpacingSpinBox) {
    connect(ui->verticalSpacingSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
    connect(ui->verticalSpacingSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this]() { handleSpacingChanged(); });
  }
}

void SettingsDialog::handleSpacingChanged() {
  if (!ui->horizontalSpacingSpinBox || !ui->verticalSpacingSpinBox) {
    return;
  }
  if (currentCollectionIndex < 0 || currentCollectionIndex >= collections.size()) {
    return;
  }
  if (originalCurrentCollectionIndex < 0 || originalCurrentCollectionIndex >= collections.size()) {
    return;
  }
  if (currentCollectionIndex == originalCurrentCollectionIndex) {
    emit spacingChanged(currentCollectionIndex,
                        spacingUiToInternal(ui->horizontalSpacingSpinBox->value()),
                        spacingUiToInternal(ui->verticalSpacingSpinBox->value()));
  }
}

void SettingsDialog::setupTreeWidgetConnections() {
  if (collectionTreeWidget) {
    connect(collectionTreeWidget, &QTreeWidget::itemSelectionChanged, this,
            &SettingsDialog::onTreeItemSelectionChanged);
    connect(collectionTreeWidget, &QTreeWidget::itemChanged, this,
            &SettingsDialog::onTreeItemChanged);
    collectionTreeWidget->setEditTriggers(QAbstractItemView::EditKeyPressed |
                                          QAbstractItemView::DoubleClicked);
  }
}

void SettingsDialog::setupUIConstraints() {
  if (ui->horizontalSpacingSpinBox) {
    ui->horizontalSpacingSpinBox->setMinimum(
        spacingInternalToUi(UIConstants::Viewport::SPACING_MIN));
    ui->horizontalSpacingSpinBox->setMaximum(
        spacingInternalToUi(UIConstants::Viewport::SPACING_MAX));
    ui->horizontalSpacingSpinBox->setSingleStep(1);
  }
  if (ui->verticalSpacingSpinBox) {
    ui->verticalSpacingSpinBox->setMinimum(
        spacingInternalToUi(UIConstants::Viewport::SPACING_MIN));
    ui->verticalSpacingSpinBox->setMaximum(
        spacingInternalToUi(UIConstants::Viewport::SPACING_MAX));
    ui->verticalSpacingSpinBox->setSingleStep(1);
  }
  if (ui->gridWidthSpinBox) {
    ui->gridWidthSpinBox->setMinimum(UIConstants::Grid::MIN_WIDTH);
    ui->gridWidthSpinBox->setMaximum(UIConstants::Grid::MAX_WIDTH);
    ui->gridWidthSpinBox->setSingleStep(1);
  }
  if (ui->fontSizeSpinBox) {
    ui->fontSizeSpinBox->setMinimum(UIConstants::Item::MIN_FONT_SIZE);
    ui->fontSizeSpinBox->setMaximum(UIConstants::Item::MAX_FONT_SIZE);
    ui->fontSizeSpinBox->setSingleStep(1);
  }
  if (ui->cornerRadiusSpinBox) {
    ui->cornerRadiusSpinBox->setMinimum(UIConstants::Item::MIN_CORNER_RADIUS);
    ui->cornerRadiusSpinBox->setMaximum(UIConstants::Item::MAX_CORNER_RADIUS);
    ui->cornerRadiusSpinBox->setSingleStep(1);
  }
}
