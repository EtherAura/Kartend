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

namespace {

// Dirty-check helpers: return true when @p widget exists and its current
// value differs from @p orig. Each helper bakes in the widget's "current
// value" accessor so callers don't have to spell it out. Reduces the
// settingsdialog check*Changes() functions from OR-chains of
// `(ui->X && ui->X->method() != originalConfig.field)` cargo to
// readable per-field checks.

inline bool lineChanged(const QLineEdit *w, const QString &orig) {
  return w && w->text() != orig;
}
inline bool lineTrimmedChanged(const QLineEdit *w, const QString &orig) {
  return w && w->text().trimmed() != orig;
}
inline bool checkboxChanged(const QCheckBox *w, bool orig) {
  return w && w->isChecked() != orig;
}
inline bool spinIntChanged(const QSpinBox *w, int orig) {
  return w && w->value() != orig;
}
template <typename EnumT> bool comboEnumChanged(const QComboBox *w, EnumT orig) {
  return w && w->currentIndex() != static_cast<int>(orig);
}

} // namespace

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
  // read free-form type label from the editable combobox. Use
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
  config.gridWidthSidebarHidden = (ui->gridWidthSidebarHiddenSpinBox)
                                      ? ui->gridWidthSidebarHiddenSpinBox->value()
                                      : config.gridWidthSidebarHidden;
  config.horizontalGridHeightSidebarHidden =
      (ui->horizontalGridHeightSidebarHiddenSpinBox)
          ? ui->horizontalGridHeightSidebarHiddenSpinBox->value()
          : config.horizontalGridHeightSidebarHidden;
  config.showAllSubcollectionItems = (ui->showAllSubcollectionItemsCheckBox)
                                         ? ui->showAllSubcollectionItemsCheckBox->isChecked()
                                         : config.showAllSubcollectionItems;
  config.horizontalAlignment =
      (ui->horizontalAlignmentComboBox)
          ? static_cast<HorizontalAlignment>(ui->horizontalAlignmentComboBox->currentIndex())
          : config.horizontalAlignment;
  config.sidebarMode = (ui->sidebarModeComboBox)
                           ? static_cast<DetailsPaneMode>(ui->sidebarModeComboBox->currentIndex())
                           : config.sidebarMode;
  // sidebar enhancements.
  if (ui->sidebarPositionComboBox) {
    config.sidebarPosition =
        static_cast<DetailsPanePosition>(ui->sidebarPositionComboBox->currentIndex());
  }
  if (ui->sidebarWidthSpinBox) {
    config.sidebarWidth = ui->sidebarWidthSpinBox->value();
  }
  if (ui->sidebarHeightSpinBox) {
    config.sidebarHeight = ui->sidebarHeightSpinBox->value();
  }
  if (ui->sidebarWidthLockedCheckBox) {
    config.sidebarWidthLocked = ui->sidebarWidthLockedCheckBox->isChecked();
  }
  if (ui->sidebarBackgroundTypeComboBox) {
    config.sidebarBackgroundType =
        static_cast<DetailsPaneBackgroundType>(ui->sidebarBackgroundTypeComboBox->currentIndex());
  }
  if (ui->sidebarBackgroundValueEdit) {
    const QString value = ui->sidebarBackgroundValueEdit->text().trimmed();
    if (config.sidebarBackgroundType == DetailsPaneBackgroundType::Image) {
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
  // per-collection sidebar font override.
  if (ui->sidebarFontFamilyEdit) {
    config.sidebarFontFamily = ui->sidebarFontFamilyEdit->text().trimmed();
  }
  if (ui->sidebarFontSizeSpinBox) {
    config.sidebarFontPointSize = ui->sidebarFontSizeSpinBox->value();
  }
  if (ui->sidebarActiveTabComboBox) {
    config.sidebarActiveTab =
        static_cast<DetailsPaneTab>(ui->sidebarActiveTabComboBox->currentIndex());
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

  // header logo
  if (ui->headerLogoEdit) {
    config.headerLogoImage = ui->headerLogoEdit->text().trimmed();
  }
  if (ui->headerLogoPositionComboBox) {
    config.headerLogoPosition =
        static_cast<HeaderLogoPosition>(ui->headerLogoPositionComboBox->currentIndex());
  }

  // vignette
  if (ui->vignetteEnabledCheckBox) {
    config.vignetteEnabled = ui->vignetteEnabledCheckBox->isChecked();
  }
  if (ui->vignetteIntensitySpinBox) {
    config.vignetteIntensity = ui->vignetteIntensitySpinBox->value();
  }

  // wallpaper parallax
  if (ui->wallpaperParallaxCheckBox) {
    config.wallpaperParallax = ui->wallpaperParallaxCheckBox->isChecked();
  }
  if (ui->parallaxStrengthSpinBox) {
    config.parallaxStrength = ui->parallaxStrengthSpinBox->value();
  }

  // toolbar backdrop blur
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
  const CollectionConfig &o = originalCollection;

  // Special cases: launcher dirty-check, additional-launchers list, default
  // launcher index (only valid when combobox has entries), the
  // collection-type combobox (compares trimmed currentText, not index), the
  // sidebar background value (compares against image OR color depending on
  // the type), and the spacing fields (apply spacingUiToInternal first).
  const bool launcherNameChanged =
      ui->launcherNameLineEdit && ui->launcherNameLineEdit->text().trimmed() != o.launcherName;
  const bool additionalChanged = m_workingAdditionalLaunchers != o.additionalLaunchers;
  const bool defaultLauncherChanged =
      ui->defaultLauncherComboBox && ui->defaultLauncherComboBox->count() > 0 &&
      ui->defaultLauncherComboBox->currentIndex() != o.defaultLauncherIndex;
  const bool typeChanged =
      ui->collectionTypeComboBox && ui->collectionTypeComboBox->currentText().trimmed() != o.type;
  const QString sidebarBgOrig = o.sidebarBackgroundType == DetailsPaneBackgroundType::Image
                                    ? o.sidebarBackgroundImage
                                    : o.sidebarBackgroundColor;
  const bool sidebarBgValueChanged =
      lineTrimmedChanged(ui->sidebarBackgroundValueEdit, sidebarBgOrig);
  const bool hSpacingChanged =
      ui->horizontalSpacingSpinBox &&
      spacingUiToInternal(ui->horizontalSpacingSpinBox->value()) != o.horizontalSpacing;
  const bool vSpacingChanged =
      ui->verticalSpacingSpinBox &&
      spacingUiToInternal(ui->verticalSpacingSpinBox->value()) != o.verticalSpacing;

  return launcherNameChanged || additionalChanged || defaultLauncherChanged || typeChanged ||
         sidebarBgValueChanged || hSpacingChanged || vSpacingChanged ||

         // Launcher / archive paths
         lineChanged(ui->launcherLineEdit, o.launcherPath) ||
         lineChanged(ui->coreLineEdit, o.corePath) ||
         lineChanged(ui->launchParamsLineEdit, o.launchParameters) ||
         checkboxChanged(ui->extractArchivesCheckBox, o.extractArchives) ||
         lineChanged(ui->extractedExtensionLineEdit, o.extractedExtension) ||
         checkboxChanged(ui->expandModeCheckBox, o.expandMode) ||

         // Directories
         lineChanged(ui->mediaDirLineEdit, o.mediaDirectory) ||
         lineChanged(ui->artworkDirLineEdit, o.artworkDirectory) ||
         lineChanged(ui->videoDirLineEdit, o.videoDirectory) ||
         lineChanged(ui->manualDirLineEdit, o.manualDirectory) ||
         lineChanged(ui->placeholderArtworkLineEdit, o.placeholderArtwork) ||

         // Grid metrics
         spinIntChanged(ui->gridWidthSpinBox, o.gridWidth) ||
         spinIntChanged(ui->horizontalGridHeightSpinBox, o.horizontalGridHeight) ||
         spinIntChanged(ui->gridWidthSidebarHiddenSpinBox, o.gridWidthSidebarHidden) ||
         spinIntChanged(ui->horizontalGridHeightSidebarHiddenSpinBox,
                        o.horizontalGridHeightSidebarHidden) ||
         checkboxChanged(ui->showAllSubcollectionItemsCheckBox, o.showAllSubcollectionItems) ||
         comboEnumChanged(ui->horizontalAlignmentComboBox, o.horizontalAlignment) ||

         // Sidebar geometry + background
         comboEnumChanged(ui->sidebarModeComboBox, o.sidebarMode) ||
         comboEnumChanged(ui->sidebarPositionComboBox, o.sidebarPosition) ||
         spinIntChanged(ui->sidebarWidthSpinBox, o.sidebarWidth) ||
         spinIntChanged(ui->sidebarHeightSpinBox, o.sidebarHeight) ||
         checkboxChanged(ui->sidebarWidthLockedCheckBox, o.sidebarWidthLocked) ||
         comboEnumChanged(ui->sidebarBackgroundTypeComboBox, o.sidebarBackgroundType) ||
         lineTrimmedChanged(ui->sidebarPatternColorEdit, o.sidebarPatternColor) ||
         spinIntChanged(ui->sidebarPatternIntensitySpinBox, o.sidebarPatternIntensity) ||
         lineTrimmedChanged(ui->sidebarTextColorEdit, o.sidebarTextColor) ||
         lineTrimmedChanged(ui->sidebarAccentColorEdit, o.sidebarAccentColor) ||
         lineTrimmedChanged(ui->sidebarHeaderBgEdit, o.sidebarHeaderBgColor) ||
         lineTrimmedChanged(ui->sidebarSectionBgEdit, o.sidebarSectionBgColor) ||
         spinIntChanged(ui->sidebarHeaderBgOpacitySpinBox, o.sidebarHeaderBgOpacity) ||
         spinIntChanged(ui->sidebarSectionBgOpacitySpinBox, o.sidebarSectionBgOpacity) ||
         lineTrimmedChanged(ui->sidebarFontFamilyEdit, o.sidebarFontFamily) ||
         spinIntChanged(ui->sidebarFontSizeSpinBox, o.sidebarFontPointSize) ||
         comboEnumChanged(ui->sidebarActiveTabComboBox, o.sidebarActiveTab) ||

         // View / scrollbars / titles / folders / typography
         comboEnumChanged(ui->viewTypeComboBox, o.viewType) ||
         checkboxChanged(ui->hideMissingArtworkCheckBox, o.hideMissingArtwork) ||
         checkboxChanged(ui->hideHorizontalScrollbarCheckBox, o.hideHorizontalScrollbar) ||
         checkboxChanged(ui->hideVerticalScrollbarCheckBox, o.hideVerticalScrollbar) ||
         checkboxChanged(ui->hideTitlesCheckBox, o.hideTitles) ||
         checkboxChanged(ui->hideSubcollectionTitlesCheckBox, o.hideSubcollectionTitles) ||
         checkboxChanged(ui->includeContentSubfoldersCheckBox, o.includeContentSubfolders) ||
         checkboxChanged(ui->showAllSubfolderItemsCheckBox, o.showAllSubfolderItems) ||
         checkboxChanged(ui->hideSubfolderTitlesCheckBox, o.hideSubfolderTitles) ||
         checkboxChanged(ui->showHiddenFoldersCheckBox, o.showHiddenFolders) ||
         checkboxChanged(ui->includeArtworkSubfoldersCheckBox, o.includeArtworkSubfolders) ||
         spinIntChanged(ui->fontSizeSpinBox, o.fontSize) ||
         spinIntChanged(ui->cornerRadiusSpinBox, o.cornerRadius);
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
  const CollectionConfig &o = originalCollection;
  // / qbp3 / y25g / eq8r: theme-adjacent fields (logo, vignette,
  // parallax, blur) are tracked here so hasUnsavedChanges() picks them up.
  return lineTrimmedChanged(ui->primaryColorEdit, o.primaryColor) ||
         lineTrimmedChanged(ui->tileColorEdit, o.tileColor) ||
         lineTrimmedChanged(ui->selectionColorEdit, o.selectionColor) ||
         lineTrimmedChanged(ui->headerLogoEdit, o.headerLogoImage) ||
         comboEnumChanged(ui->headerLogoPositionComboBox, o.headerLogoPosition) ||
         checkboxChanged(ui->vignetteEnabledCheckBox, o.vignetteEnabled) ||
         spinIntChanged(ui->vignetteIntensitySpinBox, o.vignetteIntensity) ||
         checkboxChanged(ui->wallpaperParallaxCheckBox, o.wallpaperParallax) ||
         spinIntChanged(ui->parallaxStrengthSpinBox, o.parallaxStrength) ||
         checkboxChanged(ui->toolbarBackdropBlurCheckBox, o.toolbarBackdropBlur) ||
         spinIntChanged(ui->backdropBlurRadiusSpinBox, o.backdropBlurRadius);
}

// Checks list mode field changes
auto SettingsDialog::checkListModeChanges() const -> bool {
  const CollectionConfig &o = originalCollection;
  return spinIntChanged(ui->listFontSizeSpinBox, o.listFontSize) ||
         spinIntChanged(ui->listRowHeightSpinBox, o.listRowHeight) ||
         lineTrimmedChanged(ui->listRowColorEdit, o.listRowColor) ||
         lineTrimmedChanged(ui->listAltRowColorEdit, o.listAltRowColor) ||
         lineTrimmedChanged(ui->customFontEdit, o.customFontFamily);
}

// Checks background field changes
auto SettingsDialog::checkBackgroundChanges() const -> bool {
  const CollectionConfig &originalConfig = originalCollection;
  // Check background type — must include the Video radio so
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
  // launcher presets are mutated directly on m_generalSettings
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
  // +: startup video fields participate in the
  // dirty-check comparison so the save button reflects unsaved edits.
  if (ui->startupVideoEnabledCheckBox && ui->startupVideoEnabledCheckBox->isChecked() !=
                                             m_originalGeneralSettings.startupVideoEnabled) {
    return true;
  }
  if (ui->startupVideoPathLineEdit && ui->startupVideoPathLineEdit->text().trimmed() !=
                                          m_originalGeneralSettings.startupVideoPath.trimmed()) {
    return true;
  }
  if (ui->resumeFocusSplashCheckBox && ui->resumeFocusSplashCheckBox->isChecked() !=
                                           m_originalGeneralSettings.resumeFocusSplashEnabled) {
    return true;
  }
  if (ui->bootSplashTitleLineEdit && ui->bootSplashTitleLineEdit->text().trimmed() !=
                                         m_originalGeneralSettings.bootSplashTitle.trimmed()) {
    return true;
  }
  if (ui->bootSplashSubtitleLineEdit &&
      ui->bootSplashSubtitleLineEdit->text().trimmed() !=
          m_originalGeneralSettings.bootSplashSubtitle.trimmed()) {
    return true;
  }
  if (ui->resumeFocusSplashTitleLineEdit &&
      ui->resumeFocusSplashTitleLineEdit->text().trimmed() !=
          m_originalGeneralSettings.resumeFocusSplashTitle.trimmed()) {
    return true;
  }
  if (ui->resumeFocusSplashSubtitleLineEdit &&
      ui->resumeFocusSplashSubtitleLineEdit->text().trimmed() !=
          m_originalGeneralSettings.resumeFocusSplashSubtitle.trimmed()) {
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
  // customizable toolbar fields.
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
  // artwork-cycle modifier dropdown.
  if (ui->artworkCycleModifierComboBox && ui->artworkCycleModifierComboBox->currentData().toInt() !=
                                              m_originalGeneralSettings.artworkCycleModifier) {
    return true;
  }
  if (ui->useHomeViewCheckBox &&
      ui->useHomeViewCheckBox->isChecked() != m_originalGeneralSettings.useHomeView) {
    return true;
  }
  if (ui->homeViewLabelLineEdit && ui->homeViewLabelLineEdit->text().trimmed() !=
                                       m_originalGeneralSettings.homeViewLabel.trimmed()) {
    return true;
  }
  if (ui->homeViewIconLineEdit && ui->homeViewIconLineEdit->text().trimmed() !=
                                      m_originalGeneralSettings.homeViewIcon.trimmed()) {
    return true;
  }
  return false;
}
