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

auto SettingsDialog::extractUIFieldValues() -> CollectionConfig {
  CollectionConfig config;
  if (currentCollectionIndex >= 0 && currentCollectionIndex < m_workingCollections.size()) {
    config = m_workingCollections[currentCollectionIndex];
  }

  if (collectionIndexToItem.contains(currentCollectionIndex) &&
      (collectionIndexToItem[currentCollectionIndex])) {
    QString treeName = collectionIndexToItem[currentCollectionIndex]->text(0);
    if (!treeName.isEmpty()) {
      config.name = treeName;
    }
  }

  config.launcherPath = (ui->launcherLineEdit) ? ui->launcherLineEdit->text() : config.launcherPath;
  config.corePath = (ui->coreLineEdit) ? ui->coreLineEdit->text() : config.corePath;
  config.launchParameters =
      (ui->launchParamsLineEdit) ? ui->launchParamsLineEdit->text() : config.launchParameters;
  config.launcherName =
      (ui->launcherNameLineEdit) ? ui->launcherNameLineEdit->text().trimmed() : config.launcherName;
  config.additionalLaunchers = m_workingAdditionalLaunchers;
  if (ui->defaultLauncherComboBox && ui->defaultLauncherComboBox->count() > 0) {
    config.defaultLauncherIndex = ui->defaultLauncherComboBox->currentIndex();
  }
  config.extractArchives = (ui->extractArchivesCheckBox) ? ui->extractArchivesCheckBox->isChecked()
                                                         : config.extractArchives;
  config.extractedExtension = (ui->extractedExtensionLineEdit)
                                  ? ui->extractedExtensionLineEdit->text()
                                  : config.extractedExtension;
  config.expandMode =
      (ui->expandModeCheckBox) ? ui->expandModeCheckBox->isChecked() : config.expandMode;
  config.mediaDirectory =
      (ui->mediaDirLineEdit) ? ui->mediaDirLineEdit->text() : config.mediaDirectory;
  config.artworkDirectory =
      (ui->artworkDirLineEdit) ? ui->artworkDirLineEdit->text() : config.artworkDirectory;
  config.videoDirectory =
      (ui->videoDirLineEdit) ? ui->videoDirLineEdit->text() : config.videoDirectory;
  config.manualDirectory =
      (ui->manualDirLineEdit) ? ui->manualDirLineEdit->text() : config.manualDirectory;
  config.placeholderArtwork = (ui->placeholderArtworkLineEdit)
                                  ? ui->placeholderArtworkLineEdit->text()
                                  : config.placeholderArtwork;
  config.includeContentSubfolders = (ui->includeContentSubfoldersCheckBox)
                                        ? ui->includeContentSubfoldersCheckBox->isChecked()
                                        : config.includeContentSubfolders;
  config.showAllSubfolderItems = (ui->showAllSubfolderItemsCheckBox)
                                     ? ui->showAllSubfolderItemsCheckBox->isChecked()
                                     : config.showAllSubfolderItems;
  config.hideSubfolderTitles = (ui->hideSubfolderTitlesCheckBox)
                                   ? ui->hideSubfolderTitlesCheckBox->isChecked()
                                   : config.hideSubfolderTitles;
  config.showHiddenFolders = (ui->showHiddenFoldersCheckBox)
                                 ? ui->showHiddenFoldersCheckBox->isChecked()
                                 : config.showHiddenFolders;
  config.includeArtworkSubfolders = (ui->includeArtworkSubfoldersCheckBox)
                                        ? ui->includeArtworkSubfoldersCheckBox->isChecked()
                                        : config.includeArtworkSubfolders;
  config.itemWidth = (ui->itemWidthSpinBox) ? ui->itemWidthSpinBox->value() : config.itemWidth;
  config.itemHeight = (ui->itemHeightSpinBox) ? ui->itemHeightSpinBox->value() : config.itemHeight;
  config.fontSize = (ui->fontSizeSpinBox) ? ui->fontSizeSpinBox->value() : config.fontSize;
  config.cornerRadius =
      (ui->cornerRadiusSpinBox) ? ui->cornerRadiusSpinBox->value() : config.cornerRadius;
  config.extensions =
      (ui->fileExtensionsLineEdit)
          ? ExtensionUtils::parseUserExtensionList(ui->fileExtensionsLineEdit->text())
          : config.extensions;
  if (ui->customArtworkTypesLineEdit) {
    QStringList parsed = ui->customArtworkTypesLineEdit->text().split(',', Qt::SkipEmptyParts);
    QStringList cleaned;
    cleaned.reserve(parsed.size());
    for (QString &type : parsed) {
      type = type.trimmed();
      if (!type.isEmpty() && !cleaned.contains(type)) {
        cleaned.append(type);
      }
    }
    config.customArtworkTypes = cleaned;
  }
  config.gridWidth = (ui->gridWidthSpinBox) ? ui->gridWidthSpinBox->value() : config.gridWidth;
  config.showAllSubcollectionItems = (ui->showAllSubcollectionItemsCheckBox)
                                         ? ui->showAllSubcollectionItemsCheckBox->isChecked()
                                         : config.showAllSubcollectionItems;
  config.horizontalAlignment =
      (ui->horizontalAlignmentComboBox)
          ? static_cast<HorizontalAlignment>(ui->horizontalAlignmentComboBox->currentIndex())
          : config.horizontalAlignment;
  config.sidebarMode = (ui->sidebarModeComboBox)
                           ? static_cast<SidebarMode>(ui->sidebarModeComboBox->currentIndex())
                           : config.sidebarMode;
  config.viewType = (ui->viewTypeComboBox)
                        ? static_cast<ViewType>(ui->viewTypeComboBox->currentIndex())
                        : config.viewType;
  config.horizontalSpacing = (ui->horizontalSpacingSpinBox)
                                 ? spacingUiToInternal(ui->horizontalSpacingSpinBox->value())
                                 : config.horizontalSpacing;
  config.verticalSpacing = (ui->verticalSpacingSpinBox)
                               ? spacingUiToInternal(ui->verticalSpacingSpinBox->value())
                               : config.verticalSpacing;
  config.hideHorizontalScrollbar = (ui->hideHorizontalScrollbarCheckBox)
                                       ? ui->hideHorizontalScrollbarCheckBox->isChecked()
                                       : config.hideHorizontalScrollbar;
  config.hideVerticalScrollbar = (ui->hideVerticalScrollbarCheckBox)
                                     ? ui->hideVerticalScrollbarCheckBox->isChecked()
                                     : config.hideVerticalScrollbar;
  config.hideTitles =
      (ui->hideTitlesCheckBox) ? ui->hideTitlesCheckBox->isChecked() : config.hideTitles;
  config.hideSubcollectionTitles = (ui->hideSubcollectionTitlesCheckBox)
                                       ? ui->hideSubcollectionTitlesCheckBox->isChecked()
                                       : config.hideSubcollectionTitles;

  // Background settings
  if (ui->backgroundImageRadio && ui->backgroundColorRadio) {
    config.backgroundType =
        ui->backgroundImageRadio->isChecked() ? BackgroundType::Image : BackgroundType::Color;
  }
  if (ui->backgroundValueEdit) {
    QString value = ui->backgroundValueEdit->text().trimmed();
    if (config.backgroundType == BackgroundType::Image) {
      config.backgroundImage = value;
      // Clear color when switching to image mode
      config.backgroundColor.clear();
    } else {
      config.backgroundColor = value;
      // Clear image when switching to color mode
      config.backgroundImage.clear();
    }
  }

  // Primary color setting
  config.primaryColor =
      (ui->primaryColorEdit) ? ui->primaryColorEdit->text().trimmed() : config.primaryColor;

  // Tile color setting
  config.tileColor = (ui->tileColorEdit) ? ui->tileColorEdit->text().trimmed() : config.tileColor;

  // Selection color setting
  config.selectionColor =
      (ui->selectionColorEdit) ? ui->selectionColorEdit->text().trimmed() : config.selectionColor;

  // List mode settings
  config.listFontSize =
      (ui->listFontSizeSpinBox) ? ui->listFontSizeSpinBox->value() : config.listFontSize;
  config.listRowHeight =
      (ui->listRowHeightSpinBox) ? ui->listRowHeightSpinBox->value() : config.listRowHeight;
  config.listRowColor =
      (ui->listRowColorEdit) ? ui->listRowColorEdit->text().trimmed() : config.listRowColor;
  config.listAltRowColor = (ui->listAltRowColorEdit) ? ui->listAltRowColorEdit->text().trimmed()
                                                     : config.listAltRowColor;

  // Custom font family (per-collection)
  config.customFontFamily =
      (ui->customFontEdit) ? ui->customFontEdit->text().trimmed() : config.customFontFamily;

  return config;
}

