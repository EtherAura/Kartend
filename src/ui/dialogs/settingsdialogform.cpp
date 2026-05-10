// Sibling translation unit for SettingsDialog: form/spacing connections,
// field extraction, change detection, browse helpers, load/save.
#include <algorithm>
#include <functional>
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
#include "interactionmanager.h"
#include "itemwidget.h"
#include "launchertabpanel.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsformbinding.h"
#include "settingsmanager.h"
#include "splashpanel.h"
#include "toolbarpanel.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"
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
  if (!validatePath(collection.launcherPath, tr("Launcher Path"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.corePath, tr("Core Path"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.backgroundImage, tr("Background Image"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.backgroundVideo, tr("Background Video"))) {
    rollback();
    return;
  }
  if (!validatePath(collection.headerLogoImage, tr("Header Logo"))) {
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
  QPushButton *btn = ui->collectionsTreeShell->saveCollectionButton();
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
    ui->collectionsTreeShell->removeCollectionButton()->setEnabled(hasSelection && !collections.isEmpty());
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
  ui->appearanceLayoutPanel->gridWidthSpinBox()->setMaximum(UIConstants::Grid::MAX_WIDTH);
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
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if (mainWindow) {
    m_generalSettings = mainWindow->m_generalSettings;
  }
  // Splash / Fonts / Attract / general "General" sub-tab fields are all
  // owned by their respective panels — refresh them from the working copy.
  ui->splashPanel->refresh();
  ui->fontsPanel->refresh();
  ui->attractPanel->refresh();
  ui->generalSettingsPanel->refresh();
  // Title-tint fields physically live in the per-collection appearance tab
  // even though they edit GeneralSettings; owned by AppearanceColorsPanel
  // which observes &m_generalSettings.
  ui->appearanceColorsPanel->refresh();
  // Populate the panel's startup-collection combo with the live collection
  // names so the user can pick one.
  if (mainWindow) {
    QStringList names;
    names.reserve(mainWindow->m_collections.size());
    for (const CollectionConfig &cfg : std::as_const(mainWindow->m_collections)) {
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

void SettingsDialog::saveGeneralSettingsFromUI() {
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if ((mainWindow) && (mainWindow->getSettingsManager())) {
    // Splash / GeneralSettings fields owned by their respective panels —
    // already in m_generalSettings; mirror the whole struct's relevant
    // fields to mainWindow below. Side-effect application (PixmapCache /
    // VideoThumbnailExtractor) still runs here so the change is visible
    // while the dialog is open.
    mainWindow->m_generalSettings.bootSplashEnabled = m_generalSettings.bootSplashEnabled;
    mainWindow->m_generalSettings.resumeFocusSplashEnabled =
        m_generalSettings.resumeFocusSplashEnabled;
    mainWindow->m_generalSettings.bootSplashTitle = m_generalSettings.bootSplashTitle;
    mainWindow->m_generalSettings.bootSplashSubtitle = m_generalSettings.bootSplashSubtitle;
    mainWindow->m_generalSettings.resumeFocusSplashTitle = m_generalSettings.resumeFocusSplashTitle;
    mainWindow->m_generalSettings.resumeFocusSplashSubtitle =
        m_generalSettings.resumeFocusSplashSubtitle;

    // Startup
    mainWindow->m_generalSettings.startupCollection = m_generalSettings.startupCollection;
    mainWindow->m_generalSettings.useHomeView = m_generalSettings.useHomeView;
    mainWindow->m_generalSettings.homeViewLabel = m_generalSettings.homeViewLabel;
    mainWindow->m_generalSettings.homeViewIcon = m_generalSettings.homeViewIcon;
    mainWindow->m_generalSettings.startupVideoEnabled = m_generalSettings.startupVideoEnabled;
    mainWindow->m_generalSettings.startupVideoPath = m_generalSettings.startupVideoPath;
    // Selection & Display
    mainWindow->m_generalSettings.rememberSelection = m_generalSettings.rememberSelection;
    mainWindow->m_generalSettings.wrapNavigation = m_generalSettings.wrapNavigation;
    mainWindow->m_generalSettings.selectItemOnHover = m_generalSettings.selectItemOnHover;
    mainWindow->m_generalSettings.showTitleInPlaceholder = m_generalSettings.showTitleInPlaceholder;
    // Input & Scroll Timing
    mainWindow->m_generalSettings.mouseWheelRows = m_generalSettings.mouseWheelRows;
    mainWindow->m_generalSettings.scrollVelocityMultiplier =
        m_generalSettings.scrollVelocityMultiplier;
    mainWindow->m_generalSettings.scrollAnimationDurationMs =
        m_generalSettings.scrollAnimationDurationMs;
    mainWindow->m_generalSettings.clickHoldDelayMs = m_generalSettings.clickHoldDelayMs;
    mainWindow->m_generalSettings.clickHoldRepeatIntervalMs =
        m_generalSettings.clickHoldRepeatIntervalMs;
    mainWindow->m_generalSettings.listClickHoldRepeatIntervalMs =
        m_generalSettings.listClickHoldRepeatIntervalMs;
    mainWindow->m_generalSettings.keyboardRepeatIntervalMs =
        m_generalSettings.keyboardRepeatIntervalMs;
    mainWindow->m_generalSettings.keyboardRepeatDelayMs = m_generalSettings.keyboardRepeatDelayMs;
    mainWindow->m_generalSettings.listKeyboardRepeatIntervalMs =
        m_generalSettings.listKeyboardRepeatIntervalMs;
    // Performance & History (with live-apply side effects)
    mainWindow->m_generalSettings.pixmapCacheSizeMB = m_generalSettings.pixmapCacheSizeMB;
    QPixmapCache::setCacheLimit(m_generalSettings.pixmapCacheSizeMB * 1024);
    mainWindow->m_generalSettings.runtimeDetectionEnabled =
        m_generalSettings.runtimeDetectionEnabled;
    mainWindow->m_generalSettings.historyEnabled = m_generalSettings.historyEnabled;
    mainWindow->m_generalSettings.historyMaxEntries = m_generalSettings.historyMaxEntries;
    mainWindow->m_generalSettings.videoThumbnailExtractionTimeoutMs =
        m_generalSettings.videoThumbnailExtractionTimeoutMs;
    VideoThumbnailExtractor::instance()->setExtractionTimeoutMs(
        m_generalSettings.videoThumbnailExtractionTimeoutMs);
    // Attract-mode fields owned by AttractPanel — already in
    // m_generalSettings (the panel's settings pointer); copy to mainWindow.
    mainWindow->m_generalSettings.attractModeEnabled = m_generalSettings.attractModeEnabled;
    mainWindow->m_generalSettings.attractModeIdleTimeoutSec =
        m_generalSettings.attractModeIdleTimeoutSec;
    mainWindow->m_generalSettings.attractModeAutoScrollEnabled =
        m_generalSettings.attractModeAutoScrollEnabled;
    mainWindow->m_generalSettings.attractModeScrollSpeed = m_generalSettings.attractModeScrollSpeed;
    mainWindow->m_generalSettings.attractModeAdvanceSelectionEnabled =
        m_generalSettings.attractModeAdvanceSelectionEnabled;
    mainWindow->m_generalSettings.attractModeAdvanceSelectionIntervalSec =
        m_generalSettings.attractModeAdvanceSelectionIntervalSec;
    mainWindow->m_generalSettings.attractModeAdvanceSelectionRandom =
        m_generalSettings.attractModeAdvanceSelectionRandom;
    // Title-tint fields: AppearanceColorsPanel keeps m_generalSettings live;
    // copy struct fields to mainWindow and apply ItemWidget side effects.
    mainWindow->m_generalSettings.titleTintSaturation = m_generalSettings.titleTintSaturation;
    ItemWidget::setTitleTintSaturation(m_generalSettings.titleTintSaturation);
    mainWindow->m_generalSettings.titleTintLightness = m_generalSettings.titleTintLightness;
    ItemWidget::setTitleTintLightness(m_generalSettings.titleTintLightness);
    mainWindow->m_generalSettings.titleBaseColor = m_generalSettings.titleBaseColor;
    ItemWidget::setTitleBaseColor(m_generalSettings.titleBaseColor);
    // Note: customFontFamily is now saved per-collection, not in general
    // settings

    // Keyboard / Gamepad / Mouse fields owned by ControlsPanel — already
    // in m_generalSettings via writeBack(); copy to mainWindow's struct.
    mainWindow->m_generalSettings.keyNavUp = m_generalSettings.keyNavUp;
    mainWindow->m_generalSettings.keyNavDown = m_generalSettings.keyNavDown;
    mainWindow->m_generalSettings.keyNavLeft = m_generalSettings.keyNavLeft;
    mainWindow->m_generalSettings.keyNavRight = m_generalSettings.keyNavRight;
    mainWindow->m_generalSettings.keyConfirm = m_generalSettings.keyConfirm;
    mainWindow->m_generalSettings.keyBack = m_generalSettings.keyBack;
    mainWindow->m_generalSettings.keySearch = m_generalSettings.keySearch;
    mainWindow->m_generalSettings.keyHomeView = m_generalSettings.keyHomeView;
    mainWindow->m_generalSettings.gamepadUseDpad = m_generalSettings.gamepadUseDpad;
    mainWindow->m_generalSettings.gamepadUseLeftStick = m_generalSettings.gamepadUseLeftStick;
    mainWindow->m_generalSettings.gamepadConfirmButton = m_generalSettings.gamepadConfirmButton;
    mainWindow->m_generalSettings.gamepadBackButton = m_generalSettings.gamepadBackButton;
    mainWindow->m_generalSettings.gamepadToggleSidebarButton =
        m_generalSettings.gamepadToggleSidebarButton;
    mainWindow->m_generalSettings.artworkCycleModifier = m_generalSettings.artworkCycleModifier;
    // launcher presets live on the dialog's m_generalSettings
    // (mutated directly by the Launchers tab) — copy them onto the main
    // window's settings before persisting so the saved snapshot includes
    // any preset add/edit/remove the user just performed.
    mainWindow->m_generalSettings.launcherPresets = m_generalSettings.launcherPresets;

    // Toolbar customization fields owned by ToolbarPanel — already in
    // m_generalSettings via writeBack(); copy to mainWindow's struct.
    mainWindow->m_generalSettings.toolbarShowGridViewButton =
        m_generalSettings.toolbarShowGridViewButton;
    mainWindow->m_generalSettings.toolbarShowListViewButton =
        m_generalSettings.toolbarShowListViewButton;
    mainWindow->m_generalSettings.toolbarShowCoverFlowViewButton =
        m_generalSettings.toolbarShowCoverFlowViewButton;
    mainWindow->m_generalSettings.toolbarShowHorizontalViewButton =
        m_generalSettings.toolbarShowHorizontalViewButton;
    mainWindow->m_generalSettings.toolbarShowHideSubcollectionsButton =
        m_generalSettings.toolbarShowHideSubcollectionsButton;
    mainWindow->m_generalSettings.toolbarShowTypeFilter = m_generalSettings.toolbarShowTypeFilter;
    mainWindow->m_generalSettings.toolbarShowTitleFilter = m_generalSettings.toolbarShowTitleFilter;
    mainWindow->m_generalSettings.toolbarShowSearchModeButton =
        m_generalSettings.toolbarShowSearchModeButton;
    mainWindow->m_generalSettings.toolbarShowSearchBar = m_generalSettings.toolbarShowSearchBar;
    mainWindow->m_generalSettings.toolbarGridViewButtonText =
        m_generalSettings.toolbarGridViewButtonText;
    mainWindow->m_generalSettings.toolbarListViewButtonText =
        m_generalSettings.toolbarListViewButtonText;
    mainWindow->m_generalSettings.toolbarCoverFlowViewButtonText =
        m_generalSettings.toolbarCoverFlowViewButtonText;
    mainWindow->m_generalSettings.toolbarHorizontalViewButtonText =
        m_generalSettings.toolbarHorizontalViewButtonText;
    mainWindow->m_generalSettings.toolbarHideSubcollectionsButtonText =
        m_generalSettings.toolbarHideSubcollectionsButtonText;
    mainWindow->m_generalSettings.toolbarTitleFilterText = m_generalSettings.toolbarTitleFilterText;

    mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
    m_generalSettings = mainWindow->m_generalSettings;

    // Apply showTitleInPlaceholder to ItemWidget + repaint visible widgets,
    // since this used to be a live-apply side-effect and the panel pattern
    // makes the field deferred-save.
    ItemWidget::setShowTitleInPlaceholder(m_generalSettings.showTitleInPlaceholder);
    if (auto *scrollManager = mainWindow->getScrollManager()) {
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
    if (auto *interaction = mainWindow->getInteractionManager()) {
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
    ScrollManager *scrollManager = mainWindow->getScrollManager();
    if (scrollManager) {
      const auto &activeWidgets = scrollManager->getActiveWidgets();
      for (auto it = activeWidgets.constBegin(); it != activeWidgets.constEnd(); ++it) {
        ItemWidget *widget = it.value();
        if (widget) {
          widget->applyTitleTint();
        }
      }
    }
  }
}
