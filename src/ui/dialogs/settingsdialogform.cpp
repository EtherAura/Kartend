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
#include "extensionutils.h"
#include "gamepadcapturecontroller.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsformbinding.h"
#include "settingsmanager.h"
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

  if (collectionIndexToItem.contains(currentCollectionIndex)) {
    if (auto *item = collectionIndexToItem[currentCollectionIndex]) {
      item->setText(0, originalCollection.name);
    }
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

  if (!validatePath(collection.mediaDirectory, tr("Media Directory"))) {
    return;
  }
  if (!validatePath(collection.artworkDirectory, tr("Artwork Directory"))) {
    return;
  }
  if (!validatePath(collection.videoDirectory, tr("Video Directory"))) {
    return;
  }
  if (!validatePath(collection.manualDirectory, tr("Manual Directory"))) {
    return;
  }
  if (!validatePath(collection.launcherPath, tr("Launcher Path"))) {
    return;
  }
  if (!validatePath(collection.corePath, tr("Core Path"))) {
    return;
  }
  if (!validatePath(collection.backgroundImage, tr("Background Image"))) {
    return;
  }
  if (!validatePath(collection.backgroundVideo, tr("Background Video"))) {
    return;
  }
  if (!validatePath(collection.headerLogoImage, tr("Header Logo"))) {
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
  QPushButton *btn = ui->saveCollectionButton;
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
  if (ui->removeCollectionButton) {
    // Enable delete when there's a valid collection selected
    bool hasSelection = currentTreeItem && itemToCollectionIndex.contains(currentTreeItem);
    ui->removeCollectionButton->setEnabled(hasSelection && !collections.isEmpty());
  }
}

void SettingsDialog::updateUIForLauncherType(const QString &launcherPath) {
  bool hasContentDir = !ui->mediaDirLineEdit->text().trimmed().isEmpty();
  bool usesLibretroCore = LauncherUtils::usesLibretroCore(launcherPath);
  bool showCore = hasContentDir && usesLibretroCore;
  ui->coreLineEdit->setVisible(showCore);
  ui->browseCoreButton->setVisible(showCore);
  ui->label_core->setVisible(showCore);
  if (usesLibretroCore) {
    ui->coreLineEdit->setToolTip("Path to libretro core file (.so/.dll/.dylib)");
    ui->launchParamsLineEdit->setToolTip("Additional libretro frontend parameters");
  } else {
    ui->launchParamsLineEdit->setToolTip("Additional command-line parameters for the launcher");
  }

  // Update extract archives visibility based on launcher type
  updateExtractArchivesVisibility();
}

void SettingsDialog::onContentDirectoryChanged() {
  updateFieldVisibility();
  checkForChanges();
}

void SettingsDialog::updateFieldVisibility() {
  bool hasContentDir = !ui->mediaDirLineEdit->text().trimmed().isEmpty();

  ui->label_launcher->setVisible(hasContentDir);
  ui->launcherLineEdit->setVisible(hasContentDir);
  ui->browseLauncherButton->setVisible(hasContentDir);
  ui->label_launchParams->setVisible(hasContentDir);
  ui->launchParamsLineEdit->setVisible(hasContentDir);
  ui->label_fileExtensions->setVisible(hasContentDir);
  ui->fileExtensionsLineEdit->setVisible(hasContentDir);

  // Artwork directory is always visible - shell collections can set artwork
  // for subcollections that inherit from the parent
  ui->label_artworkDir->setVisible(true);
  ui->artworkDirLineEdit->setVisible(true);
  ui->browseArtworkDirButton->setVisible(true);
  ui->label_videoDir->setVisible(true);
  ui->videoDirLineEdit->setVisible(true);
  ui->browseVideoDirButton->setVisible(true);
  ui->label_placeholderArtwork->setVisible(true);
  ui->placeholderArtworkLineEdit->setVisible(true);
  ui->browsePlaceholderArtworkButton->setVisible(true);

  if (hasContentDir) {
    updateUIForLauncherType(ui->launcherLineEdit->text());
  } else {
    ui->label_core->setVisible(false);
    ui->coreLineEdit->setVisible(false);
    ui->browseCoreButton->setVisible(false);
  }
  // Archive Handling stays visible regardless of content dir / launcher type
  // so the user can toggle the option freely.
  updateExtractArchivesVisibility();

  ui->label_sidebarMode->setVisible(true);
  ui->sidebarModeComboBox->setVisible(true);
}

void SettingsDialog::updateExtractArchivesVisibility() {
  // Archive Handling section is always visible — historically gated to
  // libretro frontends, but the user wants the toggle accessible regardless
  // of launcher type.
  ui->label_extractArchives->setVisible(true);
  ui->extractArchivesCheckBox->setVisible(true);

  // Launch Extension is meaningful only when extraction is enabled.
  bool extractEnabled = ui->extractArchivesCheckBox->isChecked();
  ui->label_extractedExtension->setVisible(extractEnabled);
  ui->extractedExtensionLineEdit->setVisible(extractEnabled);

  if (ui->launcherArchiveGroupBox) {
    ui->launcherArchiveGroupBox->setVisible(true);
  }
}

void SettingsDialog::onExtractArchivesToggled(bool checked) {
  Q_UNUSED(checked)
  updateExtractArchivesVisibility();
}

void SettingsDialog::updateSidebarModeVisibility() {
  ui->label_sidebarMode->setVisible(true);
  ui->sidebarModeComboBox->setVisible(true);

  // width-vs-height field visibility tracks the position combo.
  // Right/Left expose Width; Top/Bottom expose Height. The lock checkbox
  // governs both directions so it stays visible regardless.
  if (ui->sidebarPositionComboBox) {
    const auto pos = static_cast<DetailsPanePosition>(ui->sidebarPositionComboBox->currentIndex());
    const bool horizontalDock = CollectionUtils::isDetailsPaneHorizontal(pos);
    if (ui->label_sidebarWidth) ui->label_sidebarWidth->setVisible(!horizontalDock);
    if (ui->sidebarWidthSpinBox) ui->sidebarWidthSpinBox->setVisible(!horizontalDock);
    if (ui->label_sidebarHeight) ui->label_sidebarHeight->setVisible(horizontalDock);
    if (ui->sidebarHeightSpinBox) ui->sidebarHeightSpinBox->setVisible(horizontalDock);
  }
}

void SettingsDialog::updateGridWidthLimits() {
  if (!ui->gridWidthSpinBox) {
    return;
  }
  int preservedValue = ui->gridWidthSpinBox->value();
  ui->gridWidthSpinBox->setMaximum(UIConstants::Grid::MAX_WIDTH);
  ui->gridWidthSpinBox->setValue(preservedValue);
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
  SettingsFormBinding::loadInto(ui->rememberSelectionCheckBox, m_generalSettings.rememberSelection);
  SettingsFormBinding::loadInto(ui->wrapNavigationCheckBox, m_generalSettings.wrapNavigation);
  SettingsFormBinding::loadInto(ui->selectItemOnHoverCheckBox, m_generalSettings.selectItemOnHover);
  SettingsFormBinding::loadInto(ui->showTitleInPlaceholderCheckBox,
                                m_generalSettings.showTitleInPlaceholder);
  SettingsFormBinding::loadInto(ui->bootSplashCheckBox, m_generalSettings.bootSplashEnabled);
  // startup video
  SettingsFormBinding::loadInto(ui->startupVideoEnabledCheckBox,
                                m_generalSettings.startupVideoEnabled);
  SettingsFormBinding::loadInto(ui->startupVideoPathLineEdit, m_generalSettings.startupVideoPath);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashCheckBox,
                                m_generalSettings.resumeFocusSplashEnabled);
  SettingsFormBinding::loadInto(ui->bootSplashTitleLineEdit, m_generalSettings.bootSplashTitle);
  SettingsFormBinding::loadInto(ui->bootSplashSubtitleLineEdit,
                                m_generalSettings.bootSplashSubtitle);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashTitleLineEdit,
                                m_generalSettings.resumeFocusSplashTitle);
  SettingsFormBinding::loadInto(ui->resumeFocusSplashSubtitleLineEdit,
                                m_generalSettings.resumeFocusSplashSubtitle);
  SettingsFormBinding::loadInto(ui->runtimeDetectionCheckBox,
                                m_generalSettings.runtimeDetectionEnabled);
  SettingsFormBinding::loadInto(ui->historyEnabledCheckBox, m_generalSettings.historyEnabled);
  SettingsFormBinding::loadInto(ui->historyMaxEntriesSpinBox, m_generalSettings.historyMaxEntries);
  SettingsFormBinding::loadInto(ui->pixmapCacheSpinBox, m_generalSettings.pixmapCacheSizeMB);
  SettingsFormBinding::loadInto(ui->videoThumbnailTimeoutSpinBox,
                                m_generalSettings.videoThumbnailExtractionTimeoutMs);
  SettingsFormBinding::loadInto(ui->keyboardSpeedSpinBox,
                                m_generalSettings.keyboardRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->keyboardRepeatDelaySpinBox,
                                m_generalSettings.keyboardRepeatDelayMs);
  SettingsFormBinding::loadInto(ui->clickHoldDelaySpinBox, m_generalSettings.clickHoldDelayMs);
  SettingsFormBinding::loadInto(ui->clickHoldRepeatIntervalSpinBox,
                                m_generalSettings.clickHoldRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->listKeyboardRepeatSpinBox,
                                m_generalSettings.listKeyboardRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->listClickHoldRepeatSpinBox,
                                m_generalSettings.listClickHoldRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->mouseWheelSpeedSpinBox, m_generalSettings.mouseWheelRows);
  SettingsFormBinding::loadInto(ui->scrollAnimationSpeedSpinBox,
                                m_generalSettings.scrollAnimationDurationMs);
  SettingsFormBinding::loadInto(ui->scrollVelocityMultiplierSpinBox,
                                m_generalSettings.scrollVelocityMultiplier);
  SettingsFormBinding::loadInto(ui->titleSaturationSpinBox, m_generalSettings.titleTintSaturation);
  SettingsFormBinding::loadInto(ui->titleLightnessSpinBox, m_generalSettings.titleTintLightness);
  SettingsFormBinding::loadInto(ui->baseColorEdit, m_generalSettings.titleBaseColor);
  // global UI font controls
  SettingsFormBinding::loadInto(ui->globalUiFontFamilyEdit, m_generalSettings.globalUiFontFamily);
  SettingsFormBinding::loadInto(ui->globalUiFontSizeSpinBox,
                                m_generalSettings.globalUiFontPointSize);
  SettingsFormBinding::loadInto(ui->attractModeCheckBox, m_generalSettings.attractModeEnabled);
  SettingsFormBinding::loadInto(ui->attractIdleTimeoutSpinBox,
                                m_generalSettings.attractModeIdleTimeoutSec);
  SettingsFormBinding::loadInto(ui->attractAutoScrollCheckBox,
                                m_generalSettings.attractModeAutoScrollEnabled);
  SettingsFormBinding::loadInto(ui->attractScrollSpeedSpinBox,
                                m_generalSettings.attractModeScrollSpeed);
  SettingsFormBinding::loadInto(ui->attractAdvanceSelectionCheckBox,
                                m_generalSettings.attractModeAdvanceSelectionEnabled);
  SettingsFormBinding::loadInto(ui->attractAdvanceIntervalSpinBox,
                                m_generalSettings.attractModeAdvanceSelectionIntervalSec);
  SettingsFormBinding::loadInto(ui->attractAdvanceRandomCheckBox,
                                m_generalSettings.attractModeAdvanceSelectionRandom);
  if (ui->startupCollectionComboBox) {
    ui->startupCollectionComboBox->blockSignals(true);
    ui->startupCollectionComboBox->clear();
    ui->startupCollectionComboBox->addItem(tr("(Default)"), QString());
    if (mainWindow) {
      for (const CollectionConfig &cfg : std::as_const(mainWindow->m_collections)) {
        ui->startupCollectionComboBox->addItem(cfg.name, cfg.name);
      }
    }
    int idx = ui->startupCollectionComboBox->findData(m_generalSettings.startupCollection);
    ui->startupCollectionComboBox->setCurrentIndex(idx >= 0 ? idx : 0);
    ui->startupCollectionComboBox->blockSignals(false);
  }
  SettingsFormBinding::loadInto(ui->useHomeViewCheckBox, m_generalSettings.useHomeView);
  SettingsFormBinding::loadInto(ui->homeViewLabelLineEdit, m_generalSettings.homeViewLabel);
  SettingsFormBinding::loadInto(ui->homeViewIconLineEdit, m_generalSettings.homeViewIcon);
  // Note: customFontEdit is now loaded per-collection in loadCollectionFields()

  auto setKeyEdit = [](QKeySequenceEdit *edit, int key) {
    if (!edit) {
      return;
    }
    edit->blockSignals(true);
    edit->setKeySequence(QKeySequence(key));
    edit->blockSignals(false);
  };

  setKeyEdit(ui->keyNavUpEdit, m_generalSettings.keyNavUp);
  setKeyEdit(ui->keyNavDownEdit, m_generalSettings.keyNavDown);
  setKeyEdit(ui->keyNavLeftEdit, m_generalSettings.keyNavLeft);
  setKeyEdit(ui->keyNavRightEdit, m_generalSettings.keyNavRight);
  setKeyEdit(ui->keyConfirmEdit, m_generalSettings.keyConfirm);
  setKeyEdit(ui->keyBackEdit, m_generalSettings.keyBack);
  setKeyEdit(ui->keySearchEdit, m_generalSettings.keySearch);
  setKeyEdit(ui->keyHomeViewEdit, m_generalSettings.keyHomeView);

  SettingsFormBinding::loadInto(ui->gamepadUseDpadCheckBox, m_generalSettings.gamepadUseDpad);
  SettingsFormBinding::loadInto(ui->gamepadUseLeftStickCheckBox,
                                m_generalSettings.gamepadUseLeftStick);
  SettingsFormBinding::loadInto(ui->gamepadConfirmButtonLineEdit,
                                m_generalSettings.gamepadConfirmButton);
  SettingsFormBinding::loadInto(ui->gamepadBackButtonLineEdit, m_generalSettings.gamepadBackButton);
  SettingsFormBinding::loadInto(ui->gamepadToggleSidebarButtonLineEdit,
                                m_generalSettings.gamepadToggleSidebarButton);

  // artwork-cycle modifier dropdown. Populated lazily on first
  // load so a freshly opened dialog reflects whatever the user picked last
  // session. Order matches the order users tend to reach for: Shift first,
  // Meta last (Win/Cmd is the most likely to clash with a global shortcut).
  if (ui->artworkCycleModifierComboBox) {
    QSignalBlocker blocker(ui->artworkCycleModifierComboBox);
    if (ui->artworkCycleModifierComboBox->count() == 0) {
      ui->artworkCycleModifierComboBox->addItem(tr("Shift"), static_cast<int>(Qt::ShiftModifier));
      ui->artworkCycleModifierComboBox->addItem(tr("Ctrl"), static_cast<int>(Qt::ControlModifier));
      ui->artworkCycleModifierComboBox->addItem(tr("Alt"), static_cast<int>(Qt::AltModifier));
      ui->artworkCycleModifierComboBox->addItem(tr("Meta (Win/Cmd)"),
                                                static_cast<int>(Qt::MetaModifier));
    }
    int comboIdx =
        ui->artworkCycleModifierComboBox->findData(m_generalSettings.artworkCycleModifier);
    ui->artworkCycleModifierComboBox->setCurrentIndex(comboIdx >= 0 ? comboIdx : 0);
  }

  // load toolbar customization controls.
  auto setToolbarCheck = [](QCheckBox *box, bool value) {
    if (!box) {
      return;
    }
    QSignalBlocker blocker(box);
    box->setChecked(value);
  };
  auto setToolbarText = [](QLineEdit *edit, const QString &value) {
    if (!edit) {
      return;
    }
    QSignalBlocker blocker(edit);
    edit->setText(value);
  };
  setToolbarCheck(ui->toolbarGridViewVisibleCheckBox, m_generalSettings.toolbarShowGridViewButton);
  setToolbarCheck(ui->toolbarListViewVisibleCheckBox, m_generalSettings.toolbarShowListViewButton);
  setToolbarCheck(ui->toolbarCoverFlowViewVisibleCheckBox,
                  m_generalSettings.toolbarShowCoverFlowViewButton);
  setToolbarCheck(ui->toolbarHorizontalViewVisibleCheckBox,
                  m_generalSettings.toolbarShowHorizontalViewButton);
  setToolbarCheck(ui->toolbarHideSubcollectionsVisibleCheckBox,
                  m_generalSettings.toolbarShowHideSubcollectionsButton);
  setToolbarCheck(ui->toolbarTypeFilterVisibleCheckBox, m_generalSettings.toolbarShowTypeFilter);
  setToolbarCheck(ui->toolbarTitleFilterVisibleCheckBox, m_generalSettings.toolbarShowTitleFilter);
  setToolbarCheck(ui->toolbarSearchModeVisibleCheckBox,
                  m_generalSettings.toolbarShowSearchModeButton);
  setToolbarCheck(ui->toolbarSearchBarVisibleCheckBox, m_generalSettings.toolbarShowSearchBar);
  setToolbarText(ui->toolbarGridViewTextEdit, m_generalSettings.toolbarGridViewButtonText);
  setToolbarText(ui->toolbarListViewTextEdit, m_generalSettings.toolbarListViewButtonText);
  setToolbarText(ui->toolbarCoverFlowViewTextEdit,
                 m_generalSettings.toolbarCoverFlowViewButtonText);
  setToolbarText(ui->toolbarHorizontalViewTextEdit,
                 m_generalSettings.toolbarHorizontalViewButtonText);
  setToolbarText(ui->toolbarHideSubcollectionsTextEdit,
                 m_generalSettings.toolbarHideSubcollectionsButtonText);
  setToolbarText(ui->toolbarTitleFilterTextEdit, m_generalSettings.toolbarTitleFilterText);

  // Store original general settings for change detection
  m_originalGeneralSettings = m_generalSettings;

  // hydrate the launcher-presets list from the loaded general
  // settings. Done after m_originalGeneralSettings is captured so the change
  // detector can compare the live presets against the saved baseline.
  loadLauncherPresetsToUI();

  if (m_gamepadCapture) {
    m_gamepadCapture->refreshUi();
  }
}