// Updates parent collection settings from UI
auto SettingsDialog::updateParentCollectionFromUI(CollectionConfig &collection, int index) -> void {
  if (ui->parentCollectionComboBox) {
    int dropdownIndex = ui->parentCollectionComboBox->currentIndex();
    if (dropdownIndex >= 0 && dropdownIndex < m_parentCollectionMapping.size()) {
      int newParentIndex = m_parentCollectionMapping[dropdownIndex];
      if (newParentIndex >= 0 && newParentIndex < m_workingCollections.size() &&
          newParentIndex != index) {
        collection.parentCollectionIndex = newParentIndex;
        collection.isSubcollection = true;
      } else {
        collection.parentCollectionIndex = -1;
        collection.isSubcollection = false;
      }
    } else {
      collection.parentCollectionIndex = -1;
      collection.isSubcollection = false;
    }
  }
}

// Checks basic field changes against original configuration
auto SettingsDialog::checkBasicFieldChanges() const -> bool {
  const CollectionConfig &originalConfig = originalCollection;

  // Kartend-bdl: also flag changes when the user has edited the launcher
  // name, the additional-launchers list, or the default-launcher pick.
  const bool launcherNameChanged =
      ui->launcherNameLineEdit &&
      ui->launcherNameLineEdit->text().trimmed() != originalConfig.launcherName;
  const bool additionalChanged = m_workingAdditionalLaunchers != originalConfig.additionalLaunchers;
  const bool defaultLauncherChanged =
      ui->defaultLauncherComboBox && ui->defaultLauncherComboBox->count() > 0 &&
      ui->defaultLauncherComboBox->currentIndex() != originalConfig.defaultLauncherIndex;
  return (
      launcherNameChanged || additionalChanged || defaultLauncherChanged ||
      ((ui->launcherLineEdit) && ui->launcherLineEdit->text() != originalConfig.launcherPath) ||
      ((ui->coreLineEdit) && ui->coreLineEdit->text() != originalConfig.corePath) ||
      ((ui->launchParamsLineEdit) &&
       ui->launchParamsLineEdit->text() != originalConfig.launchParameters) ||
      ((ui->extractArchivesCheckBox) &&
       ui->extractArchivesCheckBox->isChecked() != originalConfig.extractArchives) ||
      ((ui->extractedExtensionLineEdit) &&
       ui->extractedExtensionLineEdit->text() != originalConfig.extractedExtension) ||
      ((ui->expandModeCheckBox) &&
       ui->expandModeCheckBox->isChecked() != originalConfig.expandMode) ||
      ((ui->mediaDirLineEdit) && ui->mediaDirLineEdit->text() != originalConfig.mediaDirectory) ||
      ((ui->artworkDirLineEdit) &&
       ui->artworkDirLineEdit->text() != originalConfig.artworkDirectory) ||
      ((ui->videoDirLineEdit) && ui->videoDirLineEdit->text() != originalConfig.videoDirectory) ||
      ((ui->manualDirLineEdit) &&
       ui->manualDirLineEdit->text() != originalConfig.manualDirectory) ||
      ((ui->placeholderArtworkLineEdit) &&
       ui->placeholderArtworkLineEdit->text() != originalConfig.placeholderArtwork) ||
      ((ui->gridWidthSpinBox) && ui->gridWidthSpinBox->value() != originalConfig.gridWidth) ||
      ((ui->showAllSubcollectionItemsCheckBox) &&
       ui->showAllSubcollectionItemsCheckBox->isChecked() !=
           originalConfig.showAllSubcollectionItems) ||
      ((ui->horizontalAlignmentComboBox) &&
       ui->horizontalAlignmentComboBox->currentIndex() !=
           static_cast<int>(originalConfig.horizontalAlignment)) ||
      ((ui->sidebarModeComboBox) &&
       ui->sidebarModeComboBox->currentIndex() != static_cast<int>(originalConfig.sidebarMode)) ||
      ((ui->viewTypeComboBox) &&
       ui->viewTypeComboBox->currentIndex() != static_cast<int>(originalConfig.viewType)) ||
      ((ui->horizontalSpacingSpinBox) &&
       spacingUiToInternal(ui->horizontalSpacingSpinBox->value()) !=
           originalConfig.horizontalSpacing) ||
      ((ui->verticalSpacingSpinBox) && spacingUiToInternal(ui->verticalSpacingSpinBox->value()) !=
                                           originalConfig.verticalSpacing) ||
      ((ui->hideHorizontalScrollbarCheckBox) && ui->hideHorizontalScrollbarCheckBox->isChecked() !=
                                                    originalConfig.hideHorizontalScrollbar) ||
      ((ui->hideVerticalScrollbarCheckBox) &&
       ui->hideVerticalScrollbarCheckBox->isChecked() != originalConfig.hideVerticalScrollbar) ||
      ((ui->hideTitlesCheckBox) &&
       ui->hideTitlesCheckBox->isChecked() != originalConfig.hideTitles) ||
      ((ui->hideSubcollectionTitlesCheckBox) && ui->hideSubcollectionTitlesCheckBox->isChecked() !=
                                                    originalConfig.hideSubcollectionTitles) ||
      ((ui->includeContentSubfoldersCheckBox) &&
       ui->includeContentSubfoldersCheckBox->isChecked() !=
           originalConfig.includeContentSubfolders) ||
      ((ui->showAllSubfolderItemsCheckBox) &&
       ui->showAllSubfolderItemsCheckBox->isChecked() != originalConfig.showAllSubfolderItems) ||
      ((ui->hideSubfolderTitlesCheckBox) &&
       ui->hideSubfolderTitlesCheckBox->isChecked() != originalConfig.hideSubfolderTitles) ||
      ((ui->showHiddenFoldersCheckBox) &&
       ui->showHiddenFoldersCheckBox->isChecked() != originalConfig.showHiddenFolders) ||
      ((ui->includeArtworkSubfoldersCheckBox) &&
       ui->includeArtworkSubfoldersCheckBox->isChecked() !=
           originalConfig.includeArtworkSubfolders) ||
      ((ui->fontSizeSpinBox) && ui->fontSizeSpinBox->value() != originalConfig.fontSize) ||
      ((ui->cornerRadiusSpinBox) &&
       ui->cornerRadiusSpinBox->value() != originalConfig.cornerRadius));
}

