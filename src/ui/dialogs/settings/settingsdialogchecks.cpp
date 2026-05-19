// Sibling translation unit for SettingsDialog.
// Extracted from settingsdialogform.cpp during LOC-reduction refactor.
// These remain SettingsDialog members; this is a translation-unit split.
#include <algorithm>
#include <functional>
#include <QAbstractItemView>
#include <QCheckBox>
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
#include <QSpinBox>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <set>

#include "appearanceeffectspanel.h"
#include "appearancelayoutpanel.h"
#include "appearancelistpanel.h"
#include "appearancetitlespanel.h"
#include "appearancetoolbarpanel.h"
#include "artworktabpanel.h"
#include "configurationpanel.h"
#include "extensionutils.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "launchertabpanel.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "sidebarpanel.h"
#include "subfolderspanel.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

auto SettingsDialog::extractUIFieldValues() -> CollectionConfig {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_workingCollections.size()) {
    return {};
  }

  // Allow panels to write directly into the working row via the model.
  // saveCollectionFromUI snapshots first and rolls back if validation
  // fails downstream.
  ui->launcherPanel->save();
  ui->configurationPanel->save();
  ui->artworkPanel->save();
  ui->subfoldersPanel->save();
  ui->appearanceLayoutPanel->save();
  ui->appearanceTitlesPanel->save();
  ui->sidebarPanel->save();

  // Background / palette / list-row colors / vignette owned by
  // AppearanceColorsPanel.
  ui->appearanceColorsPanel->save();

  // List mode settings (font size + row height owned by AppearanceListPanel).
  ui->appearanceListPanel->save();

  // header logo
  ui->appearanceToolbarPanel->save();

  // Effects (parallax + backdrop blur) handled by AppearanceEffectsPanel.
  ui->appearanceEffectsPanel->save();

  // Special handling for tree name, additionalLaunchers, additionalParentNames,
  // and defaultLauncherIndex — these stay on the dialog and write directly
  // into the working row.
  auto &row = m_workingCollections[currentCollectionIndex];
  if (auto *item = m_treeManager ? m_treeManager->itemAt(currentCollectionIndex) : nullptr) {
    QString treeName = item->text(0);
    if (!treeName.isEmpty()) {
      row.name = treeName;
    }
  }
  row.additionalLaunchers = m_workingAdditionalLaunchers;
  row.additionalParentNames = m_workingAdditionalParentNames;
  if (ui->launcherPanel->defaultLauncherComboBox()->count() > 0) {
    row.defaultLauncherIndex = ui->launcherPanel->defaultLauncherComboBox()->currentIndex();
  }
  return row;
}

