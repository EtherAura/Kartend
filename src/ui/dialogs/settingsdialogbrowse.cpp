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
#include "sidebarpanel.h"
#include "subfolderspanel.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

void SettingsDialog::checkForChanges() {
  // Skip change detection during programmatic loading to avoid
  // false positives before originalCollection is set
  if (m_isLoading) {
    return;
  }
  m_collectionSaved = !hasUnsavedChanges();
  updateSaveButtonStyle();
}

void SettingsDialog::browseLauncher() {
  QString fileName =
      QFileDialog::getOpenFileName(this, tr("Select Launcher"), "", tr("All Files (*)"));
  if (!fileName.isEmpty() && ui->launcherLineEdit) {
    ui->launcherLineEdit->setText(fileName);
  }
}

void SettingsDialog::browseCore() {
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Select Core"), "", tr("Core Files (*.so *.dll *.dylib);;All Files (*)"));
  if (!fileName.isEmpty() && ui->coreLineEdit) {
    ui->coreLineEdit->setText(fileName);
  }
}

void SettingsDialog::browseMediaDir() {
  QString dirName = QFileDialog::getExistingDirectory(this, tr("Select Media Directory"), "");
  if (!dirName.isEmpty() && ui->mediaDirLineEdit) {
    ui->mediaDirLineEdit->setText(dirName);
  }
}

void SettingsDialog::browseArtworkDir() {
  QString dirName = QFileDialog::getExistingDirectory(this, tr("Select Artwork Directory"), "");
  if (!dirName.isEmpty() && ui->artworkDirLineEdit) {
    ui->artworkDirLineEdit->setText(dirName);
  }
}

void SettingsDialog::browseVideoDir() {
  QString dirName = QFileDialog::getExistingDirectory(this, tr("Select Video Directory"), "");
  if (!dirName.isEmpty() && ui->videoDirLineEdit) {
    ui->videoDirLineEdit->setText(dirName);
  }
}

void SettingsDialog::browseManualDir() {
  QString dirName = QFileDialog::getExistingDirectory(this, tr("Select Manual Directory"), "");
  if (!dirName.isEmpty() && ui->manualDirLineEdit) {
    ui->manualDirLineEdit->setText(dirName);
  }
}

void SettingsDialog::browsePlaceholderArtwork() {
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Select Placeholder Artwork"), "",
      tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
  if (!fileName.isEmpty() && ui->placeholderArtworkLineEdit) {
    ui->placeholderArtworkLineEdit->setText(fileName);
  }
}

void SettingsDialog::onRecursiveImportContent() {
  if (!ui->mediaDirLineEdit) {
    return;
  }
  QString baseDir = ui->mediaDirLineEdit->text().trimmed();
  if (baseDir.isEmpty()) {
    QMessageBox::warning(this, tr("Recursive Import"),
                         tr("Please specify a content directory first."));
    return;
  }
  performRecursiveImport(baseDir, true);
}

void SettingsDialog::onRecursiveImportArtwork() {
  if (!ui->artworkDirLineEdit) {
    return;
  }
  QString baseDir = ui->artworkDirLineEdit->text().trimmed();
  if (baseDir.isEmpty()) {
    QMessageBox::warning(this, tr("Recursive Import"),
                         tr("Please specify an artwork directory first."));
    return;
  }
  performRecursiveImport(baseDir, false);
}