// Checks extension list changes
auto SettingsDialog::checkExtensionChanges() const -> bool {
  QStringList currentExtensions =
      (ui->fileExtensionsLineEdit)
          ? ExtensionUtils::parseUserExtensionList(ui->fileExtensionsLineEdit->text())
          : originalCollection.extensions;
  if (currentExtensions != originalCollection.extensions) {
    return true;
  }
  if (ui->customArtworkTypesLineEdit) {
    QStringList parsed = ui->customArtworkTypesLineEdit->text().split(',', Qt::SkipEmptyParts);
    QStringList cleaned;
    cleaned.reserve(parsed.size());
    for (QString &type : parsed) {
      type = type.trimmed();
      if (!type.isEmpty() && !cleaned.contains(type)) {
        cleaned.append(type);
      }
    }
    if (cleaned != originalCollection.customArtworkTypes) {
      return true;
    }
  }
  return false;
}

// Checks tree name changes
auto SettingsDialog::checkTreeNameChanges() const -> bool {
  QString currentTreeName = originalCollection.name;
  if (collectionIndexToItem.contains(currentCollectionIndex)) {
    QTreeWidgetItem *item = collectionIndexToItem[currentCollectionIndex];
    if (item) {
      currentTreeName = item->text(0);
    }
  }
  return currentTreeName != originalCollection.name;
}

