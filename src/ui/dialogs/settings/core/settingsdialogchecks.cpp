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
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "sidebarpanel.h"
#include "subfolderspanel.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"

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
  row.launcher.additionalLaunchers = m_workingAdditionalLaunchers;
  row.additionalParentNames = m_workingAdditionalParentNames;
  if (ui->launcherPanel->defaultLauncherComboBox()->count() > 0) {
    row.launcher.defaultLauncherIndex =
        ui->launcherPanel->defaultLauncherComboBox()->currentIndex();
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
  const bool additionalChanged = m_workingAdditionalLaunchers != o.launcher.additionalLaunchers;
  const bool defaultLauncherChanged =
      ui->launcherPanel->defaultLauncherComboBox()->count() > 0 &&
      ui->launcherPanel->defaultLauncherComboBox()->currentIndex() !=
          o.launcher.defaultLauncherIndex;
  // type comparison is handled inside ConfigurationPanel::hasChanges below.
  const bool hSpacingChanged =
      ui->appearanceLayoutPanel->horizontalSpacingSpinBox() &&
      spacingUiToInternal(ui->appearanceLayoutPanel->horizontalSpacingSpinBox()->value()) !=
          o.gridLayout.horizontalSpacing;
  const bool vSpacingChanged =
      ui->appearanceLayoutPanel->verticalSpacingSpinBox() &&
      spacingUiToInternal(ui->appearanceLayoutPanel->verticalSpacingSpinBox()->value()) !=
          o.gridLayout.verticalSpacing;

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
  if (m_generalSettings.launchers.launcherPresets !=
      m_originalGeneralSettings.launchers.launcherPresets) {
    return true;
  }
  // GeneralSettingsPanel writes the RetroArch config path live into
  // m_generalSettings.launchers on every keystroke; without this comparison a
  // config-path-only edit never dirties the dialog (Save button stays dark)
  // and the edit can be lost on a collection switch (Kartend-hvdow).
  if (m_generalSettings.launchers.retroarchConfigPath.trimmed() !=
      m_originalGeneralSettings.launchers.retroarchConfigPath.trimmed()) {
    return true;
  }
  // GeneralSettingsPanel-owned fields (Selection & Display) — struct compare.
  if (m_generalSettings.input.rememberSelection !=
          m_originalGeneralSettings.input.rememberSelection ||
      m_generalSettings.input.wrapNavigation != m_originalGeneralSettings.input.wrapNavigation ||
      m_generalSettings.input.selectItemOnHover !=
          m_originalGeneralSettings.input.selectItemOnHover ||
      m_generalSettings.view.showTitleInPlaceholder !=
          m_originalGeneralSettings.view.showTitleInPlaceholder) {
    return true;
  }
  // Splash fields live on SplashPanel, which keeps m_generalSettings live-
  // updated; compare struct-to-struct against the original snapshot rather
  // than going through the UI tree (the old widgets no longer exist on
  // ui_settingsdialog.h).
  if (m_generalSettings.splash.bootSplashEnabled !=
          m_originalGeneralSettings.splash.bootSplashEnabled ||
      m_generalSettings.splash.resumeFocusSplashEnabled !=
          m_originalGeneralSettings.splash.resumeFocusSplashEnabled ||
      m_generalSettings.splash.bootSplashTitle.trimmed() !=
          m_originalGeneralSettings.splash.bootSplashTitle.trimmed() ||
      m_generalSettings.splash.bootSplashSubtitle.trimmed() !=
          m_originalGeneralSettings.splash.bootSplashSubtitle.trimmed() ||
      m_generalSettings.splash.resumeFocusSplashTitle.trimmed() !=
          m_originalGeneralSettings.splash.resumeFocusSplashTitle.trimmed() ||
      m_generalSettings.splash.resumeFocusSplashSubtitle.trimmed() !=
          m_originalGeneralSettings.splash.resumeFocusSplashSubtitle.trimmed()) {
    return true;
  }
  // GeneralSettingsPanel-owned fields (Startup, Input Timing, Performance &
  // History) — struct compare against the original snapshot.
  if (m_generalSettings.startup.startupVideoEnabled !=
          m_originalGeneralSettings.startup.startupVideoEnabled ||
      m_generalSettings.startup.startupVideoPath.trimmed() !=
          m_originalGeneralSettings.startup.startupVideoPath.trimmed() ||
      m_generalSettings.startup.startupCollection !=
          m_originalGeneralSettings.startup.startupCollection ||
      m_generalSettings.runtimeDetection.runtimeDetectionEnabled !=
          m_originalGeneralSettings.runtimeDetection.runtimeDetectionEnabled ||
      m_generalSettings.history.historyEnabled !=
          m_originalGeneralSettings.history.historyEnabled ||
      m_generalSettings.history.historyMaxEntries !=
          m_originalGeneralSettings.history.historyMaxEntries ||
      m_generalSettings.media.pixmapCacheSizeMB !=
          m_originalGeneralSettings.media.pixmapCacheSizeMB ||
      m_generalSettings.media.videoThumbnailExtractionTimeoutMs !=
          m_originalGeneralSettings.media.videoThumbnailExtractionTimeoutMs ||
      m_generalSettings.input.keyboardRepeatIntervalMs !=
          m_originalGeneralSettings.input.keyboardRepeatIntervalMs ||
      m_generalSettings.input.keyboardRepeatDelayMs !=
          m_originalGeneralSettings.input.keyboardRepeatDelayMs ||
      m_generalSettings.input.mouseWheelRows != m_originalGeneralSettings.input.mouseWheelRows ||
      m_generalSettings.input.scrollAnimationDurationMs !=
          m_originalGeneralSettings.input.scrollAnimationDurationMs ||
      m_generalSettings.input.scrollVelocityMultiplier !=
          m_originalGeneralSettings.input.scrollVelocityMultiplier ||
      m_generalSettings.input.clickHoldDelayMs !=
          m_originalGeneralSettings.input.clickHoldDelayMs ||
      m_generalSettings.input.clickHoldRepeatIntervalMs !=
          m_originalGeneralSettings.input.clickHoldRepeatIntervalMs ||
      m_generalSettings.input.listKeyboardRepeatIntervalMs !=
          m_originalGeneralSettings.input.listKeyboardRepeatIntervalMs ||
      m_generalSettings.input.listClickHoldRepeatIntervalMs !=
          m_originalGeneralSettings.input.listClickHoldRepeatIntervalMs) {
    return true;
  }
  // Title-tint fields owned by AppearanceColorsPanel — struct compare against
  // the original snapshot since the panel keeps m_generalSettings live.
  if (m_generalSettings.appearance.titleTintSaturation !=
          m_originalGeneralSettings.appearance.titleTintSaturation ||
      m_generalSettings.appearance.titleTintLightness !=
          m_originalGeneralSettings.appearance.titleTintLightness ||
      m_generalSettings.appearance.titleBaseColor !=
          m_originalGeneralSettings.appearance.titleBaseColor) {
    return true;
  }
  // Attract-mode fields owned by AttractPanel — struct compare against the
  // original snapshot since the panel keeps m_generalSettings live.
  if (m_generalSettings.attract.attractModeEnabled !=
          m_originalGeneralSettings.attract.attractModeEnabled ||
      m_generalSettings.attract.attractModeIdleTimeoutSec !=
          m_originalGeneralSettings.attract.attractModeIdleTimeoutSec ||
      m_generalSettings.attract.attractModeAutoScrollEnabled !=
          m_originalGeneralSettings.attract.attractModeAutoScrollEnabled ||
      m_generalSettings.attract.attractModeScrollSpeed !=
          m_originalGeneralSettings.attract.attractModeScrollSpeed ||
      m_generalSettings.attract.attractModeAdvanceSelectionEnabled !=
          m_originalGeneralSettings.attract.attractModeAdvanceSelectionEnabled ||
      m_generalSettings.attract.attractModeAdvanceSelectionIntervalSec !=
          m_originalGeneralSettings.attract.attractModeAdvanceSelectionIntervalSec ||
      m_generalSettings.attract.attractModeAdvanceSelectionRandom !=
          m_originalGeneralSettings.attract.attractModeAdvanceSelectionRandom) {
    return true;
  }
  // Marquee fields owned by MarqueePanel — same deferred-save shape as
  // AttractPanel, struct compare against the original snapshot.
  if (m_generalSettings.marquee.marqueeEnabled !=
          m_originalGeneralSettings.marquee.marqueeEnabled ||
      m_generalSettings.marquee.marqueeScreenName !=
          m_originalGeneralSettings.marquee.marqueeScreenName ||
      m_generalSettings.marquee.marqueeMode != m_originalGeneralSettings.marquee.marqueeMode) {
    return true;
  }
  // Scraper options owned by ScraperSettingsPanel — also deferred-save
  // via panel.writeModel(); without this diff the dialog's "anything
  // changed?" check would miss them and skip persistence on close.
  if (m_generalSettings.scraper.options.preset !=
          m_originalGeneralSettings.scraper.options.preset ||
      m_generalSettings.scraper.options.mediaMaxDimension !=
          m_originalGeneralSettings.scraper.options.mediaMaxDimension ||
      m_generalSettings.scraper.options.mediaConcurrency !=
          m_originalGeneralSettings.scraper.options.mediaConcurrency ||
      m_generalSettings.scraper.options.mediaThrottleMs !=
          m_originalGeneralSettings.scraper.options.mediaThrottleMs ||
      m_generalSettings.scraper.options.batchItemConcurrency !=
          m_originalGeneralSettings.scraper.options.batchItemConcurrency ||
      m_generalSettings.scraper.options.rescrapeMode !=
          m_originalGeneralSettings.scraper.options.rescrapeMode ||
      m_generalSettings.scraper.options.skipRecentScrapeDays !=
          m_originalGeneralSettings.scraper.options.skipRecentScrapeDays) {
    return true;
  }
  // ScraperCredentialsPanel: full credential map compare. QHash
  // operator== checks size + key/value equality, so adding/removing
  // a provider or toggling any single field flips this.
  if (m_generalSettings.scraper.credentials != m_originalGeneralSettings.scraper.credentials) {
    return true;
  }
  // Customizable toolbar fields owned by ToolbarPanel — struct compare.
  if (m_generalSettings.toolbar.toolbarShowGridViewButton !=
          m_originalGeneralSettings.toolbar.toolbarShowGridViewButton ||
      m_generalSettings.toolbar.toolbarShowListViewButton !=
          m_originalGeneralSettings.toolbar.toolbarShowListViewButton ||
      m_generalSettings.toolbar.toolbarShowCoverFlowViewButton !=
          m_originalGeneralSettings.toolbar.toolbarShowCoverFlowViewButton ||
      m_generalSettings.toolbar.toolbarShowHorizontalViewButton !=
          m_originalGeneralSettings.toolbar.toolbarShowHorizontalViewButton ||
      m_generalSettings.toolbar.toolbarShowHideSubcollectionsButton !=
          m_originalGeneralSettings.toolbar.toolbarShowHideSubcollectionsButton ||
      m_generalSettings.toolbar.toolbarShowTypeFilter !=
          m_originalGeneralSettings.toolbar.toolbarShowTypeFilter ||
      m_generalSettings.toolbar.toolbarShowTitleFilter !=
          m_originalGeneralSettings.toolbar.toolbarShowTitleFilter ||
      m_generalSettings.toolbar.toolbarShowSearchModeButton !=
          m_originalGeneralSettings.toolbar.toolbarShowSearchModeButton ||
      m_generalSettings.toolbar.toolbarShowSearchBar !=
          m_originalGeneralSettings.toolbar.toolbarShowSearchBar ||
      m_generalSettings.toolbar.toolbarGridViewButtonText !=
          m_originalGeneralSettings.toolbar.toolbarGridViewButtonText ||
      m_generalSettings.toolbar.toolbarListViewButtonText !=
          m_originalGeneralSettings.toolbar.toolbarListViewButtonText ||
      m_generalSettings.toolbar.toolbarCoverFlowViewButtonText !=
          m_originalGeneralSettings.toolbar.toolbarCoverFlowViewButtonText ||
      m_generalSettings.toolbar.toolbarHorizontalViewButtonText !=
          m_originalGeneralSettings.toolbar.toolbarHorizontalViewButtonText ||
      m_generalSettings.toolbar.toolbarHideSubcollectionsButtonText !=
          m_originalGeneralSettings.toolbar.toolbarHideSubcollectionsButtonText ||
      m_generalSettings.toolbar.toolbarTitleFilterText !=
          m_originalGeneralSettings.toolbar.toolbarTitleFilterText) {
    return true;
  }
  // ControlsPanel-owned fields (Keyboard / Gamepad / Mouse) — struct compare.
  if (m_generalSettings.keybindings.keyNavUp != m_originalGeneralSettings.keybindings.keyNavUp ||
      m_generalSettings.keybindings.keyNavDown !=
          m_originalGeneralSettings.keybindings.keyNavDown ||
      m_generalSettings.keybindings.keyNavLeft !=
          m_originalGeneralSettings.keybindings.keyNavLeft ||
      m_generalSettings.keybindings.keyNavRight !=
          m_originalGeneralSettings.keybindings.keyNavRight ||
      m_generalSettings.keybindings.keyConfirm !=
          m_originalGeneralSettings.keybindings.keyConfirm ||
      m_generalSettings.keybindings.keyBack != m_originalGeneralSettings.keybindings.keyBack ||
      m_generalSettings.keybindings.keySearch != m_originalGeneralSettings.keybindings.keySearch ||
      m_generalSettings.keybindings.keyHomeView !=
          m_originalGeneralSettings.keybindings.keyHomeView ||
      m_generalSettings.gamepad.gamepadUseDpad !=
          m_originalGeneralSettings.gamepad.gamepadUseDpad ||
      m_generalSettings.gamepad.gamepadUseLeftStick !=
          m_originalGeneralSettings.gamepad.gamepadUseLeftStick ||
      m_generalSettings.gamepad.gamepadConfirmButton !=
          m_originalGeneralSettings.gamepad.gamepadConfirmButton ||
      m_generalSettings.gamepad.gamepadBackButton !=
          m_originalGeneralSettings.gamepad.gamepadBackButton ||
      m_generalSettings.gamepad.gamepadToggleSidebarButton !=
          m_originalGeneralSettings.gamepad.gamepadToggleSidebarButton ||
      m_generalSettings.input.artworkCycleModifier !=
          m_originalGeneralSettings.input.artworkCycleModifier) {
    return true;
  }
  if (m_generalSettings.startup.useHomeView != m_originalGeneralSettings.startup.useHomeView ||
      m_generalSettings.startup.homeViewLabel.trimmed() !=
          m_originalGeneralSettings.startup.homeViewLabel.trimmed() ||
      m_generalSettings.startup.homeViewIcon.trimmed() !=
          m_originalGeneralSettings.startup.homeViewIcon.trimmed()) {
    return true;
  }
  return false;
}