// Updates parent collection settings from UI
auto SettingsDialog::updateParentCollectionFromUI(CollectionConfig &collection, int index) -> void {
  if (ui->configurationPanel->parentCollectionComboBox()) {
    int dropdownIndex = ui->configurationPanel->parentCollectionComboBox()->currentIndex();
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
  // launcher fields (path / core / params / name / extract / extracted-ext)
  // delegate to LauncherTabPanel::hasChanges; the additional-launchers list
  // and default-launcher combo state still live on the dialog.
  const bool additionalChanged = m_workingAdditionalLaunchers != o.additionalLaunchers;
  const bool defaultLauncherChanged =
      ui->launcherPanel->defaultLauncherComboBox()->count() > 0 &&
      ui->launcherPanel->defaultLauncherComboBox()->currentIndex() != o.defaultLauncherIndex;
  // type comparison is handled inside ConfigurationPanel::hasChanges below.
  const bool hSpacingChanged =
      ui->appearanceLayoutPanel->horizontalSpacingSpinBox() &&
      spacingUiToInternal(ui->appearanceLayoutPanel->horizontalSpacingSpinBox()->value()) !=
          o.horizontalSpacing;
  const bool vSpacingChanged =
      ui->appearanceLayoutPanel->verticalSpacingSpinBox() &&
      spacingUiToInternal(ui->appearanceLayoutPanel->verticalSpacingSpinBox()->value()) !=
          o.verticalSpacing;

  return additionalChanged || defaultLauncherChanged || hSpacingChanged || vSpacingChanged ||
         ui->sidebarPanel->hasChanges() || ui->configurationPanel->hasChanges() ||
         ui->artworkPanel->hasChanges() || ui->launcherPanel->hasChanges() ||

         // Layout / grid / view fields delegate to AppearanceLayoutPanel.
         ui->appearanceLayoutPanel->hasChanges() ||

         // Titles / folders.
         ui->appearanceTitlesPanel->hasChanges() || ui->subfoldersPanel->hasChanges();
}

// Extension + customArtworkTypes dirty checks live in ConfigurationPanel /
// ArtworkTabPanel respectively (covered by checkBasicFieldChanges via the
// panels' own hasChanges() methods). Kept as an empty stub so the
// hasUnsavedChanges() callsite stays unchanged for now.
auto SettingsDialog::checkExtensionChanges() const -> bool {
  return false;
}

// Checks tree name changes
auto SettingsDialog::checkTreeNameChanges() const -> bool {
  QString currentTreeName = originalCollection.name;
  if (auto *item = m_treeManager ? m_treeManager->itemAt(currentCollectionIndex) : nullptr) {
    currentTreeName = item->text(0);
  }
  return currentTreeName != originalCollection.name;
}

// Checks parent collection changes
auto SettingsDialog::checkParentCollectionChanges() const -> bool {
  int dropdownIndex = (ui->configurationPanel->parentCollectionComboBox())
                          ? ui->configurationPanel->parentCollectionComboBox()->currentIndex()
                          : -1;
  int currentParentIndex = -1;
  if (dropdownIndex >= 0 && dropdownIndex < m_parentCollectionMapping.size()) {
    currentParentIndex = m_parentCollectionMapping[dropdownIndex];
  }
  return currentParentIndex != originalCollection.parentCollectionIndex;
}

// Checks dimension changes
auto SettingsDialog::checkDimensionChanges() const -> bool {
  // Item dimensions live on AppearanceLayoutPanel; rolled into
  // checkBasicFieldChanges via the panel's own hasChanges().
  return false;
}

// Checks color field changes — palette / list-row colors / vignette / header-
// logo / parallax+blur. Background type/value are part of AppearanceColorsPanel
// too (collapsed under hasChanges).
auto SettingsDialog::checkColorChanges() const -> bool {
  return ui->appearanceColorsPanel->hasChanges() || ui->appearanceToolbarPanel->hasChanges() ||
         ui->appearanceEffectsPanel->hasChanges();
}

// Checks list mode field changes
auto SettingsDialog::checkListModeChanges() const -> bool {
  return ui->appearanceListPanel->hasChanges();
}

// Background type/value dirty check rolled into checkColorChanges via
// AppearanceColorsPanel::hasChanges. Kept as a stub so the hasUnsavedChanges()
// callsite stays unchanged.
auto SettingsDialog::checkBackgroundChanges() const -> bool {
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
  // Title-tint fields owned by AppearanceColorsPanel — struct compare against
  // the original snapshot since the panel keeps m_generalSettings live.
  if (m_generalSettings.titleTintSaturation != m_originalGeneralSettings.titleTintSaturation ||
      m_generalSettings.titleTintLightness != m_originalGeneralSettings.titleTintLightness ||
      m_generalSettings.titleBaseColor != m_originalGeneralSettings.titleBaseColor) {
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
  // Marquee fields owned by MarqueePanel — same deferred-save shape as
  // AttractPanel, struct compare against the original snapshot.
  if (m_generalSettings.marqueeEnabled != m_originalGeneralSettings.marqueeEnabled ||
      m_generalSettings.marqueeScreenName != m_originalGeneralSettings.marqueeScreenName ||
      m_generalSettings.marqueeMode != m_originalGeneralSettings.marqueeMode) {
    return true;
  }
  // Scraper options owned by ScraperSettingsPanel — also deferred-save
  // via panel.writeModel(); without this diff the dialog's "anything
  // changed?" check would miss them and skip persistence on close.
  if (m_generalSettings.scraperOptions.preset != m_originalGeneralSettings.scraperOptions.preset ||
      m_generalSettings.scraperOptions.mediaMaxDimension !=
          m_originalGeneralSettings.scraperOptions.mediaMaxDimension ||
      m_generalSettings.scraperOptions.mediaConcurrency !=
          m_originalGeneralSettings.scraperOptions.mediaConcurrency ||
      m_generalSettings.scraperOptions.mediaThrottleMs !=
          m_originalGeneralSettings.scraperOptions.mediaThrottleMs ||
      m_generalSettings.scraperOptions.batchItemConcurrency !=
          m_originalGeneralSettings.scraperOptions.batchItemConcurrency ||
      m_generalSettings.scraperOptions.rescrapeMode !=
          m_originalGeneralSettings.scraperOptions.rescrapeMode ||
      m_generalSettings.scraperOptions.skipRecentScrapeDays !=
          m_originalGeneralSettings.scraperOptions.skipRecentScrapeDays) {
    return true;
  }
  // ScraperCredentialsPanel: full credential map compare. QHash
  // operator== checks size + key/value equality, so adding/removing
  // a provider or toggling any single field flips this.
  if (m_generalSettings.scraperCredentials != m_originalGeneralSettings.scraperCredentials) {
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