void SettingsDialog::performRecursiveImport(const QString &baseDir, bool isContentDir) {
  QDir dir(baseDir);
  if (!dir.exists()) {
    QMessageBox::warning(this, tr("Recursive Import"),
                         tr("The specified directory does not exist."));
    return;
  }

  // Get list of subdirectories
  QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  if (subdirs.isEmpty()) {
    QMessageBox::information(this, tr("Recursive Import"),
                             tr("No subdirectories found in the specified directory."));
    return;
  }

  // Show confirmation dialog
  QString message = tr("This will create %1 subcollection(s) based on the "
                       "directory structure:\n\n")
                        .arg(subdirs.size());
  for (int i = 0; i < qMin(subdirs.size(), 10); ++i) {
    message += QString("  • %1\n").arg(subdirs[i]);
  }
  if (subdirs.size() > 10) {
    message += tr("  ... and %1 more\n").arg(subdirs.size() - 10);
  }
  message += tr("\nEach subcollection will inherit the current collection's settings.\n");
  if (isContentDir) {
    message += tr("Content directories will be set automatically.");
  } else {
    message += tr("Artwork directories will be set automatically.");
  }

  QMessageBox::StandardButton reply =
      QMessageBox::question(this, tr("Confirm Recursive Import"), message,
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (reply != QMessageBox::Yes) {
    return;
  }

  // Get current collection as template
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_workingCollections.size()) {
    return;
  }

  // Save current collection first
  handleSaveCollection(currentCollectionIndex, false);

  // Copy by value to avoid reference invalidation when m_workingCollections is
  // appended to
  const CollectionConfig templateConfig = m_workingCollections[currentCollectionIndex];
  int parentIndex = currentCollectionIndex;

  // Create subcollections for each subdirectory
  for (const QString &subdir : subdirs) {
    CollectionConfig newCollection = templateConfig;
    newCollection.name = subdir;
    newCollection.parentCollectionIndex = parentIndex;
    newCollection.isSubcollection = true;

    // Set the directory paths
    if (isContentDir) {
      newCollection.mediaDirectory = dir.absoluteFilePath(subdir);
      // Try to match artwork directory structure if it exists
      if (!templateConfig.artworkDirectory.isEmpty()) {
        QDir artworkBase(templateConfig.artworkDirectory);
        QString potentialArtworkDir = artworkBase.absoluteFilePath(subdir);
        if (QDir(potentialArtworkDir).exists()) {
          newCollection.artworkDirectory = potentialArtworkDir;
        }
      }
    } else {
      newCollection.artworkDirectory = dir.absoluteFilePath(subdir);
      // Try to match content directory structure if it exists
      if (!templateConfig.mediaDirectory.isEmpty()) {
        QDir mediaBase(templateConfig.mediaDirectory);
        QString potentialMediaDir = mediaBase.absoluteFilePath(subdir);
        if (QDir(potentialMediaDir).exists()) {
          newCollection.mediaDirectory = potentialMediaDir;
        }
      }
    }

    // Clear the virtual subfolder state
    newCollection.currentSubfolder.clear();

    m_workingCollections.append(newCollection);
    collections.append(newCollection);
  }

  // Refresh the tree widget
  updateCollectionTreeWidget();
  expandPathToCollection(currentCollectionIndex);

  // Reselect current collection
  if (collectionIndexToItem.contains(currentCollectionIndex)) {
    QTreeWidgetItem *item = collectionIndexToItem[currentCollectionIndex];
    if (item) {
      collectionTreeWidget->setCurrentItem(item);
      item->setSelected(true);
      item->setExpanded(true);
    }
  }

  emit collectionSaved(collections);

  QMessageBox::information(this, tr("Recursive Import"),
                           tr("Successfully created %1 subcollection(s).").arg(subdirs.size()));
}

