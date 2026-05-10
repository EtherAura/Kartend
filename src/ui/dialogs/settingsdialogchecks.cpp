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
  ui->sidebarPanel->save(config);
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
  // collection-type combobox (compares trimmed currentText, not index), and
  // the spacing fields (apply spacingUiToInternal first). Sidebar / details-
  // pane fields delegate to SidebarPanel::hasChanges, which handles its own
  // image-vs-color background-value special case.
  const bool launcherNameChanged =
      ui->launcherNameLineEdit && ui->launcherNameLineEdit->text().trimmed() != o.launcherName;
  const bool additionalChanged = m_workingAdditionalLaunchers != o.additionalLaunchers;
  const bool defaultLauncherChanged =
      ui->defaultLauncherComboBox && ui->defaultLauncherComboBox->count() > 0 &&
      ui->defaultLauncherComboBox->currentIndex() != o.defaultLauncherIndex;
  const bool typeChanged =
      ui->collectionTypeComboBox && ui->collectionTypeComboBox->currentText().trimmed() != o.type;
  const bool hSpacingChanged =
      ui->horizontalSpacingSpinBox &&
      spacingUiToInternal(ui->horizontalSpacingSpinBox->value()) != o.horizontalSpacing;
  const bool vSpacingChanged =
      ui->verticalSpacingSpinBox &&
      spacingUiToInternal(ui->verticalSpacingSpinBox->value()) != o.verticalSpacing;

  return launcherNameChanged || additionalChanged || defaultLauncherChanged || typeChanged ||
         hSpacingChanged || vSpacingChanged || ui->sidebarPanel->hasChanges(o) ||

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

         // View / titles / folders / typography
         comboEnumChanged(ui->viewTypeComboBox, o.viewType) ||
         checkboxChanged(ui->hideMissingArtworkCheckBox, o.hideMissingArtwork) ||
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
  // GeneralSettingsPanel-owned fields (Selection & Display) — struct compare.
  if (m_generalSettings.rememberSelection != m_originalGeneralSettings.rememberSelection ||
      m_generalSettings.wrapNavigation != m_originalGeneralSettings.wrapNavigation ||
      m_generalSettings.selectItemOnHover != m_originalGeneralSettings.selectItemOnHover ||
      m_generalSettings.showTitleInPlaceholder !=
          m_originalGeneralSettings.showTitleInPlaceholder) {
    return true;
  }
  // Splash fields live on SplashPanel, which keeps m_generalSettings live-
  // updated; compare struct-to-struct against the original snapshot rather
  // than going through the UI tree (the old widgets no longer exist on
  // ui_settingsdialog.h).
  if (m_generalSettings.bootSplashEnabled != m_originalGeneralSettings.bootSplashEnabled ||
      m_generalSettings.resumeFocusSplashEnabled !=
          m_originalGeneralSettings.resumeFocusSplashEnabled ||
      m_generalSettings.bootSplashTitle.trimmed() !=
          m_originalGeneralSettings.bootSplashTitle.trimmed() ||
      m_generalSettings.bootSplashSubtitle.trimmed() !=
          m_originalGeneralSettings.bootSplashSubtitle.trimmed() ||
      m_generalSettings.resumeFocusSplashTitle.trimmed() !=
          m_originalGeneralSettings.resumeFocusSplashTitle.trimmed() ||
      m_generalSettings.resumeFocusSplashSubtitle.trimmed() !=
          m_originalGeneralSettings.resumeFocusSplashSubtitle.trimmed()) {
    return true;
  }
  // GeneralSettingsPanel-owned fields (Startup, Input Timing, Performance &
  // History) — struct compare against the original snapshot.
  if (m_generalSettings.startupVideoEnabled != m_originalGeneralSettings.startupVideoEnabled ||
      m_generalSettings.startupVideoPath.trimmed() !=
          m_originalGeneralSettings.startupVideoPath.trimmed() ||
      m_generalSettings.startupCollection != m_originalGeneralSettings.startupCollection ||
      m_generalSettings.runtimeDetectionEnabled !=
          m_originalGeneralSettings.runtimeDetectionEnabled ||
      m_generalSettings.historyEnabled != m_originalGeneralSettings.historyEnabled ||
      m_generalSettings.historyMaxEntries != m_originalGeneralSettings.historyMaxEntries ||
      m_generalSettings.pixmapCacheSizeMB != m_originalGeneralSettings.pixmapCacheSizeMB ||
      m_generalSettings.videoThumbnailExtractionTimeoutMs !=
          m_originalGeneralSettings.videoThumbnailExtractionTimeoutMs ||
      m_generalSettings.keyboardRepeatIntervalMs !=
          m_originalGeneralSettings.keyboardRepeatIntervalMs ||
      m_generalSettings.keyboardRepeatDelayMs != m_originalGeneralSettings.keyboardRepeatDelayMs ||
      m_generalSettings.mouseWheelRows != m_originalGeneralSettings.mouseWheelRows ||
      m_generalSettings.scrollAnimationDurationMs !=
          m_originalGeneralSettings.scrollAnimationDurationMs ||
      m_generalSettings.scrollVelocityMultiplier !=
          m_originalGeneralSettings.scrollVelocityMultiplier ||
      m_generalSettings.clickHoldDelayMs != m_originalGeneralSettings.clickHoldDelayMs ||
      m_generalSettings.clickHoldRepeatIntervalMs !=
          m_originalGeneralSettings.clickHoldRepeatIntervalMs ||
      m_generalSettings.listKeyboardRepeatIntervalMs !=
          m_originalGeneralSettings.listKeyboardRepeatIntervalMs ||
      m_generalSettings.listClickHoldRepeatIntervalMs !=
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
  // Attract-mode fields owned by AttractPanel — struct compare against the
  // original snapshot since the panel keeps m_generalSettings live.
  if (m_generalSettings.attractModeEnabled != m_originalGeneralSettings.attractModeEnabled ||
      m_generalSettings.attractModeIdleTimeoutSec !=
          m_originalGeneralSettings.attractModeIdleTimeoutSec ||
      m_generalSettings.attractModeAutoScrollEnabled !=
          m_originalGeneralSettings.attractModeAutoScrollEnabled ||
      m_generalSettings.attractModeScrollSpeed !=
          m_originalGeneralSettings.attractModeScrollSpeed ||
      m_generalSettings.attractModeAdvanceSelectionEnabled !=
          m_originalGeneralSettings.attractModeAdvanceSelectionEnabled ||
      m_generalSettings.attractModeAdvanceSelectionIntervalSec !=
          m_originalGeneralSettings.attractModeAdvanceSelectionIntervalSec ||
      m_generalSettings.attractModeAdvanceSelectionRandom !=
          m_originalGeneralSettings.attractModeAdvanceSelectionRandom) {
    return true;
  }
  // Customizable toolbar fields owned by ToolbarPanel — struct compare.
  if (m_generalSettings.toolbarShowGridViewButton !=
          m_originalGeneralSettings.toolbarShowGridViewButton ||
      m_generalSettings.toolbarShowListViewButton !=
          m_originalGeneralSettings.toolbarShowListViewButton ||
      m_generalSettings.toolbarShowCoverFlowViewButton !=
          m_originalGeneralSettings.toolbarShowCoverFlowViewButton ||
      m_generalSettings.toolbarShowHorizontalViewButton !=
          m_originalGeneralSettings.toolbarShowHorizontalViewButton ||
      m_generalSettings.toolbarShowHideSubcollectionsButton !=
          m_originalGeneralSettings.toolbarShowHideSubcollectionsButton ||
      m_generalSettings.toolbarShowTypeFilter != m_originalGeneralSettings.toolbarShowTypeFilter ||
      m_generalSettings.toolbarShowTitleFilter !=
          m_originalGeneralSettings.toolbarShowTitleFilter ||
      m_generalSettings.toolbarShowSearchModeButton !=
          m_originalGeneralSettings.toolbarShowSearchModeButton ||
      m_generalSettings.toolbarShowSearchBar != m_originalGeneralSettings.toolbarShowSearchBar ||
      m_generalSettings.toolbarGridViewButtonText !=
          m_originalGeneralSettings.toolbarGridViewButtonText ||
      m_generalSettings.toolbarListViewButtonText !=
          m_originalGeneralSettings.toolbarListViewButtonText ||
      m_generalSettings.toolbarCoverFlowViewButtonText !=
          m_originalGeneralSettings.toolbarCoverFlowViewButtonText ||
      m_generalSettings.toolbarHorizontalViewButtonText !=
          m_originalGeneralSettings.toolbarHorizontalViewButtonText ||
      m_generalSettings.toolbarHideSubcollectionsButtonText !=
          m_originalGeneralSettings.toolbarHideSubcollectionsButtonText ||
      m_generalSettings.toolbarTitleFilterText !=
          m_originalGeneralSettings.toolbarTitleFilterText) {
    return true;
  }
  // ControlsPanel-owned fields (Keyboard / Gamepad / Mouse) — struct compare.
  if (m_generalSettings.keyNavUp != m_originalGeneralSettings.keyNavUp ||
      m_generalSettings.keyNavDown != m_originalGeneralSettings.keyNavDown ||
      m_generalSettings.keyNavLeft != m_originalGeneralSettings.keyNavLeft ||
      m_generalSettings.keyNavRight != m_originalGeneralSettings.keyNavRight ||
      m_generalSettings.keyConfirm != m_originalGeneralSettings.keyConfirm ||
      m_generalSettings.keyBack != m_originalGeneralSettings.keyBack ||
      m_generalSettings.keySearch != m_originalGeneralSettings.keySearch ||
      m_generalSettings.keyHomeView != m_originalGeneralSettings.keyHomeView ||
      m_generalSettings.gamepadUseDpad != m_originalGeneralSettings.gamepadUseDpad ||
      m_generalSettings.gamepadUseLeftStick != m_originalGeneralSettings.gamepadUseLeftStick ||
      m_generalSettings.gamepadConfirmButton != m_originalGeneralSettings.gamepadConfirmButton ||
      m_generalSettings.gamepadBackButton != m_originalGeneralSettings.gamepadBackButton ||
      m_generalSettings.gamepadToggleSidebarButton !=
          m_originalGeneralSettings.gamepadToggleSidebarButton ||
      m_generalSettings.artworkCycleModifier != m_originalGeneralSettings.artworkCycleModifier) {
    return true;
  }
  if (m_generalSettings.useHomeView != m_originalGeneralSettings.useHomeView ||
      m_generalSettings.homeViewLabel.trimmed() !=
          m_originalGeneralSettings.homeViewLabel.trimmed() ||
      m_generalSettings.homeViewIcon.trimmed() !=
          m_originalGeneralSettings.homeViewIcon.trimmed()) {
    return true;
  }
  return false;
}
