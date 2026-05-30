// Sibling translation unit for SettingsDialog: form/spacing connections,
// field extraction, change detection, browse helpers, load/save.
#include <algorithm>
#include <functional>
#include <limits>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QDir>
#include <QEasingCurve>
#include <QFileDialog>
#include <QFontDialog>
#include <QGraphicsDropShadowEffect>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPixmapCache>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <set>

#include "applicationcontext.h"
#include "attractmanager.h"
#include "attractpanel.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/validationhelpers.h"
#include "extensionutils.h"
#include "fontspanel.h"
#include "gamepadcapturecontroller.h"
#include "iinteractionmanager.h"
#include "imainwindow.h"
#include "iscrollmanager.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "launchertabpanel.h"
#include "marqueepanel.h"
#include "pathutils.h"
#include "settingsdialog.h"
#include "settingsformbinding.h"
#include "splashpanel.h"
#include "toolbarpanel.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"
#include "videothumbnailextractor.h"

void SettingsDialog::revertCurrentCollectionEdits() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_workingCollections.size()) {
    return;
  }

  m_workingCollections[currentCollectionIndex] = originalCollection;
  if (currentCollectionIndex >= 0 && currentCollectionIndex < collections.size()) {
    collections[currentCollectionIndex] = originalCollection;
  }

  if (auto *item = m_treeManager ? m_treeManager->itemAt(currentCollectionIndex) : nullptr) {
    item->setText(0, originalCollection.name);
  }

  loadCollectionToUI(currentCollectionIndex);
  m_collectionSaved = true;
  updateSaveButtonStyle();
}

auto SettingsDialog::resolveUnsavedChanges(const QString &actionDescription,
                                           bool refreshTreeAfterSave) -> bool {
  if (!hasUnsavedChanges()) {
    m_collectionSaved = true;
    return true;
  }

  const QMessageBox::StandardButton decision = promptUnsavedChanges(actionDescription);
  if (decision == QMessageBox::Cancel) {
    return false;
  }
  if (decision == QMessageBox::Save) {
    if (currentCollectionIndex >= 0 && currentCollectionIndex < m_workingCollections.size()) {
      handleSaveCollection(currentCollectionIndex, refreshTreeAfterSave);
    }
    return true;
  }

  revertCurrentCollectionEdits();
  return true;
}