void SettingsDialog::loadCollectionToUI(int index) {
  if (!CollectionUtils::isValidIndex(index, m_workingCollections)) {
    return;
  }
  m_isLoading = true;
  const CollectionConfig &config = m_workingCollections[index];

  if (ui->launcherLineEdit) {
    ui->launcherLineEdit->setText(config.launcherPath);
  }
  if (ui->coreLineEdit) {
    ui->coreLineEdit->setText(config.corePath);
  }
  if (ui->launchParamsLineEdit) {
    ui->launchParamsLineEdit->setText(config.launchParameters);
  }
  if (ui->launcherNameLineEdit) {
    ui->launcherNameLineEdit->setText(config.launcherName);
  }
  loadAdditionalLaunchersToUI(config);
  loadLinkedParentsToUI(config);
  if (ui->extractArchivesCheckBox) {
    ui->extractArchivesCheckBox->setChecked(config.extractArchives);
  }
  if (ui->extractedExtensionLineEdit) {
    ui->extractedExtensionLineEdit->setText(config.extractedExtension);
  }
  if (ui->expandModeCheckBox) {
    ui->expandModeCheckBox->setChecked(config.expandMode);
  }
  if (ui->mediaDirLineEdit) {
    ui->mediaDirLineEdit->setText(config.mediaDirectory);
  }
  if (ui->artworkDirLineEdit) {
    ui->artworkDirLineEdit->setText(config.artworkDirectory);
  }
  if (ui->videoDirLineEdit) {
    ui->videoDirLineEdit->setText(config.videoDirectory);
  }
  if (ui->manualDirLineEdit) {
    ui->manualDirLineEdit->setText(config.manualDirectory);
  }
  if (ui->placeholderArtworkLineEdit) {
    ui->placeholderArtworkLineEdit->setText(config.placeholderArtwork);
  }
  ui->subfoldersPanel->load(config);
  if (ui->fileExtensionsLineEdit) {
    ui->fileExtensionsLineEdit->setText(config.extensions.join(", "));
  }
  if (ui->customArtworkTypesLineEdit) {
    ui->customArtworkTypesLineEdit->setText(config.customArtworkTypes.join(", "));
  }
  if (ui->collectionTypeComboBox) {
    // rebuild the dropdown from the union of types currently in
    // use across the working list so the user sees everything they've already
    // tagged. Editable=true means free-form values still survive a round-trip
    // even if they aren't in the dropdown.
    QSignalBlocker blocker(ui->collectionTypeComboBox);
    ui->collectionTypeComboBox->clear();
    QStringList types = CollectionUtils::collectAllCollectionTypes(m_workingCollections);
    ui->collectionTypeComboBox->addItem(QString());
    for (const QString &t : types) {
      ui->collectionTypeComboBox->addItem(t);
    }
    ui->collectionTypeComboBox->setEditText(config.type);
  }
  if (ui->gridWidthSpinBox) {
    ui->gridWidthSpinBox->setValue(config.gridWidth);
  }
  if (ui->horizontalGridHeightSpinBox) {
    ui->horizontalGridHeightSpinBox->setValue(config.horizontalGridHeight);
  }
  if (ui->gridWidthSidebarHiddenSpinBox) {
    ui->gridWidthSidebarHiddenSpinBox->setValue(config.gridWidthSidebarHidden);
  }
  if (ui->horizontalGridHeightSidebarHiddenSpinBox) {
    ui->horizontalGridHeightSidebarHiddenSpinBox->setValue(
        config.horizontalGridHeightSidebarHidden);
  }
  if (ui->showAllSubcollectionItemsCheckBox) {
    ui->showAllSubcollectionItemsCheckBox->setChecked(config.showAllSubcollectionItems);
  }
  if (ui->horizontalAlignmentComboBox) {
    ui->horizontalAlignmentComboBox->setCurrentIndex(static_cast<int>(config.horizontalAlignment));
  }
  ui->sidebarPanel->load(config);
  if (ui->viewTypeComboBox) {
    ui->viewTypeComboBox->setCurrentIndex(static_cast<int>(config.viewType));
  }
  if (ui->hideMissingArtworkCheckBox) {
    ui->hideMissingArtworkCheckBox->setChecked(config.hideMissingArtwork);
  }
  if (ui->horizontalSpacingSpinBox) {
    ui->horizontalSpacingSpinBox->setValue(spacingInternalToUi(config.horizontalSpacing));
  }
  if (ui->verticalSpacingSpinBox) {
    ui->verticalSpacingSpinBox->setValue(spacingInternalToUi(config.verticalSpacing));
  }
  if (ui->hideTitlesCheckBox) {
    ui->hideTitlesCheckBox->setChecked(config.hideTitles);
  }
  if (ui->hideSubcollectionTitlesCheckBox) {
    ui->hideSubcollectionTitlesCheckBox->setChecked(config.hideSubcollectionTitles);
  }
  if (ui->itemWidthSpinBox) {
    ui->itemWidthSpinBox->setValue(config.itemWidth);
  }
  if (ui->itemHeightSpinBox) {
    ui->itemHeightSpinBox->setValue(config.itemHeight);
  }
  if (ui->fontSizeSpinBox) {
    ui->fontSizeSpinBox->setValue(config.fontSize);
  }
  if (ui->cornerRadiusSpinBox) {
    ui->cornerRadiusSpinBox->setValue(config.cornerRadius);
  }

  // Background settings
  if (ui->backgroundColorRadio && ui->backgroundImageRadio) {
    if (config.backgroundType == BackgroundType::Video && ui->backgroundVideoRadio) {
      ui->backgroundVideoRadio->setChecked(true);
    } else if (config.backgroundType == BackgroundType::Image) {
      ui->backgroundImageRadio->setChecked(true);
    } else {
      ui->backgroundColorRadio->setChecked(true);
    }
  }
  if (ui->backgroundValueEdit) {
    if (config.backgroundType == BackgroundType::Video) {
      ui->backgroundValueEdit->setText(config.backgroundVideo);
    } else if (config.backgroundType == BackgroundType::Image) {
      ui->backgroundValueEdit->setText(config.backgroundImage);
    } else {
      ui->backgroundValueEdit->setText(config.backgroundColor);
    }
  }

  // Primary color setting
  if (ui->primaryColorEdit) {
    ui->primaryColorEdit->setText(config.primaryColor);
  }

  // Tile color setting
  if (ui->tileColorEdit) {
    ui->tileColorEdit->setText(config.tileColor);
  }

  // Selection color setting
  if (ui->selectionColorEdit) {
    ui->selectionColorEdit->setText(config.selectionColor);
  }

  // List mode settings
  if (ui->listFontSizeSpinBox) {
    ui->listFontSizeSpinBox->setValue(config.listFontSize);
  }
  if (ui->listRowHeightSpinBox) {
    ui->listRowHeightSpinBox->setValue(config.listRowHeight);
  }
  if (ui->listRowColorEdit) {
    ui->listRowColorEdit->setText(config.listRowColor);
  }
  if (ui->listAltRowColorEdit) {
    ui->listAltRowColorEdit->setText(config.listAltRowColor);
  }

  // Custom font family (per-collection)
  if (ui->customFontEdit) {
    ui->customFontEdit->setText(config.customFontFamily);
  }

  // header logo
  if (ui->headerLogoEdit) {
    ui->headerLogoEdit->setText(config.headerLogoImage);
  }
  if (ui->headerLogoPositionComboBox) {
    ui->headerLogoPositionComboBox->setCurrentIndex(static_cast<int>(config.headerLogoPosition));
  }

  // vignette
  if (ui->vignetteEnabledCheckBox) {
    ui->vignetteEnabledCheckBox->setChecked(config.vignetteEnabled);
  }
  if (ui->vignetteIntensitySpinBox) {
    ui->vignetteIntensitySpinBox->setValue(config.vignetteIntensity);
  }

  // wallpaper parallax
  if (ui->wallpaperParallaxCheckBox) {
    ui->wallpaperParallaxCheckBox->setChecked(config.wallpaperParallax);
  }
  if (ui->parallaxStrengthSpinBox) {
    ui->parallaxStrengthSpinBox->setValue(config.parallaxStrength);
  }

  // toolbar backdrop blur
  if (ui->toolbarBackdropBlurCheckBox) {
    ui->toolbarBackdropBlurCheckBox->setChecked(config.toolbarBackdropBlur);
  }
  if (ui->backdropBlurRadiusSpinBox) {
    ui->backdropBlurRadiusSpinBox->setValue(config.backdropBlurRadius);
  }

  updateParentCollectionComboBox(index);
  updateFieldVisibility();
  updateGridWidthLimits();
  m_isLoading = false;
}

