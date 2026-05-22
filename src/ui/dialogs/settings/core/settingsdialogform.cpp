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

#include "attractmanager.h"
#include "attractpanel.h"
#include "extensionutils.h"
#include "fontspanel.h"
#include "gamepadcapturecontroller.h"
#include "imainwindow.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "launchertabpanel.h"
#include "marqueepanel.h"
#include "pathutils.h"
#include "scrollmanager.h"
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
    ui->generalSettingsPanel->setStartupCollections(names, m_generalSettings.startupCollection);
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
  if (mainWindow && mainWindow->settingsManager()) {
    GeneralSettings &mwSettings = mainWindow->generalSettings();
    // Splash / GeneralSettings fields owned by their respective panels —
    // already in m_generalSettings; mirror the whole struct's relevant
    // fields to mainWindow below. Side-effect application (PixmapCache /
    // VideoThumbnailExtractor) still runs here so the change is visible
    // while the dialog is open.
    mwSettings.bootSplashEnabled = m_generalSettings.bootSplashEnabled;
    mwSettings.resumeFocusSplashEnabled = m_generalSettings.resumeFocusSplashEnabled;
    mwSettings.bootSplashTitle = m_generalSettings.bootSplashTitle;
    mwSettings.bootSplashSubtitle = m_generalSettings.bootSplashSubtitle;
    mwSettings.resumeFocusSplashTitle = m_generalSettings.resumeFocusSplashTitle;
    mwSettings.resumeFocusSplashSubtitle = m_generalSettings.resumeFocusSplashSubtitle;

    // Startup
    mwSettings.startupCollection = m_generalSettings.startupCollection;
    mwSettings.useHomeView = m_generalSettings.useHomeView;
    mwSettings.homeViewLabel = m_generalSettings.homeViewLabel;
    mwSettings.homeViewIcon = m_generalSettings.homeViewIcon;
    mwSettings.startupVideoEnabled = m_generalSettings.startupVideoEnabled;
    mwSettings.startupVideoPath = m_generalSettings.startupVideoPath;
    // Selection & Display
    mwSettings.rememberSelection = m_generalSettings.rememberSelection;
    mwSettings.wrapNavigation = m_generalSettings.wrapNavigation;
    mwSettings.selectItemOnHover = m_generalSettings.selectItemOnHover;
    mwSettings.showTitleInPlaceholder = m_generalSettings.showTitleInPlaceholder;
    // Input & Scroll Timing
    mwSettings.mouseWheelRows = m_generalSettings.mouseWheelRows;
    mwSettings.scrollVelocityMultiplier = m_generalSettings.scrollVelocityMultiplier;
    mwSettings.scrollAnimationDurationMs = m_generalSettings.scrollAnimationDurationMs;
    mwSettings.clickHoldDelayMs = m_generalSettings.clickHoldDelayMs;
    mwSettings.clickHoldRepeatIntervalMs = m_generalSettings.clickHoldRepeatIntervalMs;
    mwSettings.listClickHoldRepeatIntervalMs = m_generalSettings.listClickHoldRepeatIntervalMs;
    mwSettings.keyboardRepeatIntervalMs = m_generalSettings.keyboardRepeatIntervalMs;
    mwSettings.keyboardRepeatDelayMs = m_generalSettings.keyboardRepeatDelayMs;
    mwSettings.listKeyboardRepeatIntervalMs = m_generalSettings.listKeyboardRepeatIntervalMs;
    // Performance & History (with live-apply side effects).
    // applyPixmapCacheBudget propagates the new size to both
    // QPixmapCache and the CacheManager artworkCache in lockstep —
    // see Kartend-10pb (the latter never received the user setting
    // before).
    mwSettings.pixmapCacheSizeMB = m_generalSettings.pixmapCacheSizeMB;
    mainWindow->applyPixmapCacheBudget(m_generalSettings.pixmapCacheSizeMB);
    mwSettings.runtimeDetectionEnabled = m_generalSettings.runtimeDetectionEnabled;
    mwSettings.historyEnabled = m_generalSettings.historyEnabled;
    mwSettings.historyMaxEntries = m_generalSettings.historyMaxEntries;
    mwSettings.videoThumbnailExtractionTimeoutMs =
        m_generalSettings.videoThumbnailExtractionTimeoutMs;
    VideoThumbnailExtractor::instance()->setExtractionTimeoutMs(
        m_generalSettings.videoThumbnailExtractionTimeoutMs);
    // Attract-mode fields owned by AttractPanel — already in
    // m_generalSettings (the panel's settings pointer); copy to mainWindow.
    mwSettings.attractModeEnabled = m_generalSettings.attractModeEnabled;
    mwSettings.attractModeIdleTimeoutSec = m_generalSettings.attractModeIdleTimeoutSec;
    mwSettings.attractModeAutoScrollEnabled = m_generalSettings.attractModeAutoScrollEnabled;
    mwSettings.attractModeScrollSpeed = m_generalSettings.attractModeScrollSpeed;
    mwSettings.attractModeAdvanceSelectionEnabled =
        m_generalSettings.attractModeAdvanceSelectionEnabled;
    mwSettings.attractModeAdvanceSelectionIntervalSec =
        m_generalSettings.attractModeAdvanceSelectionIntervalSec;
    mwSettings.attractModeAdvanceSelectionRandom =
        m_generalSettings.attractModeAdvanceSelectionRandom;
    // Marquee fields owned by MarqueePanel — copy struct + ping
    // MainWindow's marquee lifecycle so the window appears / disappears /
    // moves to a new screen / switches mode without an app restart.
    mwSettings.marqueeEnabled = m_generalSettings.marqueeEnabled;
    mwSettings.marqueeScreenName = m_generalSettings.marqueeScreenName;
    mwSettings.marqueeMode = m_generalSettings.marqueeMode;
    mainWindow->applyMarqueeSettings();
    // Title-tint fields: AppearanceColorsPanel keeps m_generalSettings live;
    // copy struct fields to mainWindow and apply ItemWidget side effects.
    mwSettings.titleTintSaturation = m_generalSettings.titleTintSaturation;
    ItemWidget::setTitleTintSaturation(m_generalSettings.titleTintSaturation);
    mwSettings.titleTintLightness = m_generalSettings.titleTintLightness;
    ItemWidget::setTitleTintLightness(m_generalSettings.titleTintLightness);
    mwSettings.titleBaseColor = m_generalSettings.titleBaseColor;
    ItemWidget::setTitleBaseColor(m_generalSettings.titleBaseColor);
    // Note: customFontFamily is now saved per-collection, not in general
    // settings

    // Keyboard / Gamepad / Mouse fields owned by ControlsPanel — already
    // in m_generalSettings via writeBack(); copy to mainWindow's struct.
    mwSettings.keyNavUp = m_generalSettings.keyNavUp;
    mwSettings.keyNavDown = m_generalSettings.keyNavDown;
    mwSettings.keyNavLeft = m_generalSettings.keyNavLeft;
    mwSettings.keyNavRight = m_generalSettings.keyNavRight;
    mwSettings.keyConfirm = m_generalSettings.keyConfirm;
    mwSettings.keyBack = m_generalSettings.keyBack;
    mwSettings.keySearch = m_generalSettings.keySearch;
    mwSettings.keyHomeView = m_generalSettings.keyHomeView;
    mwSettings.gamepadUseDpad = m_generalSettings.gamepadUseDpad;
    mwSettings.gamepadUseLeftStick = m_generalSettings.gamepadUseLeftStick;
    mwSettings.gamepadConfirmButton = m_generalSettings.gamepadConfirmButton;
    mwSettings.gamepadBackButton = m_generalSettings.gamepadBackButton;
    mwSettings.gamepadToggleSidebarButton = m_generalSettings.gamepadToggleSidebarButton;
    mwSettings.artworkCycleModifier = m_generalSettings.artworkCycleModifier;
    // launcher presets live on the dialog's m_generalSettings
    // (mutated directly by the Launchers tab) — copy them onto the main
    // window's settings before persisting so the saved snapshot includes
    // any preset add/edit/remove the user just performed.
    mwSettings.launcherPresets = m_generalSettings.launcherPresets;

    // Scraper performance + behavior options owned by ScraperSettingsPanel
    // (writes straight into m_model.generalSettings, which is &m_generalSettings).
    // Without this whole-struct copy the dialog's working changes never
    // propagate to mainWindow's GeneralSettings, so they look reverted
    // the next time the dialog opens — same pattern as all the other
    // panel-owned fields above.
    mwSettings.scraperOptions = m_generalSettings.scraperOptions;
    // ScraperCredentialsPanel writes into m_generalSettings.scraperCredentials
    // (same deferred pattern). The whole map gets copied across so removed
    // / added providers are reflected in mainWindow's struct before the
    // SettingsManager save flushes them to disk.
    mwSettings.scraperCredentials = m_generalSettings.scraperCredentials;

    // Toolbar customization fields owned by ToolbarPanel — already in
    // m_generalSettings via writeBack(); copy to mainWindow's struct.
    mwSettings.toolbarShowGridViewButton = m_generalSettings.toolbarShowGridViewButton;
    mwSettings.toolbarShowListViewButton = m_generalSettings.toolbarShowListViewButton;
    mwSettings.toolbarShowCoverFlowViewButton = m_generalSettings.toolbarShowCoverFlowViewButton;
    mwSettings.toolbarShowHorizontalViewButton = m_generalSettings.toolbarShowHorizontalViewButton;
    mwSettings.toolbarShowHideSubcollectionsButton =
        m_generalSettings.toolbarShowHideSubcollectionsButton;
    mwSettings.toolbarShowTypeFilter = m_generalSettings.toolbarShowTypeFilter;
    mwSettings.toolbarShowTitleFilter = m_generalSettings.toolbarShowTitleFilter;
    mwSettings.toolbarShowSearchModeButton = m_generalSettings.toolbarShowSearchModeButton;
    mwSettings.toolbarShowSearchBar = m_generalSettings.toolbarShowSearchBar;
    mwSettings.toolbarGridViewButtonText = m_generalSettings.toolbarGridViewButtonText;
    mwSettings.toolbarListViewButtonText = m_generalSettings.toolbarListViewButtonText;
    mwSettings.toolbarCoverFlowViewButtonText = m_generalSettings.toolbarCoverFlowViewButtonText;
    mwSettings.toolbarHorizontalViewButtonText = m_generalSettings.toolbarHorizontalViewButtonText;
    mwSettings.toolbarHideSubcollectionsButtonText =
        m_generalSettings.toolbarHideSubcollectionsButtonText;
    mwSettings.toolbarTitleFilterText = m_generalSettings.toolbarTitleFilterText;

    auto saveResult = mainWindow->settingsManager()->saveGeneralSettings(mwSettings);
    m_generalSettings = mwSettings;

    // Apply showTitleInPlaceholder to ItemWidget + repaint visible widgets,
    // since this used to be a live-apply side-effect and the panel pattern
    // makes the field deferred-save.
    ItemWidget::setShowTitleInPlaceholder(m_generalSettings.showTitleInPlaceholder);
    if (auto *scrollManager = mainWindow->scrollManager()) {
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
    if (auto *interaction = mainWindow->interactionManager()) {
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
    ScrollManager *scrollManager = mainWindow->scrollManager();
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