// Checks parent collection changes
auto SettingsDialog::checkParentCollectionChanges() const -> bool {
  int dropdownIndex =
      (ui->parentCollectionComboBox) ? ui->parentCollectionComboBox->currentIndex() : -1;
  int currentParentIndex = -1;
  if (dropdownIndex >= 0 && dropdownIndex < m_parentCollectionMapping.size()) {
    currentParentIndex = m_parentCollectionMapping[dropdownIndex];
  }
  return currentParentIndex != originalCollection.parentCollectionIndex;
}

// Checks dimension changes
auto SettingsDialog::checkDimensionChanges() const -> bool {
  return (ui->itemWidthSpinBox->value() != originalCollection.itemWidth ||
          ui->itemHeightSpinBox->value() != originalCollection.itemHeight ||
          (ui->cornerRadiusSpinBox &&
           ui->cornerRadiusSpinBox->value() != originalCollection.cornerRadius));
}

// Checks color field changes
auto SettingsDialog::checkColorChanges() const -> bool {
  const CollectionConfig &originalConfig = originalCollection;
  return (
      ((ui->primaryColorEdit) &&
       ui->primaryColorEdit->text().trimmed() != originalConfig.primaryColor) ||
      ((ui->tileColorEdit) && ui->tileColorEdit->text().trimmed() != originalConfig.tileColor) ||
      ((ui->selectionColorEdit) &&
       ui->selectionColorEdit->text().trimmed() != originalConfig.selectionColor));
}

