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
  config.additionalParentNames = m_workingAdditionalParentNames;
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
  // Kartend-dd8: read free-form type label from the editable combobox. Use
  // currentText() rather than currentIndex() so a freshly typed value (not
  // yet committed via Enter) round-trips, and trim whitespace so accidental
  // padding doesn't fragment the type set.
  config.type = (ui->collectionTypeComboBox) ? ui->collectionTypeComboBox->currentText().trimmed()
                                             : config.type;
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
  config.horizontalGridHeight = (ui->horizontalGridHeightSpinBox)
                                    ? ui->horizontalGridHeightSpinBox->value()
                                    : config.horizontalGridHeight;
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
  // Kartend-63e sidebar enhancements.
  if (ui->sidebarPositionComboBox) {
    config.sidebarPosition =
        static_cast<SidebarPosition>(ui->sidebarPositionComboBox->currentIndex());
  }
  if (ui->sidebarWidthSpinBox) {
    config.sidebarWidth = ui->sidebarWidthSpinBox->value();
  }
  if (ui->sidebarWidthLockedCheckBox) {
    config.sidebarWidthLocked = ui->sidebarWidthLockedCheckBox->isChecked();
  }
  if (ui->sidebarBackgroundTypeComboBox) {
    config.sidebarBackgroundType =
        static_cast<SidebarBackgroundType>(ui->sidebarBackgroundTypeComboBox->currentIndex());
  }
  if (ui->sidebarBackgroundValueEdit) {
    const QString value = ui->sidebarBackgroundValueEdit->text().trimmed();
    if (config.sidebarBackgroundType == SidebarBackgroundType::Image) {
      config.sidebarBackgroundImage = value;
      config.sidebarBackgroundColor.clear();
    } else {
      // Pattern mode also stores the bg base color in sidebarBackgroundColor;
      // the value field doubles for that. The pattern stroke color comes
      // from sidebarPatternColorEdit below.
      config.sidebarBackgroundColor = value;
      config.sidebarBackgroundImage.clear();
    }
  }
  if (ui->sidebarPatternIntensitySpinBox) {
    config.sidebarPatternIntensity = ui->sidebarPatternIntensitySpinBox->value();
  }
  if (ui->sidebarPatternColorEdit) {
    config.sidebarPatternColor = ui->sidebarPatternColorEdit->text().trimmed();
  }
  if (ui->sidebarTextColorEdit) {
    config.sidebarTextColor = ui->sidebarTextColorEdit->text().trimmed();
  }
  if (ui->sidebarAccentColorEdit) {
    config.sidebarAccentColor = ui->sidebarAccentColorEdit->text().trimmed();
  }
  if (ui->sidebarHeaderBgEdit) {
    config.sidebarHeaderBgColor = ui->sidebarHeaderBgEdit->text().trimmed();
  }
  if (ui->sidebarSectionBgEdit) {
    config.sidebarSectionBgColor = ui->sidebarSectionBgEdit->text().trimmed();
  }
  if (ui->sidebarHeaderBgOpacitySpinBox) {
    config.sidebarHeaderBgOpacity = ui->sidebarHeaderBgOpacitySpinBox->value();
  }
  if (ui->sidebarSectionBgOpacitySpinBox) {
    config.sidebarSectionBgOpacity = ui->sidebarSectionBgOpacitySpinBox->value();
  }
  // Kartend-ekaa: per-collection sidebar font override.
  if (ui->sidebarFontFamilyEdit) {
    config.sidebarFontFamily = ui->sidebarFontFamilyEdit->text().trimmed();
  }
  if (ui->sidebarFontSizeSpinBox) {
    config.sidebarFontPointSize = ui->sidebarFontSizeSpinBox->value();
  }
  if (ui->sidebarActiveTabComboBox) {
    config.sidebarActiveTab = static_cast<SidebarTab>(ui->sidebarActiveTabComboBox->currentIndex());
  }
  config.viewType = (ui->viewTypeComboBox)
                        ? static_cast<ViewType>(ui->viewTypeComboBox->currentIndex())
                        : config.viewType;
  config.hideMissingArtwork = (ui->hideMissingArtworkCheckBox)
                                  ? ui->hideMissingArtworkCheckBox->isChecked()
                                  : config.hideMissingArtwork;
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
    if (ui->backgroundVideoRadio && ui->backgroundVideoRadio->isChecked()) {
      config.backgroundType = BackgroundType::Video;
    } else if (ui->backgroundImageRadio->isChecked()) {
      config.backgroundType = BackgroundType::Image;
    } else {
      config.backgroundType = BackgroundType::Color;
    }
  }
  if (ui->backgroundValueEdit) {
    QString value = ui->backgroundValueEdit->text().trimmed();
    // Only the field matching the active type holds a value; the other two
    // are cleared so an old value can't bleed back when the user toggles.
    if (config.backgroundType == BackgroundType::Video) {
      config.backgroundVideo = value;
      config.backgroundColor.clear();
      config.backgroundImage.clear();
    } else if (config.backgroundType == BackgroundType::Image) {
      config.backgroundImage = value;
      config.backgroundColor.clear();
      config.backgroundVideo.clear();
    } else {
      config.backgroundColor = value;
      config.backgroundImage.clear();
      config.backgroundVideo.clear();
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

  // Kartend-guo5: header logo
  if (ui->headerLogoEdit) {
    config.headerLogoImage = ui->headerLogoEdit->text().trimmed();
  }
  if (ui->headerLogoPositionComboBox) {
    config.headerLogoPosition =
        static_cast<HeaderLogoPosition>(ui->headerLogoPositionComboBox->currentIndex());
  }

  // Kartend-qbp3: vignette
  if (ui->vignetteEnabledCheckBox) {
    config.vignetteEnabled = ui->vignetteEnabledCheckBox->isChecked();
  }
  if (ui->vignetteIntensitySpinBox) {
    config.vignetteIntensity = ui->vignetteIntensitySpinBox->value();
  }

  // Kartend-y25g: wallpaper parallax
  if (ui->wallpaperParallaxCheckBox) {
    config.wallpaperParallax = ui->wallpaperParallaxCheckBox->isChecked();
  }
  if (ui->parallaxStrengthSpinBox) {
    config.parallaxStrength = ui->parallaxStrengthSpinBox->value();
  }

  // Kartend-eq8r: toolbar backdrop blur
  if (ui->toolbarBackdropBlurCheckBox) {
    config.toolbarBackdropBlur = ui->toolbarBackdropBlurCheckBox->isChecked();
  }
  if (ui->backdropBlurRadiusSpinBox) {
    config.backdropBlurRadius = ui->backdropBlurRadiusSpinBox->value();
  }

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
      ((ui->collectionTypeComboBox) &&
       ui->collectionTypeComboBox->currentText().trimmed() != originalConfig.type) ||
      ((ui->mediaDirLineEdit) && ui->mediaDirLineEdit->text() != originalConfig.mediaDirectory) ||
      ((ui->artworkDirLineEdit) &&
       ui->artworkDirLineEdit->text() != originalConfig.artworkDirectory) ||
      ((ui->videoDirLineEdit) && ui->videoDirLineEdit->text() != originalConfig.videoDirectory) ||
      ((ui->manualDirLineEdit) &&
       ui->manualDirLineEdit->text() != originalConfig.manualDirectory) ||
      ((ui->placeholderArtworkLineEdit) &&
       ui->placeholderArtworkLineEdit->text() != originalConfig.placeholderArtwork) ||
      ((ui->gridWidthSpinBox) && ui->gridWidthSpinBox->value() != originalConfig.gridWidth) ||
      ((ui->horizontalGridHeightSpinBox) &&
       ui->horizontalGridHeightSpinBox->value() != originalConfig.horizontalGridHeight) ||
      ((ui->showAllSubcollectionItemsCheckBox) &&
       ui->showAllSubcollectionItemsCheckBox->isChecked() !=
           originalConfig.showAllSubcollectionItems) ||
      ((ui->horizontalAlignmentComboBox) &&
       ui->horizontalAlignmentComboBox->currentIndex() !=
           static_cast<int>(originalConfig.horizontalAlignment)) ||
      ((ui->sidebarModeComboBox) &&
       ui->sidebarModeComboBox->currentIndex() != static_cast<int>(originalConfig.sidebarMode)) ||
      // Kartend-63e: dirty checks for the new sidebar fields.
      ((ui->sidebarPositionComboBox) && ui->sidebarPositionComboBox->currentIndex() !=
                                            static_cast<int>(originalConfig.sidebarPosition)) ||
      ((ui->sidebarWidthSpinBox) &&
       ui->sidebarWidthSpinBox->value() != originalConfig.sidebarWidth) ||
      ((ui->sidebarWidthLockedCheckBox) &&
       ui->sidebarWidthLockedCheckBox->isChecked() != originalConfig.sidebarWidthLocked) ||
      ((ui->sidebarBackgroundTypeComboBox) &&
       ui->sidebarBackgroundTypeComboBox->currentIndex() !=
           static_cast<int>(originalConfig.sidebarBackgroundType)) ||
      ((ui->sidebarBackgroundValueEdit) &&
       ui->sidebarBackgroundValueEdit->text().trimmed() !=
           (originalConfig.sidebarBackgroundType == SidebarBackgroundType::Image
                ? originalConfig.sidebarBackgroundImage
                : originalConfig.sidebarBackgroundColor)) ||
      ((ui->sidebarPatternColorEdit) &&
       ui->sidebarPatternColorEdit->text().trimmed() != originalConfig.sidebarPatternColor) ||
      ((ui->sidebarPatternIntensitySpinBox) &&
       ui->sidebarPatternIntensitySpinBox->value() != originalConfig.sidebarPatternIntensity) ||
      ((ui->sidebarTextColorEdit) &&
       ui->sidebarTextColorEdit->text().trimmed() != originalConfig.sidebarTextColor) ||
      ((ui->sidebarAccentColorEdit) &&
       ui->sidebarAccentColorEdit->text().trimmed() != originalConfig.sidebarAccentColor) ||
      ((ui->sidebarHeaderBgEdit) &&
       ui->sidebarHeaderBgEdit->text().trimmed() != originalConfig.sidebarHeaderBgColor) ||
      ((ui->sidebarSectionBgEdit) &&
       ui->sidebarSectionBgEdit->text().trimmed() != originalConfig.sidebarSectionBgColor) ||
      ((ui->sidebarHeaderBgOpacitySpinBox) &&
       ui->sidebarHeaderBgOpacitySpinBox->value() != originalConfig.sidebarHeaderBgOpacity) ||
      ((ui->sidebarSectionBgOpacitySpinBox) &&
       ui->sidebarSectionBgOpacitySpinBox->value() != originalConfig.sidebarSectionBgOpacity) ||
      ((ui->sidebarFontFamilyEdit) &&
       ui->sidebarFontFamilyEdit->text().trimmed() != originalConfig.sidebarFontFamily) ||
      ((ui->sidebarFontSizeSpinBox) &&
       ui->sidebarFontSizeSpinBox->value() != originalConfig.sidebarFontPointSize) ||
      ((ui->sidebarActiveTabComboBox) && ui->sidebarActiveTabComboBox->currentIndex() !=
                                             static_cast<int>(originalConfig.sidebarActiveTab)) ||
      ((ui->viewTypeComboBox) &&
       ui->viewTypeComboBox->currentIndex() != static_cast<int>(originalConfig.viewType)) ||
      ((ui->hideMissingArtworkCheckBox) &&
       ui->hideMissingArtworkCheckBox->isChecked() != originalConfig.hideMissingArtwork) ||
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
  // Kartend-guo5 / qbp3: header logo + vignette are theme-adjacent fields
  // tracked here so hasUnsavedChanges() picks them up. Without this, typing
  // a path or toggling vignette never enables the Save button and the OK
  // path silently discards the edit.
  const bool logoChanged =
      (ui->headerLogoEdit &&
       ui->headerLogoEdit->text().trimmed() != originalConfig.headerLogoImage) ||
      (ui->headerLogoPositionComboBox &&
       static_cast<HeaderLogoPosition>(ui->headerLogoPositionComboBox->currentIndex()) !=
           originalConfig.headerLogoPosition);
  const bool vignetteChanged = (ui->vignetteEnabledCheckBox &&
                                ui->vignetteEnabledCheckBox->isChecked() !=
                                    originalConfig.vignetteEnabled) ||
                               (ui->vignetteIntensitySpinBox &&
                                ui->vignetteIntensitySpinBox->value() !=
                                    originalConfig.vignetteIntensity);
  // Kartend-y25g / eq8r: parallax + blur ride along under the same dirty
  // umbrella so typing into them enables the Save button.
  const bool parallaxChanged = (ui->wallpaperParallaxCheckBox &&
                                ui->wallpaperParallaxCheckBox->isChecked() !=
                                    originalConfig.wallpaperParallax) ||
                               (ui->parallaxStrengthSpinBox &&
                                ui->parallaxStrengthSpinBox->value() !=
                                    originalConfig.parallaxStrength);
  const bool blurChanged = (ui->toolbarBackdropBlurCheckBox &&
                            ui->toolbarBackdropBlurCheckBox->isChecked() !=
                                originalConfig.toolbarBackdropBlur) ||
                           (ui->backdropBlurRadiusSpinBox &&
                            ui->backdropBlurRadiusSpinBox->value() !=
                                originalConfig.backdropBlurRadius);
  return (
      ((ui->primaryColorEdit) &&
       ui->primaryColorEdit->text().trimmed() != originalConfig.primaryColor) ||
      ((ui->tileColorEdit) && ui->tileColorEdit->text().trimmed() != originalConfig.tileColor) ||
      ((ui->selectionColorEdit) &&
       ui->selectionColorEdit->text().trimmed() != originalConfig.selectionColor) ||
      logoChanged || vignetteChanged || parallaxChanged || blurChanged);
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
  // Check background type — must include the Video radio (Kartend-vbs) so
  // toggling between Color/Image/Video correctly dirties the dialog.
  if (ui->backgroundImageRadio && ui->backgroundColorRadio) {
    BackgroundType currentType = BackgroundType::Color;
    if (ui->backgroundVideoRadio && ui->backgroundVideoRadio->isChecked()) {
      currentType = BackgroundType::Video;
    } else if (ui->backgroundImageRadio->isChecked()) {
      currentType = BackgroundType::Image;
    }
    if (currentType != originalConfig.backgroundType) {
      return true;
    }
  }
  // Check background value against the field matching the active type.
  if (ui->backgroundValueEdit) {
    QString currentValue = ui->backgroundValueEdit->text().trimmed();
    if (ui->backgroundVideoRadio && ui->backgroundVideoRadio->isChecked()) {
      if (currentValue != originalConfig.backgroundVideo) {
        return true;
      }
    } else if (ui->backgroundImageRadio && ui->backgroundImageRadio->isChecked()) {
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
  // Kartend-y3ke + Kartend-wcow: startup video fields participate in the
  // dirty-check comparison so the save button reflects unsaved edits.
  if (ui->startupVideoEnabledCheckBox &&
      ui->startupVideoEnabledCheckBox->isChecked() !=
          m_originalGeneralSettings.startupVideoEnabled) {
    return true;
  }
  if (ui->startupVideoPathLineEdit &&
      ui->startupVideoPathLineEdit->text().trimmed() !=
          m_originalGeneralSettings.startupVideoPath.trimmed()) {
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
  if (ui->historyEnabledCheckBox &&
      ui->historyEnabledCheckBox->isChecked() != m_originalGeneralSettings.historyEnabled) {
    return true;
  }
  if (ui->historyMaxEntriesSpinBox &&
      ui->historyMaxEntriesSpinBox->value() != m_originalGeneralSettings.historyMaxEntries) {
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
  if (ui->attractAutoScrollCheckBox && ui->attractAutoScrollCheckBox->isChecked() !=
                                           m_originalGeneralSettings.attractModeAutoScrollEnabled) {
    return true;
  }
  if (ui->attractScrollSpeedSpinBox &&
      ui->attractScrollSpeedSpinBox->value() != m_originalGeneralSettings.attractModeScrollSpeed) {
    return true;
  }
  if (ui->attractAdvanceSelectionCheckBox &&
      ui->attractAdvanceSelectionCheckBox->isChecked() !=
          m_originalGeneralSettings.attractModeAdvanceSelectionEnabled) {
    return true;
  }
  if (ui->attractAdvanceIntervalSpinBox &&
      ui->attractAdvanceIntervalSpinBox->value() !=
          m_originalGeneralSettings.attractModeAdvanceSelectionIntervalSec) {
    return true;
  }
  if (ui->attractAdvanceRandomCheckBox &&
      ui->attractAdvanceRandomCheckBox->isChecked() !=
          m_originalGeneralSettings.attractModeAdvanceSelectionRandom) {
    return true;
  }
  // Kartend-81o: customizable toolbar fields.
  if (ui->toolbarGridViewVisibleCheckBox &&
      ui->toolbarGridViewVisibleCheckBox->isChecked() !=
          m_originalGeneralSettings.toolbarShowGridViewButton) {
    return true;
  }
  if (ui->toolbarListViewVisibleCheckBox &&
      ui->toolbarListViewVisibleCheckBox->isChecked() !=
          m_originalGeneralSettings.toolbarShowListViewButton) {
    return true;
  }
  if (ui->toolbarCoverFlowViewVisibleCheckBox &&
      ui->toolbarCoverFlowViewVisibleCheckBox->isChecked() !=
          m_originalGeneralSettings.toolbarShowCoverFlowViewButton) {
    return true;
  }
  if (ui->toolbarHorizontalViewVisibleCheckBox &&
      ui->toolbarHorizontalViewVisibleCheckBox->isChecked() !=
          m_originalGeneralSettings.toolbarShowHorizontalViewButton) {
    return true;
  }
  if (ui->toolbarHideSubcollectionsVisibleCheckBox &&
      ui->toolbarHideSubcollectionsVisibleCheckBox->isChecked() !=
          m_originalGeneralSettings.toolbarShowHideSubcollectionsButton) {
    return true;
  }
  if (ui->toolbarTypeFilterVisibleCheckBox && ui->toolbarTypeFilterVisibleCheckBox->isChecked() !=
                                                  m_originalGeneralSettings.toolbarShowTypeFilter) {
    return true;
  }
  if (ui->toolbarTitleFilterVisibleCheckBox &&
      ui->toolbarTitleFilterVisibleCheckBox->isChecked() !=
          m_originalGeneralSettings.toolbarShowTitleFilter) {
    return true;
  }
  if (ui->toolbarSearchModeVisibleCheckBox &&
      ui->toolbarSearchModeVisibleCheckBox->isChecked() !=
          m_originalGeneralSettings.toolbarShowSearchModeButton) {
    return true;
  }
  if (ui->toolbarSearchBarVisibleCheckBox && ui->toolbarSearchBarVisibleCheckBox->isChecked() !=
                                                 m_originalGeneralSettings.toolbarShowSearchBar) {
    return true;
  }
  if (ui->toolbarGridViewTextEdit &&
      ui->toolbarGridViewTextEdit->text() != m_originalGeneralSettings.toolbarGridViewButtonText) {
    return true;
  }
  if (ui->toolbarListViewTextEdit &&
      ui->toolbarListViewTextEdit->text() != m_originalGeneralSettings.toolbarListViewButtonText) {
    return true;
  }
  if (ui->toolbarCoverFlowViewTextEdit &&
      ui->toolbarCoverFlowViewTextEdit->text() !=
          m_originalGeneralSettings.toolbarCoverFlowViewButtonText) {
    return true;
  }
  if (ui->toolbarHorizontalViewTextEdit &&
      ui->toolbarHorizontalViewTextEdit->text() !=
          m_originalGeneralSettings.toolbarHorizontalViewButtonText) {
    return true;
  }
  if (ui->toolbarHideSubcollectionsTextEdit &&
      ui->toolbarHideSubcollectionsTextEdit->text() !=
          m_originalGeneralSettings.toolbarHideSubcollectionsButtonText) {
    return true;
  }
  if (ui->toolbarTitleFilterTextEdit &&
      ui->toolbarTitleFilterTextEdit->text() != m_originalGeneralSettings.toolbarTitleFilterText) {
    return true;
  }
  // Kartend-1v6: artwork-cycle modifier dropdown.
  if (ui->artworkCycleModifierComboBox && ui->artworkCycleModifierComboBox->currentData().toInt() !=
                                              m_originalGeneralSettings.artworkCycleModifier) {
    return true;
  }
  return false;
}
