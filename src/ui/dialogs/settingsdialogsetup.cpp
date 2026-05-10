// Sibling translation unit for SettingsDialog.
// Extracted from settingsdialogform.cpp during LOC-reduction refactor.
// These remain SettingsDialog members; this is a translation-unit split.
#include <algorithm>
#include <functional>
#include <QAbstractItemView>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDialog>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmapCache>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <set>

#include "collectiontreewidget.h"
#include "configurationpanel.h"
#include "extensionutils.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "launchertabpanel.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "subfolderspanel.h"
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
  QString newMediaDir = ui->configurationPanel->mediaDirLineEdit()
                            ? ui->configurationPanel->mediaDirLineEdit()->text().trimmed()
                            : originalCollection.mediaDirectory;
  QString newExtensions = ui->configurationPanel->fileExtensionsLineEdit()
                              ? ui->configurationPanel->fileExtensionsLineEdit()->text().trimmed()
                              : originalCollection.extensions.join(", ");
  bool newIncludeSubfolders = ui->subfoldersPanel
                                  ? ui->subfoldersPanel->isContentSubfoldersIncluded()
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

  // if the user selected a broader Settings Mode, propagate
  // the curated appearance/layout subset (same fields as 's
  // explicit Apply action) from the just-saved collection to the chosen
  // scope. This is silent because the mode itself is the user's opt-in.
  if (m_settingsScope != SettingsScope::Current && editedIndex >= 0 &&
      editedIndex < collections.size()) {
    QList<int> targets;
    if (m_settingsScope == SettingsScope::CurrentAndSubcollections) {
      targets = CollectionUtils::collectDescendantIndices(editedIndex, collections);
    } else { // SettingsScope::All
      targets.reserve(collections.size());
      for (int i = 0; i < collections.size(); ++i) {
        if (i != editedIndex) {
          targets.append(i);
        }
      }
    }
    propagateAppearanceToIndicesSilently(targets);
  }

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
  // Launcher data field connections (path / core / params / name / extract /
  // extension) live on LauncherTabPanel — emits changed() routed to
  // checkForChanges via the dialog constructor. Cross-cutting wiring
  // (additional-launchers list + buttons + default-launcher combo + per-
  // edit launcher-type heuristic) stays here.
  connect(ui->launcherPanel->launcherLineEdit(), &QLineEdit::textChanged, this,
          [this](const QString &text) { updateUIForLauncherType(text); });
  connect(ui->launcherPanel->launcherLineEdit(), &QLineEdit::textChanged, this,
          [this](const QString &) {
            rebuildDefaultLauncherCombo(
                ui->launcherPanel->defaultLauncherComboBox()->currentIndex());
          });
  connect(ui->launcherPanel->launcherNameLineEdit(), &QLineEdit::textChanged, this,
          [this](const QString &) {
            rebuildDefaultLauncherCombo(
                ui->launcherPanel->defaultLauncherComboBox()->currentIndex());
          });
  connect(ui->launcherPanel->addAdditionalLauncherButton(), &QPushButton::clicked, this,
          &SettingsDialog::onAddAdditionalLauncher);
  connect(ui->launcherPanel->editAdditionalLauncherButton(), &QPushButton::clicked, this,
          &SettingsDialog::onEditAdditionalLauncher);
  connect(ui->launcherPanel->removeAdditionalLauncherButton(), &QPushButton::clicked, this,
          &SettingsDialog::onRemoveAdditionalLauncher);
  connect(ui->launcherPanel->additionalLaunchersList(), &QListWidget::currentRowChanged, this,
          [this](int) { onAdditionalLauncherSelectionChanged(); });
  connect(ui->launcherPanel->additionalLaunchersList(), &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem *) { onEditAdditionalLauncher(); });
  connect(ui->launcherPanel->defaultLauncherComboBox(),
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { checkForChanges(); });
  // Configuration-tab data field connections (mediaDir, type, extensions,
  // expandMode, showAllSubcollectionItems) live on ConfigurationPanel.
  // Asset-directory + placeholder-artwork edits live on ArtworkTabPanel.
  // Subfolders edits live on SubfoldersPanel.
  // The mediaDir edit additionally feeds onContentDirectoryChanged for the
  // launcher-type heuristic — wire that here against the panel's accessor.
  connect(ui->configurationPanel->mediaDirLineEdit(), &QLineEdit::textChanged, this,
          &SettingsDialog::onContentDirectoryChanged);
  if (ui->gridWidthSpinBox) {
    connect(ui->gridWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::onGridWidthChanged);
  }
  if (ui->horizontalGridHeightSpinBox) {
    // feeds the same change-detection path as gridWidth so the
    // dialog enables Save when only this field is touched.
    connect(ui->horizontalGridHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->gridWidthSidebarHiddenSpinBox) {
    connect(ui->gridWidthSidebarHiddenSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->horizontalGridHeightSidebarHiddenSpinBox) {
    connect(ui->horizontalGridHeightSidebarHiddenSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::checkForChanges);
  }
  // showAllSubcollectionItemsCheckBox connection lives on ConfigurationPanel.
  if (ui->horizontalAlignmentComboBox) {
    connect(ui->horizontalAlignmentComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::checkForChanges);
  }
  if (ui->viewTypeComboBox) {
    connect(ui->viewTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->configurationPanel->parentCollectionComboBox()) {
    connect(ui->configurationPanel->parentCollectionComboBox(),
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
  }
  // Sidebar / Details Pane fields are wired internally by SidebarPanel; the
  // dialog observes the panel's changed() signal in the constructor.
  if (ui->hideTitlesCheckBox) {
    connect(ui->hideTitlesCheckBox, &QCheckBox::toggled, this, &SettingsDialog::checkForChanges);
  }
  if (ui->hideSubcollectionTitlesCheckBox) {
    connect(ui->hideSubcollectionTitlesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideMissingArtworkCheckBox) {
    connect(ui->hideMissingArtworkCheckBox, &QCheckBox::toggled, this,
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
  // List font size + row height connections live on AppearanceListPanel.
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
  // backgroundVideoRadio + the appearance-effects cluster
  // (header logo, vignette, parallax, toolbar backdrop blur) all persist
  // into CollectionConfig but were never wired to the dirty state.
  if (ui->backgroundVideoRadio) {
    connect(ui->backgroundVideoRadio, &QRadioButton::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->headerLogoEdit) {
    connect(ui->headerLogoEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
  }
  if (ui->headerLogoPositionComboBox) {
    connect(ui->headerLogoPositionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::checkForChanges);
  }
  if (ui->vignetteEnabledCheckBox) {
    connect(ui->vignetteEnabledCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->vignetteIntensitySpinBox) {
    connect(ui->vignetteIntensitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->wallpaperParallaxCheckBox) {
    connect(ui->wallpaperParallaxCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->parallaxStrengthSpinBox) {
    connect(ui->parallaxStrengthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->toolbarBackdropBlurCheckBox) {
    connect(ui->toolbarBackdropBlurCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->backdropBlurRadiusSpinBox) {
    connect(ui->backdropBlurRadiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
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
  if (!collectionTreeWidget) {
    return;
  }
  connect(collectionTreeWidget, &QTreeWidget::itemSelectionChanged, this,
          &SettingsDialog::onTreeItemSelectionChanged);
  connect(collectionTreeWidget, &QTreeWidget::itemChanged, this,
          &SettingsDialog::onTreeItemChanged);
  collectionTreeWidget->setEditTriggers(QAbstractItemView::EditKeyPressed |
                                        QAbstractItemView::DoubleClicked);

  // right-click context menu surfaces Rename/Duplicate/Delete
  // and expand/collapse helpers where the user's pointer already is.
  collectionTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(collectionTreeWidget, &QWidget::customContextMenuRequested, this,
          &SettingsDialog::onTreeContextMenuRequested);

  // drag-drop reparenting. The promoted CollectionTreeWidget
  // delegates cycle validation to wouldCreateCircularReference() and emits
  // treeRearranged() on success so we can resync parentCollectionIndex.
  collectionTreeWidget->setCycleCheck([this](int childIndex, int parentIndex) {
    return wouldCreateCircularReference(childIndex, parentIndex);
  });
  collectionTreeWidget->setItemToIndex([this](const QTreeWidgetItem *item) {
    return itemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(item), -1);
  });
  connect(collectionTreeWidget, &CollectionTreeWidget::treeRearranged, this,
          &SettingsDialog::onTreeRearranged);
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
    ui->verticalSpacingSpinBox->setMinimum(spacingInternalToUi(UIConstants::Viewport::SPACING_MIN));
    ui->verticalSpacingSpinBox->setMaximum(spacingInternalToUi(UIConstants::Viewport::SPACING_MAX));
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