void SettingsDialog::saveGeneralSettingsFromUI() {
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if ((mainWindow) && (mainWindow->getSettingsManager())) {
    SettingsFormBinding::saveFrom(ui->rememberSelectionCheckBox,
                                  mainWindow->m_generalSettings.rememberSelection);
    SettingsFormBinding::saveFrom(ui->wrapNavigationCheckBox,
                                  mainWindow->m_generalSettings.wrapNavigation);
    SettingsFormBinding::saveFrom(ui->selectItemOnHoverCheckBox,
                                  mainWindow->m_generalSettings.selectItemOnHover);
    SettingsFormBinding::saveFrom(ui->bootSplashCheckBox,
                                  mainWindow->m_generalSettings.bootSplashEnabled);
    // startup video
    SettingsFormBinding::saveFrom(ui->startupVideoEnabledCheckBox,
                                  mainWindow->m_generalSettings.startupVideoEnabled);
    SettingsFormBinding::saveFrom(ui->startupVideoPathLineEdit,
                                  mainWindow->m_generalSettings.startupVideoPath);
    SettingsFormBinding::saveFrom(ui->resumeFocusSplashCheckBox,
                                  mainWindow->m_generalSettings.resumeFocusSplashEnabled);
    SettingsFormBinding::saveFrom(ui->bootSplashTitleLineEdit,
                                  mainWindow->m_generalSettings.bootSplashTitle);
    SettingsFormBinding::saveFrom(ui->bootSplashSubtitleLineEdit,
                                  mainWindow->m_generalSettings.bootSplashSubtitle);
    SettingsFormBinding::saveFrom(ui->resumeFocusSplashTitleLineEdit,
                                  mainWindow->m_generalSettings.resumeFocusSplashTitle);
    SettingsFormBinding::saveFrom(ui->resumeFocusSplashSubtitleLineEdit,
                                  mainWindow->m_generalSettings.resumeFocusSplashSubtitle);
    if (ui->pixmapCacheSpinBox) {
      int newCacheSize = ui->pixmapCacheSpinBox->value();
      mainWindow->m_generalSettings.pixmapCacheSizeMB = newCacheSize;
      // Apply immediately (in KB)
      QPixmapCache::setCacheLimit(newCacheSize * 1024);
    }
    if (ui->videoThumbnailTimeoutSpinBox) {
      int newTimeout = ui->videoThumbnailTimeoutSpinBox->value();
      mainWindow->m_generalSettings.videoThumbnailExtractionTimeoutMs = newTimeout;
      VideoThumbnailExtractor::instance()->setExtractionTimeoutMs(newTimeout);
    }
    SettingsFormBinding::saveFrom(ui->keyboardSpeedSpinBox,
                                  mainWindow->m_generalSettings.keyboardRepeatIntervalMs);
    SettingsFormBinding::saveFrom(ui->keyboardRepeatDelaySpinBox,
                                  mainWindow->m_generalSettings.keyboardRepeatDelayMs);
    SettingsFormBinding::saveFrom(ui->clickHoldDelaySpinBox,
                                  mainWindow->m_generalSettings.clickHoldDelayMs);
    SettingsFormBinding::saveFrom(ui->clickHoldRepeatIntervalSpinBox,
                                  mainWindow->m_generalSettings.clickHoldRepeatIntervalMs);
    SettingsFormBinding::saveFrom(ui->listKeyboardRepeatSpinBox,
                                  mainWindow->m_generalSettings.listKeyboardRepeatIntervalMs);
    SettingsFormBinding::saveFrom(ui->listClickHoldRepeatSpinBox,
                                  mainWindow->m_generalSettings.listClickHoldRepeatIntervalMs);
    SettingsFormBinding::saveFrom(ui->mouseWheelSpeedSpinBox,
                                  mainWindow->m_generalSettings.mouseWheelRows);
    SettingsFormBinding::saveFrom(ui->scrollAnimationSpeedSpinBox,
                                  mainWindow->m_generalSettings.scrollAnimationDurationMs);
    SettingsFormBinding::saveFrom(ui->scrollVelocityMultiplierSpinBox,
                                  mainWindow->m_generalSettings.scrollVelocityMultiplier);
    SettingsFormBinding::saveFrom(ui->attractModeCheckBox,
                                  mainWindow->m_generalSettings.attractModeEnabled);
    SettingsFormBinding::saveFrom(ui->attractIdleTimeoutSpinBox,
                                  mainWindow->m_generalSettings.attractModeIdleTimeoutSec);
    SettingsFormBinding::saveFrom(ui->attractAutoScrollCheckBox,
                                  mainWindow->m_generalSettings.attractModeAutoScrollEnabled);
    SettingsFormBinding::saveFrom(ui->attractScrollSpeedSpinBox,
                                  mainWindow->m_generalSettings.attractModeScrollSpeed);
    SettingsFormBinding::saveFrom(ui->attractAdvanceSelectionCheckBox,
                                  mainWindow->m_generalSettings.attractModeAdvanceSelectionEnabled);
    SettingsFormBinding::saveFrom(
        ui->attractAdvanceIntervalSpinBox,
        mainWindow->m_generalSettings.attractModeAdvanceSelectionIntervalSec);
    SettingsFormBinding::saveFrom(ui->attractAdvanceRandomCheckBox,
                                  mainWindow->m_generalSettings.attractModeAdvanceSelectionRandom);
    if (ui->titleSaturationSpinBox) {
      mainWindow->m_generalSettings.titleTintSaturation = ui->titleSaturationSpinBox->value();
      // Apply to ItemWidget static settings
      ItemWidget::setTitleTintSaturation(ui->titleSaturationSpinBox->value());
    }
    if (ui->titleLightnessSpinBox) {
      mainWindow->m_generalSettings.titleTintLightness = ui->titleLightnessSpinBox->value();
      // Apply to ItemWidget static settings
      ItemWidget::setTitleTintLightness(ui->titleLightnessSpinBox->value());
    }
    if (ui->baseColorEdit) {
      mainWindow->m_generalSettings.titleBaseColor = ui->baseColorEdit->text().trimmed();
      // Apply to ItemWidget static settings
      ItemWidget::setTitleBaseColor(ui->baseColorEdit->text().trimmed());
    }
    if (ui->startupCollectionComboBox) {
      mainWindow->m_generalSettings.startupCollection =
          ui->startupCollectionComboBox->currentData().toString();
    }
    if (ui->useHomeViewCheckBox) {
      mainWindow->m_generalSettings.useHomeView = ui->useHomeViewCheckBox->isChecked();
    }
    if (ui->homeViewLabelLineEdit) {
      mainWindow->m_generalSettings.homeViewLabel = ui->homeViewLabelLineEdit->text().trimmed();
    }
    if (ui->homeViewIconLineEdit) {
      mainWindow->m_generalSettings.homeViewIcon = ui->homeViewIconLineEdit->text().trimmed();
    }
    // Note: customFontFamily is now saved per-collection, not in general
    // settings

    auto singleKeyFromEdit = [](QKeySequenceEdit *edit, int fallbackKey) -> int {
      if (!edit) {
        return fallbackKey;
      }
      const QKeySequence seq = edit->keySequence();
      if (seq.isEmpty()) {
        return fallbackKey;
      }
      const auto combo = seq[0];
      const int keyOnly = static_cast<int>(combo.key());
      return (keyOnly != 0) ? keyOnly : fallbackKey;
    };

    mainWindow->m_generalSettings.keyNavUp =
        singleKeyFromEdit(ui->keyNavUpEdit, mainWindow->m_generalSettings.keyNavUp);
    mainWindow->m_generalSettings.keyNavDown =
        singleKeyFromEdit(ui->keyNavDownEdit, mainWindow->m_generalSettings.keyNavDown);
    mainWindow->m_generalSettings.keyNavLeft =
        singleKeyFromEdit(ui->keyNavLeftEdit, mainWindow->m_generalSettings.keyNavLeft);
    mainWindow->m_generalSettings.keyNavRight =
        singleKeyFromEdit(ui->keyNavRightEdit, mainWindow->m_generalSettings.keyNavRight);
    mainWindow->m_generalSettings.keyConfirm =
        singleKeyFromEdit(ui->keyConfirmEdit, mainWindow->m_generalSettings.keyConfirm);
    mainWindow->m_generalSettings.keyBack =
        singleKeyFromEdit(ui->keyBackEdit, mainWindow->m_generalSettings.keyBack);
    mainWindow->m_generalSettings.keySearch =
        singleKeyFromEdit(ui->keySearchEdit, mainWindow->m_generalSettings.keySearch);
    // Pass 0 as the fallback so clearing the field actually unbinds the
    // shortcut — the rest of the keybinds revert to their previous value
    // because they have meaningful defaults; Home has no default.
    mainWindow->m_generalSettings.keyHomeView = singleKeyFromEdit(ui->keyHomeViewEdit, 0);

    SettingsFormBinding::saveFrom(ui->gamepadUseDpadCheckBox,
                                  mainWindow->m_generalSettings.gamepadUseDpad);
    SettingsFormBinding::saveFrom(ui->gamepadUseLeftStickCheckBox,
                                  mainWindow->m_generalSettings.gamepadUseLeftStick);
    if (ui->gamepadConfirmButtonLineEdit) {
      const QString v = ui->gamepadConfirmButtonLineEdit->text().trimmed();
      if (!v.isEmpty()) {
        mainWindow->m_generalSettings.gamepadConfirmButton = v;
      }
    }
    if (ui->gamepadBackButtonLineEdit) {
      const QString v = ui->gamepadBackButtonLineEdit->text().trimmed();
      if (!v.isEmpty()) {
        mainWindow->m_generalSettings.gamepadBackButton = v;
      }
    }
    if (ui->gamepadToggleSidebarButtonLineEdit) {
      const QString v = ui->gamepadToggleSidebarButtonLineEdit->text().trimmed();
      if (!v.isEmpty()) {
        mainWindow->m_generalSettings.gamepadToggleSidebarButton = v;
      }
    }
    if (ui->artworkCycleModifierComboBox) {
      const int rawModifier = ui->artworkCycleModifierComboBox->currentData().toInt();
      switch (rawModifier) {
      case static_cast<int>(Qt::ShiftModifier):
      case static_cast<int>(Qt::ControlModifier):
      case static_cast<int>(Qt::AltModifier):
      case static_cast<int>(Qt::MetaModifier):
        mainWindow->m_generalSettings.artworkCycleModifier = rawModifier;
        break;
      default:
        break; // leave the existing value untouched on a stale combo entry
      }
    }
    // launcher presets live on the dialog's m_generalSettings
    // (mutated directly by the Launchers tab) — copy them onto the main
    // window's settings before persisting so the saved snapshot includes
    // any preset add/edit/remove the user just performed.
    mainWindow->m_generalSettings.launcherPresets = m_generalSettings.launcherPresets;

    // pull customizable-toolbar fields off the dialog controls.
    SettingsFormBinding::saveFrom(ui->toolbarGridViewVisibleCheckBox,
                                  mainWindow->m_generalSettings.toolbarShowGridViewButton);
    SettingsFormBinding::saveFrom(ui->toolbarListViewVisibleCheckBox,
                                  mainWindow->m_generalSettings.toolbarShowListViewButton);
    SettingsFormBinding::saveFrom(ui->toolbarCoverFlowViewVisibleCheckBox,
                                  mainWindow->m_generalSettings.toolbarShowCoverFlowViewButton);
    SettingsFormBinding::saveFrom(ui->toolbarHorizontalViewVisibleCheckBox,
                                  mainWindow->m_generalSettings.toolbarShowHorizontalViewButton);
    SettingsFormBinding::saveFrom(
        ui->toolbarHideSubcollectionsVisibleCheckBox,
        mainWindow->m_generalSettings.toolbarShowHideSubcollectionsButton);
    SettingsFormBinding::saveFrom(ui->toolbarTypeFilterVisibleCheckBox,
                                  mainWindow->m_generalSettings.toolbarShowTypeFilter);
    SettingsFormBinding::saveFrom(ui->toolbarTitleFilterVisibleCheckBox,
                                  mainWindow->m_generalSettings.toolbarShowTitleFilter);
    SettingsFormBinding::saveFrom(ui->toolbarSearchModeVisibleCheckBox,
                                  mainWindow->m_generalSettings.toolbarShowSearchModeButton);
    SettingsFormBinding::saveFrom(ui->toolbarSearchBarVisibleCheckBox,
                                  mainWindow->m_generalSettings.toolbarShowSearchBar);
    SettingsFormBinding::saveFrom(ui->toolbarGridViewTextEdit,
                                  mainWindow->m_generalSettings.toolbarGridViewButtonText,
                                  /*trim=*/false);
    SettingsFormBinding::saveFrom(ui->toolbarListViewTextEdit,
                                  mainWindow->m_generalSettings.toolbarListViewButtonText,
                                  /*trim=*/false);
    SettingsFormBinding::saveFrom(ui->toolbarCoverFlowViewTextEdit,
                                  mainWindow->m_generalSettings.toolbarCoverFlowViewButtonText,
                                  /*trim=*/false);
    SettingsFormBinding::saveFrom(ui->toolbarHorizontalViewTextEdit,
                                  mainWindow->m_generalSettings.toolbarHorizontalViewButtonText,
                                  /*trim=*/false);
    SettingsFormBinding::saveFrom(ui->toolbarHideSubcollectionsTextEdit,
                                  mainWindow->m_generalSettings.toolbarHideSubcollectionsButtonText,
                                  /*trim=*/false);
    SettingsFormBinding::saveFrom(ui->toolbarTitleFilterTextEdit,
                                  mainWindow->m_generalSettings.toolbarTitleFilterText,
                                  /*trim=*/false);

    mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
    m_generalSettings = mainWindow->m_generalSettings;

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