// Checks list mode field changes
auto SettingsDialog::checkListModeChanges() const -> bool {
  const CollectionConfig &originalConfig = originalCollection;
  return (((ui->listFontSizeSpinBox) &&
           ui->listFontSizeSpinBox->value() != originalConfig.listFontSize) ||
          ((ui->listRowHeightSpinBox) &&
           ui->listRowHeightSpinBox->value() != originalConfig.listRowHeight) ||
          ((ui->listRowColorEdit) &&
           ui->listRowColorEdit->text().trimmed() != originalConfig.listRowColor) ||
          ((ui->listAltRowColorEdit) &&
           ui->listAltRowColorEdit->text().trimmed() != originalConfig.listAltRowColor) ||
          ((ui->customFontEdit) &&
           ui->customFontEdit->text().trimmed() != originalConfig.customFontFamily));
}

// Checks background field changes
auto SettingsDialog::checkBackgroundChanges() const -> bool {
  const CollectionConfig &originalConfig = originalCollection;
  // Check background type
  if (ui->backgroundImageRadio && ui->backgroundColorRadio) {
    BackgroundType currentType =
        ui->backgroundImageRadio->isChecked() ? BackgroundType::Image : BackgroundType::Color;
    if (currentType != originalConfig.backgroundType) {
      return true;
    }
  }
  // Check background value
  if (ui->backgroundValueEdit) {
    QString currentValue = ui->backgroundValueEdit->text().trimmed();
    // Compare against the appropriate original field based on type
    if (ui->backgroundImageRadio && ui->backgroundImageRadio->isChecked()) {
      if (currentValue != originalConfig.backgroundImage) {
        return true;
      }
    } else {
      if (currentValue != originalConfig.backgroundColor) {
        return true;
      }
    }
  }
  return false;
}