void SettingsDialog::saveCollectionFromUI(int index) {
  if (!CollectionUtils::isValidIndex(index, m_workingCollections)) {
    return;
  }

  // Snapshot the row before panels write into it via extractUIFieldValues —
  // if any path validation below fails we restore the row so a bad input
  // doesn't poison the working state.
  const CollectionConfig snapshot = m_workingCollections[index];

  CollectionConfig collection = extractUIFieldValues();

  // Validate paths for security before saving
  // Check each path that could be used for file operations
  auto validatePath = [this](const QString &path, const QString &fieldName) -> bool {
    if (path.isEmpty()) {
      return true; // Empty paths are allowed (optional fields)
    }
    auto result = PathUtils::validatePathSecurity(path);
    if (result.isError()) {
      QMessageBox::warning(this, tr("Invalid Path"),
                           tr("The %1 contains invalid characters:\n\n%2\n\n"
                              "Please remove shell metacharacters, backslashes, "
                              "or other special characters.")
                               .arg(fieldName, result.error().message));
      return false;
    }
    return true;
  };

  auto rollback = [&]() { m_workingCollections[index] = snapshot; };

  if (!validatePath(collection.mediaDirectory, tr("Media Directory"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.artworkDirectory, tr("Artwork Directory"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.videoDirectory, tr("Video Directory"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.manualDirectory, tr("Manual Directory"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.launcher.launcherPath, tr("Launcher Path"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.launcher.corePath, tr("Core Path"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.background.backgroundImage, tr("Background Image"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.background.backgroundVideo, tr("Background Video"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.background.headerLogoImage, tr("Header Logo"))) {
    rollback();
    return;
  }

  updateParentCollectionFromUI(collection, index);

  m_workingCollections[index] = collection;
  collections = m_workingCollections;
  originalCollection = collection;
  m_collectionSaved = true;
  updateSaveButtonStyle();
}

auto SettingsDialog::hasUnsavedChanges() const -> bool {
  // General settings can be changed without a collection selected
  if (checkGeneralSettingsChanges()) {
    return true;
  }

  if (currentCollectionIndex < 0 || currentCollectionIndex >= collections.size()) {
    return false;
  }

  return checkBasicFieldChanges() || checkExtensionChanges() || checkTreeNameChanges() ||
         checkParentCollectionChanges() || checkLinkedParentsChanges() || checkDimensionChanges() ||
         checkColorChanges() || checkListModeChanges() || checkBackgroundChanges();
}

void SettingsDialog::updateSaveButtonStyle() {
  // illuminate the save icon while there are unsaved changes.
  // The button also stays enabled so the user can click Save; when there are
  // no changes we detach the glow and disable the button.
  const bool dirty = hasUnsavedChanges();
  QPushButton *btn = m_saveButton;
  if (!btn) return;
  btn->setEnabled(dirty);

  if (dirty) {
    // Lazy-init the glow effect and its pulse animation on first transition
    // to the dirty state. Qt takes ownership of the effect via
    // QWidget::setGraphicsEffect, and the animation is parented to `this`.
    if (!m_saveButtonGlow) {
      m_saveButtonGlow = new QGraphicsDropShadowEffect(btn);
      m_saveButtonGlow->setOffset(0, 0);
      // Use the palette's Highlight color so the glow respects the active
      // theme (light/dark) instead of a hard-coded accent.
      QColor glowColor = btn->palette().color(QPalette::Highlight);
      glowColor.setAlpha(220);
      m_saveButtonGlow->setColor(glowColor);
      m_saveButtonGlow->setBlurRadius(8.0);
      btn->setGraphicsEffect(m_saveButtonGlow);
    }
    if (!m_saveButtonGlowAnim) {
      m_saveButtonGlowAnim = new QPropertyAnimation(m_saveButtonGlow, "blurRadius", this);
      m_saveButtonGlowAnim->setDuration(1200);
      m_saveButtonGlowAnim->setStartValue(6.0);
      m_saveButtonGlowAnim->setEndValue(22.0);
      m_saveButtonGlowAnim->setEasingCurve(QEasingCurve::InOutSine);
      m_saveButtonGlowAnim->setLoopCount(-1);
      // Ping-pong between start/end by alternating direction on each loop.
      QObject::connect(m_saveButtonGlowAnim, &QPropertyAnimation::currentLoopChanged, this,
                       [this](int) {
                         m_saveButtonGlowAnim->setDirection(m_saveButtonGlowAnim->direction() ==
                                                                    QAbstractAnimation::Forward
                                                                ? QAbstractAnimation::Backward
                                                                : QAbstractAnimation::Forward);
                       });
    }
    m_saveButtonGlow->setEnabled(true);
    if (m_saveButtonGlowAnim->state() != QAbstractAnimation::Running) {
      m_saveButtonGlowAnim->start();
    }
  } else {
    if (m_saveButtonGlowAnim && m_saveButtonGlowAnim->state() == QAbstractAnimation::Running) {
      m_saveButtonGlowAnim->stop();
    }
    if (m_saveButtonGlow) {
      // Disabling (rather than removing) the effect keeps Qt's effect-owner
      // wiring stable across dirty/clean transitions.
      m_saveButtonGlow->setEnabled(false);
    }
  }
}

void SettingsDialog::updateDeleteButtonState() {
  if (ui->collectionsTreeShell->removeCollectionButton()) {
    // Enable delete when there's a valid collection selected
    bool hasSelection =
        currentTreeItem && m_treeManager && m_treeManager->contains(currentTreeItem);
    ui->collectionsTreeShell->removeCollectionButton()->setEnabled(hasSelection &&
                                                                   !collections.isEmpty());
  }
}

void SettingsDialog::updateUIForLauncherType(const QString &launcherPath) {
  bool hasContentDir = !ui->configurationPanel->mediaDirLineEdit()->text().trimmed().isEmpty();
  bool usesLibretroCore = LauncherUtils::usesLibretroCore(launcherPath);
  bool showCore = hasContentDir && usesLibretroCore;
  ui->launcherPanel->coreLineEdit()->setVisible(showCore);
  ui->launcherPanel->browseCoreButton()->setVisible(showCore);
  ui->launcherPanel->labelCore()->setVisible(showCore);
  if (usesLibretroCore) {
    ui->launcherPanel->coreLineEdit()->setToolTip("Path to libretro core file (.so/.dll/.dylib)");
    ui->launcherPanel->launchParamsLineEdit()->setToolTip(
        "Additional libretro frontend parameters");
  } else {
    ui->launcherPanel->launchParamsLineEdit()->setToolTip(
        "Additional command-line parameters for the launcher");
  }

  // Update extract archives visibility based on launcher type
  updateExtractArchivesVisibility();
}

void SettingsDialog::onContentDirectoryChanged() {
  updateFieldVisibility();
  checkForChanges();
}

void SettingsDialog::updateFieldVisibility() {
  bool hasContentDir = !ui->configurationPanel->mediaDirLineEdit()->text().trimmed().isEmpty();

  // Launcher path / params + label visibility tracks hasContentDir; toggling
  // the line edits is enough — the labels are inside the panel and stay with
  // the panel widget visibility.
  ui->launcherPanel->launcherLineEdit()->setVisible(hasContentDir);
  ui->launcherPanel->browseLauncherButton()->setVisible(hasContentDir);
  ui->launcherPanel->launchParamsLineEdit()->setVisible(hasContentDir);
  ui->configurationPanel->fileExtensionsLineEdit()->setVisible(hasContentDir);

  // ArtworkTabPanel widgets are always visible — collections inherit
  // artwork directories from a parent even when they have no content of
  // their own.

  if (hasContentDir) {
    updateUIForLauncherType(ui->launcherPanel->launcherLineEdit()->text());
  } else {
    ui->launcherPanel->labelCore()->setVisible(false);
    ui->launcherPanel->coreLineEdit()->setVisible(false);
    ui->launcherPanel->browseCoreButton()->setVisible(false);
  }
  // Archive Handling stays visible regardless of content dir / launcher type
  // so the user can toggle the option freely.
  updateExtractArchivesVisibility();
}

void SettingsDialog::updateExtractArchivesVisibility() {
  // Archive Handling section is always visible — historically gated to
  // libretro frontends, but the user wants the toggle accessible regardless
  // of launcher type.
  ui->launcherPanel->extractArchivesCheckBox()->setVisible(true);

  // Launch Extension is meaningful only when extraction is enabled.
  bool extractEnabled = ui->launcherPanel->extractArchivesCheckBox()->isChecked();
  ui->launcherPanel->labelExtractedExtension()->setVisible(extractEnabled);
  ui->launcherPanel->extractedExtensionLineEdit()->setVisible(extractEnabled);

  if (ui->launcherPanel->launcherArchiveGroupBox()) {
    ui->launcherPanel->launcherArchiveGroupBox()->setVisible(true);
  }
}

void SettingsDialog::onExtractArchivesToggled(bool checked) {
  Q_UNUSED(checked)
  updateExtractArchivesVisibility();
}

void SettingsDialog::updateGridWidthLimits() {
  if (!ui->appearanceLayoutPanel->gridWidthSpinBox()) {
    return;
  }
  int preservedValue = ui->appearanceLayoutPanel->gridWidthSpinBox()->value();
  ui->appearanceLayoutPanel->gridWidthSpinBox()->setMaximum(std::numeric_limits<int>::max());
  ui->appearanceLayoutPanel->gridWidthSpinBox()->setValue(preservedValue);
}

void SettingsDialog::onGridWidthChanged(int value) {
  Q_UNUSED(value)
  checkForChanges();
  if (!m_isLoading && currentCollectionIndex == originalCurrentCollectionIndex &&
      originalCurrentCollectionIndex >= 0 && originalCurrentCollectionIndex < collections.size()) {
    emit gridWidthChanged(currentCollectionIndex, value);
  }
}

void SettingsDialog::loadGeneralSettingsToUI() {
  auto *mainWindow = dynamic_cast<IMainWindow *>(parent());
  if (mainWindow) {
    m_generalSettings = mainWindow->generalSettings();
  }
  // Splash / Fonts / Attract / general "General" sub-tab fields are all
  // owned by their respective panels — refresh them from the working copy.
  ui->splashPanel->refresh();
  ui->fontsPanel->refresh();
  ui->attractPanel->refresh();
  ui->marqueePanel->refresh();
  ui->generalSettingsPanel->refresh();
  ui->scraperSettingsPanel->refresh();
  ui->screenScraperCredentialsPanel->refresh();
  ui->tmdbCredentialsPanel->refresh();
  // Title-tint fields physically live in the per-collection appearance tab
  // even though they edit GeneralSettings; owned by AppearanceColorsPanel
  // which observes &m_generalSettings.
  ui->appearanceColorsPanel->refresh();
  // Populate the panel's startup-collection combo with the live collection
  // names so the user can pick one.
  if (mainWindow) {
    const auto &mwCollections = mainWindow->collections();
    QStringList names;
    names.reserve(mwCollections.size());
    for (const CollectionConfig &cfg : std::as_const(mwCollections)) {
      names.append(cfg.name);
    }
    ui->generalSettingsPanel->setStartupCollections(names,
                                                    m_generalSettings.startup.startupCollection);
  }
  // Note: customFontEdit is now loaded per-collection in loadCollectionFields()

  // Keyboard / Gamepad / Mouse fields owned by ControlsPanel.
  ui->controlsPanel->refresh();
  // Toolbar customization fields owned by ToolbarPanel.
  ui->toolbarPanel->refresh();

  // Store original general settings for change detection
  m_originalGeneralSettings = m_generalSettings;

  // hydrate the launcher-presets list from the loaded general
  // settings. Done after m_originalGeneralSettings is captured so the change
  // detector can compare the live presets against the saved baseline.
  ui->launcherPresetsPanel->refresh();

  if (m_gamepadCapture) {
    m_gamepadCapture->refreshUi();
  }
}

ErrorUtils::Result<void> SettingsDialog::saveGeneralSettingsFromUI() {
  auto *mainWindow = dynamic_cast<IMainWindow *>(parent());
  auto *settingsManager = m_ctx ? m_ctx->settingsManager() : nullptr;
  if (mainWindow && settingsManager) {
    GeneralSettings &mwSettings = mainWindow->generalSettings();
    // Splash / GeneralSettings fields owned by their respective panels —
    // already in m_generalSettings; mirror the whole struct's relevant
    // fields to mainWindow below. Side-effect application (PixmapCache /
    // VideoThumbnailExtractor) still runs here so the change is visible
    // while the dialog is open.
    mwSettings.splash.bootSplashEnabled = m_generalSettings.splash.bootSplashEnabled;
    mwSettings.splash.resumeFocusSplashEnabled = m_generalSettings.splash.resumeFocusSplashEnabled;
    mwSettings.splash.bootSplashTitle = m_generalSettings.splash.bootSplashTitle;
    mwSettings.splash.bootSplashSubtitle = m_generalSettings.splash.bootSplashSubtitle;
    mwSettings.splash.resumeFocusSplashTitle = m_generalSettings.splash.resumeFocusSplashTitle;
    mwSettings.splash.resumeFocusSplashSubtitle =
        m_generalSettings.splash.resumeFocusSplashSubtitle;

    // Startup
    mwSettings.startup.startupCollection = m_generalSettings.startup.startupCollection;
    mwSettings.startup.useHomeView = m_generalSettings.startup.useHomeView;
    mwSettings.startup.homeViewLabel = m_generalSettings.startup.homeViewLabel;
    mwSettings.startup.homeViewIcon = m_generalSettings.startup.homeViewIcon;
    mwSettings.startup.startupVideoEnabled = m_generalSettings.startup.startupVideoEnabled;
    mwSettings.startup.startupVideoPath = m_generalSettings.startup.startupVideoPath;
    // Selection & Display
    mwSettings.input.rememberSelection = m_generalSettings.input.rememberSelection;
    mwSettings.input.wrapNavigation = m_generalSettings.input.wrapNavigation;
    mwSettings.input.selectItemOnHover = m_generalSettings.input.selectItemOnHover;
    mwSettings.view.showTitleInPlaceholder = m_generalSettings.view.showTitleInPlaceholder;
    // Input & Scroll Timing
    mwSettings.input.mouseWheelRows = m_generalSettings.input.mouseWheelRows;
    mwSettings.input.scrollVelocityMultiplier = m_generalSettings.input.scrollVelocityMultiplier;
    mwSettings.input.scrollAnimationDurationMs = m_generalSettings.input.scrollAnimationDurationMs;
    mwSettings.input.clickHoldDelayMs = m_generalSettings.input.clickHoldDelayMs;
    mwSettings.input.clickHoldRepeatIntervalMs = m_generalSettings.input.clickHoldRepeatIntervalMs;
    mwSettings.input.listClickHoldRepeatIntervalMs =
        m_generalSettings.input.listClickHoldRepeatIntervalMs;
    mwSettings.input.keyboardRepeatIntervalMs = m_generalSettings.input.keyboardRepeatIntervalMs;
    mwSettings.input.keyboardRepeatDelayMs = m_generalSettings.input.keyboardRepeatDelayMs;
    mwSettings.input.listKeyboardRepeatIntervalMs =
        m_generalSettings.input.listKeyboardRepeatIntervalMs;
    // Performance & History (with live-apply side effects).
    // applyPixmapCacheBudget propagates the new size to both
    // QPixmapCache and the CacheManager artworkCache in lockstep —
    // see Kartend-10pb (the latter never received the user setting
    // before).
    mwSettings.media.pixmapCacheSizeMB = m_generalSettings.media.pixmapCacheSizeMB;
    mainWindow->applyPixmapCacheBudget(m_generalSettings.media.pixmapCacheSizeMB);
    mwSettings.runtimeDetection.runtimeDetectionEnabled =
        m_generalSettings.runtimeDetection.runtimeDetectionEnabled;
    mwSettings.history.historyEnabled = m_generalSettings.history.historyEnabled;
    mwSettings.history.historyMaxEntries = m_generalSettings.history.historyMaxEntries;
    mwSettings.media.videoThumbnailExtractionTimeoutMs =
        m_generalSettings.media.videoThumbnailExtractionTimeoutMs;
    VideoThumbnailExtractor::instance()->setExtractionTimeoutMs(
        m_generalSettings.media.videoThumbnailExtractionTimeoutMs);
    // Attract-mode fields owned by AttractPanel — already in
    // m_generalSettings (the panel's settings pointer); copy to mainWindow.
    mwSettings.attract.attractModeEnabled = m_generalSettings.attract.attractModeEnabled;
    mwSettings.attract.attractModeIdleTimeoutSec =
        m_generalSettings.attract.attractModeIdleTimeoutSec;
    mwSettings.attract.attractModeAutoScrollEnabled =
        m_generalSettings.attract.attractModeAutoScrollEnabled;
    mwSettings.attract.attractModeScrollSpeed = m_generalSettings.attract.attractModeScrollSpeed;
    mwSettings.attract.attractModeAdvanceSelectionEnabled =
        m_generalSettings.attract.attractModeAdvanceSelectionEnabled;
    mwSettings.attract.attractModeAdvanceSelectionIntervalSec =
        m_generalSettings.attract.attractModeAdvanceSelectionIntervalSec;
    mwSettings.attract.attractModeAdvanceSelectionRandom =
        m_generalSettings.attract.attractModeAdvanceSelectionRandom;
    // Marquee fields owned by MarqueePanel — copy struct + ping
    // MainWindow's marquee lifecycle so the window appears / disappears /
    // moves to a new screen / switches mode without an app restart.
    mwSettings.marquee.marqueeEnabled = m_generalSettings.marquee.marqueeEnabled;
    mwSettings.marquee.marqueeScreenName = m_generalSettings.marquee.marqueeScreenName;
    mwSettings.marquee.marqueeMode = m_generalSettings.marquee.marqueeMode;
    mainWindow->applyMarqueeSettings();
    // Title-tint fields: AppearanceColorsPanel keeps m_generalSettings live;
    // copy struct fields to mainWindow and apply ItemWidget side effects.
    mwSettings.appearance.titleTintSaturation = m_generalSettings.appearance.titleTintSaturation;
    ItemWidget::setTitleTintSaturation(m_generalSettings.appearance.titleTintSaturation);
    mwSettings.appearance.titleTintLightness = m_generalSettings.appearance.titleTintLightness;
    ItemWidget::setTitleTintLightness(m_generalSettings.appearance.titleTintLightness);
    mwSettings.appearance.titleBaseColor = m_generalSettings.appearance.titleBaseColor;
    ItemWidget::setTitleBaseColor(m_generalSettings.appearance.titleBaseColor);
    // Note: customFontFamily is now saved per-collection, not in general
    // settings

    // Keyboard / Gamepad / Mouse fields owned by ControlsPanel — already
    // in m_generalSettings via writeBack(); copy to mainWindow's struct.
    mwSettings.keybindings.keyNavUp = m_generalSettings.keybindings.keyNavUp;
    mwSettings.keybindings.keyNavDown = m_generalSettings.keybindings.keyNavDown;
    mwSettings.keybindings.keyNavLeft = m_generalSettings.keybindings.keyNavLeft;
    mwSettings.keybindings.keyNavRight = m_generalSettings.keybindings.keyNavRight;
    mwSettings.keybindings.keyConfirm = m_generalSettings.keybindings.keyConfirm;
    mwSettings.keybindings.keyBack = m_generalSettings.keybindings.keyBack;
    mwSettings.keybindings.keySearch = m_generalSettings.keybindings.keySearch;
    mwSettings.keybindings.keyHomeView = m_generalSettings.keybindings.keyHomeView;
    mwSettings.gamepad.gamepadUseDpad = m_generalSettings.gamepad.gamepadUseDpad;
    mwSettings.gamepad.gamepadUseLeftStick = m_generalSettings.gamepad.gamepadUseLeftStick;
    mwSettings.gamepad.gamepadConfirmButton = m_generalSettings.gamepad.gamepadConfirmButton;
    mwSettings.gamepad.gamepadBackButton = m_generalSettings.gamepad.gamepadBackButton;
    mwSettings.gamepad.gamepadToggleSidebarButton =
        m_generalSettings.gamepad.gamepadToggleSidebarButton;
    mwSettings.input.artworkCycleModifier = m_generalSettings.input.artworkCycleModifier;
    // launcher presets live on the dialog's m_generalSettings
    // (mutated directly by the Launchers tab) — copy them onto the main
    // window's settings before persisting so the saved snapshot includes
    // any preset add/edit/remove the user just performed.
    mwSettings.launchers.launcherPresets = m_generalSettings.launchers.launcherPresets;

    // Scraper performance + behavior options owned by ScraperSettingsPanel
    // (writes straight into m_model.generalSettings, which is &m_generalSettings).
    // Without this whole-struct copy the dialog's working changes never
    // propagate to mainWindow's GeneralSettings, so they look reverted
    // the next time the dialog opens — same pattern as all the other
    // panel-owned fields above.
    mwSettings.scraper.options = m_generalSettings.scraper.options;
    // ScraperCredentialsPanel writes into m_generalSettings.scraper.credentials
    // (same deferred pattern). The whole map gets copied across so removed
    // / added providers are reflected in mainWindow's struct before the
    // SettingsManager save flushes them to disk.
    mwSettings.scraper.credentials = m_generalSettings.scraper.credentials;

    // Toolbar customization fields owned by ToolbarPanel — already in
    // m_generalSettings via writeBack(); copy to mainWindow's struct.
    mwSettings.toolbar.toolbarShowGridViewButton =
        m_generalSettings.toolbar.toolbarShowGridViewButton;
    mwSettings.toolbar.toolbarShowListViewButton =
        m_generalSettings.toolbar.toolbarShowListViewButton;
    mwSettings.toolbar.toolbarShowCoverFlowViewButton =
        m_generalSettings.toolbar.toolbarShowCoverFlowViewButton;
    mwSettings.toolbar.toolbarShowHorizontalViewButton =
        m_generalSettings.toolbar.toolbarShowHorizontalViewButton;
    mwSettings.toolbar.toolbarShowHideSubcollectionsButton =
        m_generalSettings.toolbar.toolbarShowHideSubcollectionsButton;
    mwSettings.toolbar.toolbarShowTypeFilter = m_generalSettings.toolbar.toolbarShowTypeFilter;
    mwSettings.toolbar.toolbarShowTitleFilter = m_generalSettings.toolbar.toolbarShowTitleFilter;
    mwSettings.toolbar.toolbarShowSearchModeButton =
        m_generalSettings.toolbar.toolbarShowSearchModeButton;
    mwSettings.toolbar.toolbarShowSearchBar = m_generalSettings.toolbar.toolbarShowSearchBar;
    mwSettings.toolbar.toolbarGridViewButtonText =
        m_generalSettings.toolbar.toolbarGridViewButtonText;
    mwSettings.toolbar.toolbarListViewButtonText =
        m_generalSettings.toolbar.toolbarListViewButtonText;
    mwSettings.toolbar.toolbarCoverFlowViewButtonText =
        m_generalSettings.toolbar.toolbarCoverFlowViewButtonText;
    mwSettings.toolbar.toolbarHorizontalViewButtonText =
        m_generalSettings.toolbar.toolbarHorizontalViewButtonText;
    mwSettings.toolbar.toolbarHideSubcollectionsButtonText =
        m_generalSettings.toolbar.toolbarHideSubcollectionsButtonText;
    mwSettings.toolbar.toolbarTitleFilterText = m_generalSettings.toolbar.toolbarTitleFilterText;

    auto saveResult = settingsManager->saveGeneralSettings(mwSettings);
    m_generalSettings = mwSettings;

    // Apply showTitleInPlaceholder to ItemWidget + repaint visible widgets,
    // since this used to be a live-apply side-effect and the panel pattern
    // makes the field deferred-save.
    ItemWidget::setShowTitleInPlaceholder(m_generalSettings.view.showTitleInPlaceholder);
    if (auto *scrollManager = m_ctx->scrollManager()) {
      const auto &activeWidgets = scrollManager->getActiveWidgets();
      for (auto it = activeWidgets.constBegin(); it != activeWidgets.constEnd(); ++it) {
        if (auto *widget = it.value()) {
          widget->onArtworkChanged();
        }
      }
    }

    // Push the new attract-mode tunables (idle timeout, scroll speed, advance-
    // selection toggle/interval) into AttractManager so a runtime change applies
    // without restarting attract mode.
    if (auto *interaction = m_ctx->interactionManager()) {
      if (auto *attract = interaction->attractManager()) {
        attract->reloadSettings();
      }
    }
    // Refresh the originals so the dirty indicator clears for the presets
    // tab too. (Other fields' baselines are reset implicitly by the assign
    // above; this line keeps the comment local to where it matters.)
    m_originalGeneralSettings = m_generalSettings;

    // push the new toolbar config onto the live UI immediately so
    // the user sees the change without restart or extra clicks.
    mainWindow->applyToolbarCustomization();

    // Refresh all visible widgets to apply text appearance changes immediately
    IScrollManager *scrollManager = m_ctx->scrollManager();
    if (scrollManager) {
      const auto &activeWidgets = scrollManager->getActiveWidgets();
      for (auto it = activeWidgets.constBegin(); it != activeWidgets.constEnd(); ++it) {
        ItemWidget *widget = it.value();
        if (widget) {
          widget->applyTitleTint();
        }
      }
    }

    return saveResult;
  }
  return ErrorUtils::Result<void>::success();
}