void SettingsDialog::clearCollectionUI() {
  m_isLoading = true;

  if (ui->launcherLineEdit) ui->launcherLineEdit->clear();
  if (ui->coreLineEdit) ui->coreLineEdit->clear();
  if (ui->launchParamsLineEdit) ui->launchParamsLineEdit->clear();
  if (ui->launcherNameLineEdit) ui->launcherNameLineEdit->clear();
  clearAdditionalLaunchersUI();
  clearLinkedParentsUI();
  if (ui->mediaDirLineEdit) ui->mediaDirLineEdit->clear();
  if (ui->artworkDirLineEdit) ui->artworkDirLineEdit->clear();
  if (ui->videoDirLineEdit) ui->videoDirLineEdit->clear();
  if (ui->manualDirLineEdit) ui->manualDirLineEdit->clear();
  if (ui->placeholderArtworkLineEdit) ui->placeholderArtworkLineEdit->clear();
  if (ui->fileExtensionsLineEdit) ui->fileExtensionsLineEdit->clear();
  if (ui->customArtworkTypesLineEdit) ui->customArtworkTypesLineEdit->clear();
  if (ui->collectionTypeComboBox) {
    QSignalBlocker blocker(ui->collectionTypeComboBox);
    ui->collectionTypeComboBox->clear();
    ui->collectionTypeComboBox->setEditText(QString());
  }
  if (ui->backgroundValueEdit) ui->backgroundValueEdit->clear();
  if (ui->primaryColorEdit) ui->primaryColorEdit->clear();
  if (ui->tileColorEdit) ui->tileColorEdit->clear();
  if (ui->selectionColorEdit) ui->selectionColorEdit->clear();
  if (ui->listRowColorEdit) ui->listRowColorEdit->clear();
  if (ui->listAltRowColorEdit) ui->listAltRowColorEdit->clear();
  if (ui->customFontEdit) ui->customFontEdit->clear();
  if (ui->headerLogoEdit) ui->headerLogoEdit->clear();
  if (ui->headerLogoPositionComboBox) ui->headerLogoPositionComboBox->setCurrentIndex(1);
  if (ui->vignetteEnabledCheckBox) ui->vignetteEnabledCheckBox->setChecked(false);
  if (ui->vignetteIntensitySpinBox) ui->vignetteIntensitySpinBox->setValue(60);
  if (ui->wallpaperParallaxCheckBox) ui->wallpaperParallaxCheckBox->setChecked(false);
  if (ui->parallaxStrengthSpinBox) ui->parallaxStrengthSpinBox->setValue(30);
  if (ui->toolbarBackdropBlurCheckBox) ui->toolbarBackdropBlurCheckBox->setChecked(false);
  if (ui->backdropBlurRadiusSpinBox) ui->backdropBlurRadiusSpinBox->setValue(12);

  ui->subfoldersPanel->clear();
  if (ui->showAllSubcollectionItemsCheckBox)
    ui->showAllSubcollectionItemsCheckBox->setChecked(false);
  if (ui->hideTitlesCheckBox) ui->hideTitlesCheckBox->setChecked(false);
  if (ui->hideSubcollectionTitlesCheckBox) ui->hideSubcollectionTitlesCheckBox->setChecked(false);
  if (ui->hideMissingArtworkCheckBox) ui->hideMissingArtworkCheckBox->setChecked(false);

  if (ui->gridWidthSpinBox) ui->gridWidthSpinBox->setValue(UIConstants::Grid::DEFAULT_WIDTH);
  if (ui->horizontalGridHeightSpinBox) ui->horizontalGridHeightSpinBox->setValue(0);
  if (ui->gridWidthSidebarHiddenSpinBox) ui->gridWidthSidebarHiddenSpinBox->setValue(0);
  if (ui->horizontalGridHeightSidebarHiddenSpinBox)
    ui->horizontalGridHeightSidebarHiddenSpinBox->setValue(0);
  if (ui->horizontalSpacingSpinBox)
    ui->horizontalSpacingSpinBox->setValue(spacingInternalToUi(UIConstants::Grid::SPACING));
  if (ui->verticalSpacingSpinBox)
    ui->verticalSpacingSpinBox->setValue(spacingInternalToUi(UIConstants::Grid::SPACING));
  if (ui->itemWidthSpinBox) ui->itemWidthSpinBox->setValue(200);
  if (ui->itemHeightSpinBox) ui->itemHeightSpinBox->setValue(300);
  if (ui->fontSizeSpinBox) ui->fontSizeSpinBox->setValue(12);
  if (ui->cornerRadiusSpinBox) ui->cornerRadiusSpinBox->setValue(0);

  if (ui->horizontalAlignmentComboBox) ui->horizontalAlignmentComboBox->setCurrentIndex(0);
  ui->sidebarPanel->clear();
  if (ui->viewTypeComboBox) ui->viewTypeComboBox->setCurrentIndex(0);
  if (ui->parentCollectionComboBox) ui->parentCollectionComboBox->clear();

  if (ui->backgroundColorRadio) ui->backgroundColorRadio->setChecked(true);

  m_isLoading = false;
}
