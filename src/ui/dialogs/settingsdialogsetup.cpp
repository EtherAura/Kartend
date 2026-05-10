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
  if (ui->launcherLineEdit) {
    connect(ui->launcherLineEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
    connect(ui->launcherLineEdit, &QLineEdit::textChanged, this,
            [this](const QString &text) { updateUIForLauncherType(text); });
    // keep the default-launcher combo's primary label in sync
    // with the live launcher path / name as the user edits.
    connect(ui->launcherLineEdit, &QLineEdit::textChanged, this, [this](const QString &) {
      const int idx = ui->defaultLauncherComboBox ? ui->defaultLauncherComboBox->currentIndex() : 0;
      rebuildDefaultLauncherCombo(idx);
    });
  }
  if (ui->launcherNameLineEdit) {
    connect(ui->launcherNameLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
    connect(ui->launcherNameLineEdit, &QLineEdit::textChanged, this, [this](const QString &) {
      const int idx = ui->defaultLauncherComboBox ? ui->defaultLauncherComboBox->currentIndex() : 0;
      rebuildDefaultLauncherCombo(idx);
    });
  }
  if (ui->addAdditionalLauncherButton) {
    connect(ui->addAdditionalLauncherButton, &QPushButton::clicked, this,
            &SettingsDialog::onAddAdditionalLauncher);
  }
  if (ui->editAdditionalLauncherButton) {
    connect(ui->editAdditionalLauncherButton, &QPushButton::clicked, this,
            &SettingsDialog::onEditAdditionalLauncher);
  }
  if (ui->removeAdditionalLauncherButton) {
    connect(ui->removeAdditionalLauncherButton, &QPushButton::clicked, this,
            &SettingsDialog::onRemoveAdditionalLauncher);
  }
  if (ui->additionalLaunchersList) {
    connect(ui->additionalLaunchersList, &QListWidget::currentRowChanged, this,
            [this](int) { onAdditionalLauncherSelectionChanged(); });
    connect(ui->additionalLaunchersList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { onEditAdditionalLauncher(); });
  }
  if (ui->defaultLauncherComboBox) {
    connect(ui->defaultLauncherComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { checkForChanges(); });
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
  if (ui->expandModeCheckBox) {
    connect(ui->expandModeCheckBox, &QCheckBox::toggled, this, &SettingsDialog::checkForChanges);
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
  // video / manual directories were missing dirty wiring; the
  // settings save reads them but the save icon stayed dim until any other
  // field changed too.
  if (ui->videoDirLineEdit) {
    connect(ui->videoDirLineEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
  }
  if (ui->manualDirLineEdit) {
    connect(ui->manualDirLineEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
  }
  if (ui->placeholderArtworkLineEdit) {
    connect(ui->placeholderArtworkLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  // collection type combo (free-form, editable). currentTextChanged
  // covers both selection and free typing; currentIndexChanged alone misses
  // the user typing a brand-new label.
  if (ui->collectionTypeComboBox) {
    connect(ui->collectionTypeComboBox, &QComboBox::currentTextChanged, this,
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
  if (ui->customArtworkTypesLineEdit) {
    connect(ui->customArtworkTypesLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
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
  // sidebar enhancements — wire all new sidebar fields to the
  // dirty-check.
  if (ui->sidebarPositionComboBox) {
    connect(ui->sidebarPositionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
    // position drives whether Width or Height is exposed.
    connect(ui->sidebarPositionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::updateSidebarModeVisibility);
  }
  if (ui->sidebarWidthSpinBox) {
    connect(ui->sidebarWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarHeightSpinBox) {
    connect(ui->sidebarHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarWidthLockedCheckBox) {
    connect(ui->sidebarWidthLockedCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarBackgroundTypeComboBox) {
    connect(ui->sidebarBackgroundTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarBackgroundValueEdit) {
    connect(ui->sidebarBackgroundValueEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarPatternIntensitySpinBox) {
    connect(ui->sidebarPatternIntensitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarPatternColorEdit) {
    connect(ui->sidebarPatternColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarTextColorEdit) {
    connect(ui->sidebarTextColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarAccentColorEdit) {
    connect(ui->sidebarAccentColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarHeaderBgEdit) {
    connect(ui->sidebarHeaderBgEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarSectionBgEdit) {
    connect(ui->sidebarSectionBgEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarHeaderBgOpacitySpinBox) {
    connect(ui->sidebarHeaderBgOpacitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarSectionBgOpacitySpinBox) {
    connect(ui->sidebarSectionBgOpacitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  // per-collection sidebar font controls.
  if (ui->sidebarFontFamilyEdit) {
    connect(ui->sidebarFontFamilyEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarFontSizeSpinBox) {
    connect(ui->sidebarFontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarFontPickButton && ui->sidebarFontFamilyEdit) {
    connect(ui->sidebarFontPickButton, &QPushButton::clicked, this, [this]() {
      bool ok = false;
      QFont currentFont = QApplication::font();
      const QString currentFamily = ui->sidebarFontFamilyEdit->text().trimmed();
      if (!currentFamily.isEmpty()) {
        currentFont.setFamily(currentFamily);
      }
      if (ui->sidebarFontSizeSpinBox && ui->sidebarFontSizeSpinBox->value() > 0) {
        currentFont.setPointSize(ui->sidebarFontSizeSpinBox->value());
      }
      const QFont chosen =
          QFontDialog::getFont(&ok, currentFont, this, tr("Select Details Pane Font"));
      if (ok) {
        ui->sidebarFontFamilyEdit->setText(chosen.family());
        if (ui->sidebarFontSizeSpinBox && chosen.pointSize() > 0) {
          ui->sidebarFontSizeSpinBox->setValue(chosen.pointSize());
        }
      }
    });
  }
  if (ui->sidebarActiveTabComboBox) {
    connect(ui->sidebarActiveTabComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
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