auto SettingsDialog::checkGeneralSettingsChanges() const -> bool {
  // Kartend-p1jd: launcher presets are mutated directly on m_generalSettings
  // (no per-control widgets), so compare the live list against the saved
  // baseline up front. Earlier guard so a preset edit always dirties the
  // dialog even when no other field changed.
  if (m_generalSettings.launcherPresets != m_originalGeneralSettings.launcherPresets) {
    return true;
  }
  if (ui->rememberSelectionCheckBox &&
      ui->rememberSelectionCheckBox->isChecked() != m_originalGeneralSettings.rememberSelection) {
    return true;
  }
  if (ui->wrapNavigationCheckBox &&
      ui->wrapNavigationCheckBox->isChecked() != m_originalGeneralSettings.wrapNavigation) {
    return true;
  }
  if (ui->selectItemOnHoverCheckBox &&
      ui->selectItemOnHoverCheckBox->isChecked() != m_originalGeneralSettings.selectItemOnHover) {
    return true;
  }
  if (ui->bootSplashCheckBox &&
      ui->bootSplashCheckBox->isChecked() != m_originalGeneralSettings.bootSplashEnabled) {
    return true;
  }
  if (ui->resumeFocusSplashCheckBox && ui->resumeFocusSplashCheckBox->isChecked() !=
                                           m_originalGeneralSettings.resumeFocusSplashEnabled) {
    return true;
  }
  if (ui->runtimeDetectionCheckBox && ui->runtimeDetectionCheckBox->isChecked() !=
                                          m_originalGeneralSettings.runtimeDetectionEnabled) {
    return true;
  }
  if (ui->startupCollectionComboBox && ui->startupCollectionComboBox->currentData().toString() !=
                                           m_originalGeneralSettings.startupCollection) {
    return true;
  }
  if (ui->pixmapCacheSpinBox &&
      ui->pixmapCacheSpinBox->value() != m_originalGeneralSettings.pixmapCacheSizeMB) {
    return true;
  }
  if (ui->keyboardSpeedSpinBox &&
      ui->keyboardSpeedSpinBox->value() != m_originalGeneralSettings.keyboardRepeatIntervalMs) {
    return true;
  }
  if (ui->keyboardRepeatDelaySpinBox &&
      ui->keyboardRepeatDelaySpinBox->value() != m_originalGeneralSettings.keyboardRepeatDelayMs) {
    return true;
  }
  if (ui->mouseWheelSpeedSpinBox &&
      ui->mouseWheelSpeedSpinBox->value() != m_originalGeneralSettings.mouseWheelRows) {
    return true;
  }
  if (ui->scrollAnimationSpeedSpinBox && ui->scrollAnimationSpeedSpinBox->value() !=
                                             m_originalGeneralSettings.scrollAnimationDurationMs) {
    return true;
  }
  if (ui->scrollVelocityMultiplierSpinBox &&
      ui->scrollVelocityMultiplierSpinBox->value() !=
          m_originalGeneralSettings.scrollVelocityMultiplier) {
    return true;
  }
  if (ui->clickHoldDelaySpinBox &&
      ui->clickHoldDelaySpinBox->value() != m_originalGeneralSettings.clickHoldDelayMs) {
    return true;
  }
  if (ui->clickHoldRepeatIntervalSpinBox &&
      ui->clickHoldRepeatIntervalSpinBox->value() !=
          m_originalGeneralSettings.clickHoldRepeatIntervalMs) {
    return true;
  }
  if (ui->listKeyboardRepeatSpinBox && ui->listKeyboardRepeatSpinBox->value() !=
                                           m_originalGeneralSettings.listKeyboardRepeatIntervalMs) {
    return true;
  }
  if (ui->listClickHoldRepeatSpinBox &&
      ui->listClickHoldRepeatSpinBox->value() !=
          m_originalGeneralSettings.listClickHoldRepeatIntervalMs) {
    return true;
  }
  // Check text appearance settings (saturation, lightness, base color)
  if (ui->titleSaturationSpinBox &&
      ui->titleSaturationSpinBox->value() != m_originalGeneralSettings.titleTintSaturation) {
    return true;
  }
  if (ui->titleLightnessSpinBox &&
      ui->titleLightnessSpinBox->value() != m_originalGeneralSettings.titleTintLightness) {
    return true;
  }
  if (ui->baseColorEdit &&
      ui->baseColorEdit->text().trimmed() != m_originalGeneralSettings.titleBaseColor) {
    return true;
  }
  // Check attract mode settings
  if (ui->attractModeCheckBox &&
      ui->attractModeCheckBox->isChecked() != m_originalGeneralSettings.attractModeEnabled) {
    return true;
  }
  if (ui->attractIdleTimeoutSpinBox && ui->attractIdleTimeoutSpinBox->value() !=
                                           m_originalGeneralSettings.attractModeIdleTimeoutSec) {
    return true;
  }
  if (ui->attractScrollSpeedSpinBox &&
      ui->attractScrollSpeedSpinBox->value() != m_originalGeneralSettings.attractModeScrollSpeed) {
    return true;
  }
  return false;
}
