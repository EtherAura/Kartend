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

#include "extensionutils.h"
#include "attractmanager.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

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
  // Kartend-9f6: illuminate the save icon while there are unsaved changes.
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
  bool isRetroArch = launcherPath.contains("retroarch", Qt::CaseInsensitive);
  bool showCore = hasContentDir && isRetroArch;
  ui->coreLineEdit->setVisible(showCore);
  ui->browseCoreButton->setVisible(showCore);
  ui->label_core->setVisible(showCore);
  if (isRetroArch) {
    ui->coreLineEdit->setToolTip("Path to RetroArch core file (.so/.dll/.dylib)");
    ui->launchParamsLineEdit->setToolTip("Additional RetroArch parameters");
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
    updateExtractArchivesVisibility();
  } else {
    ui->label_core->setVisible(false);
    ui->coreLineEdit->setVisible(false);
    ui->browseCoreButton->setVisible(false);
    ui->label_extractArchives->setVisible(false);
    ui->extractArchivesCheckBox->setVisible(false);
    ui->label_extractedExtension->setVisible(false);
    ui->extractedExtensionLineEdit->setVisible(false);
  }

  ui->label_sidebarMode->setVisible(true);
  ui->sidebarModeComboBox->setVisible(true);
}

void SettingsDialog::updateExtractArchivesVisibility() {
  bool isRetroArch = ui->launcherLineEdit->text().contains("retroarch", Qt::CaseInsensitive);
  bool extractEnabled = ui->extractArchivesCheckBox->isChecked();

  // Show extract archives option only for RetroArch launchers
  ui->label_extractArchives->setVisible(isRetroArch);
  ui->extractArchivesCheckBox->setVisible(isRetroArch);

  // Show extracted extension field only when extraction is enabled
  ui->label_extractedExtension->setVisible(isRetroArch && extractEnabled);
  ui->extractedExtensionLineEdit->setVisible(isRetroArch && extractEnabled);
}

void SettingsDialog::onExtractArchivesToggled(bool checked) {
  Q_UNUSED(checked)
  updateExtractArchivesVisibility();
}

void SettingsDialog::updateSidebarModeVisibility() {
  ui->label_sidebarMode->setVisible(true);
  ui->sidebarModeComboBox->setVisible(true);
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
  if (ui->rememberSelectionCheckBox) {
    ui->rememberSelectionCheckBox->blockSignals(true);
    ui->rememberSelectionCheckBox->setChecked(m_generalSettings.rememberSelection);
    ui->rememberSelectionCheckBox->blockSignals(false);
  }
  if (ui->wrapNavigationCheckBox) {
    ui->wrapNavigationCheckBox->blockSignals(true);
    ui->wrapNavigationCheckBox->setChecked(m_generalSettings.wrapNavigation);
    ui->wrapNavigationCheckBox->blockSignals(false);
  }
  if (ui->selectItemOnHoverCheckBox) {
    ui->selectItemOnHoverCheckBox->blockSignals(true);
    ui->selectItemOnHoverCheckBox->setChecked(m_generalSettings.selectItemOnHover);
    ui->selectItemOnHoverCheckBox->blockSignals(false);
  }
  if (ui->bootSplashCheckBox) {
    ui->bootSplashCheckBox->blockSignals(true);
    ui->bootSplashCheckBox->setChecked(m_generalSettings.bootSplashEnabled);
    ui->bootSplashCheckBox->blockSignals(false);
  }
  if (ui->resumeFocusSplashCheckBox) {
    ui->resumeFocusSplashCheckBox->blockSignals(true);
    ui->resumeFocusSplashCheckBox->setChecked(m_generalSettings.resumeFocusSplashEnabled);
    ui->resumeFocusSplashCheckBox->blockSignals(false);
  }
  if (ui->runtimeDetectionCheckBox) {
    ui->runtimeDetectionCheckBox->blockSignals(true);
    ui->runtimeDetectionCheckBox->setChecked(m_generalSettings.runtimeDetectionEnabled);
    ui->runtimeDetectionCheckBox->blockSignals(false);
  }
  if (ui->historyEnabledCheckBox) {
    ui->historyEnabledCheckBox->blockSignals(true);
    ui->historyEnabledCheckBox->setChecked(m_generalSettings.historyEnabled);
    ui->historyEnabledCheckBox->blockSignals(false);
  }
  if (ui->historyMaxEntriesSpinBox) {
    ui->historyMaxEntriesSpinBox->blockSignals(true);
    ui->historyMaxEntriesSpinBox->setValue(m_generalSettings.historyMaxEntries);
    ui->historyMaxEntriesSpinBox->blockSignals(false);
  }
  if (ui->pixmapCacheSpinBox) {
    ui->pixmapCacheSpinBox->blockSignals(true);
    ui->pixmapCacheSpinBox->setValue(m_generalSettings.pixmapCacheSizeMB);
    ui->pixmapCacheSpinBox->blockSignals(false);
  }
  if (ui->keyboardSpeedSpinBox) {
    ui->keyboardSpeedSpinBox->blockSignals(true);
    ui->keyboardSpeedSpinBox->setValue(m_generalSettings.keyboardRepeatIntervalMs);
    ui->keyboardSpeedSpinBox->blockSignals(false);
  }
  if (ui->keyboardRepeatDelaySpinBox) {
    ui->keyboardRepeatDelaySpinBox->blockSignals(true);
    ui->keyboardRepeatDelaySpinBox->setValue(m_generalSettings.keyboardRepeatDelayMs);
    ui->keyboardRepeatDelaySpinBox->blockSignals(false);
  }
  if (ui->clickHoldDelaySpinBox) {
    ui->clickHoldDelaySpinBox->blockSignals(true);
    ui->clickHoldDelaySpinBox->setValue(m_generalSettings.clickHoldDelayMs);
    ui->clickHoldDelaySpinBox->blockSignals(false);
  }
  if (ui->clickHoldRepeatIntervalSpinBox) {
    ui->clickHoldRepeatIntervalSpinBox->blockSignals(true);
    ui->clickHoldRepeatIntervalSpinBox->setValue(m_generalSettings.clickHoldRepeatIntervalMs);
    ui->clickHoldRepeatIntervalSpinBox->blockSignals(false);
  }
  if (ui->listKeyboardRepeatSpinBox) {
    ui->listKeyboardRepeatSpinBox->blockSignals(true);
    ui->listKeyboardRepeatSpinBox->setValue(m_generalSettings.listKeyboardRepeatIntervalMs);
    ui->listKeyboardRepeatSpinBox->blockSignals(false);
  }
  if (ui->listClickHoldRepeatSpinBox) {
    ui->listClickHoldRepeatSpinBox->blockSignals(true);
    ui->listClickHoldRepeatSpinBox->setValue(m_generalSettings.listClickHoldRepeatIntervalMs);
    ui->listClickHoldRepeatSpinBox->blockSignals(false);
  }
  if (ui->mouseWheelSpeedSpinBox) {
    ui->mouseWheelSpeedSpinBox->blockSignals(true);
    ui->mouseWheelSpeedSpinBox->setValue(m_generalSettings.mouseWheelRows);
    ui->mouseWheelSpeedSpinBox->blockSignals(false);
  }
  if (ui->scrollAnimationSpeedSpinBox) {
    ui->scrollAnimationSpeedSpinBox->blockSignals(true);
    ui->scrollAnimationSpeedSpinBox->setValue(m_generalSettings.scrollAnimationDurationMs);
    ui->scrollAnimationSpeedSpinBox->blockSignals(false);
  }
  if (ui->scrollVelocityMultiplierSpinBox) {
    ui->scrollVelocityMultiplierSpinBox->blockSignals(true);
    ui->scrollVelocityMultiplierSpinBox->setValue(m_generalSettings.scrollVelocityMultiplier);
    ui->scrollVelocityMultiplierSpinBox->blockSignals(false);
  }
  if (ui->titleSaturationSpinBox) {
    ui->titleSaturationSpinBox->blockSignals(true);
    ui->titleSaturationSpinBox->setValue(m_generalSettings.titleTintSaturation);
    ui->titleSaturationSpinBox->blockSignals(false);
  }
  if (ui->titleLightnessSpinBox) {
    ui->titleLightnessSpinBox->blockSignals(true);
    ui->titleLightnessSpinBox->setValue(m_generalSettings.titleTintLightness);
    ui->titleLightnessSpinBox->blockSignals(false);
  }
  if (ui->baseColorEdit) {
    ui->baseColorEdit->blockSignals(true);
    ui->baseColorEdit->setText(m_generalSettings.titleBaseColor);
    ui->baseColorEdit->blockSignals(false);
  }
  // Kartend-9v0o: global UI font controls
  if (ui->globalUiFontFamilyEdit) {
    ui->globalUiFontFamilyEdit->blockSignals(true);
    ui->globalUiFontFamilyEdit->setText(m_generalSettings.globalUiFontFamily);
    ui->globalUiFontFamilyEdit->blockSignals(false);
  }
  if (ui->globalUiFontSizeSpinBox) {
    ui->globalUiFontSizeSpinBox->blockSignals(true);
    ui->globalUiFontSizeSpinBox->setValue(m_generalSettings.globalUiFontPointSize);
    ui->globalUiFontSizeSpinBox->blockSignals(false);
  }
  if (ui->attractModeCheckBox) {
    ui->attractModeCheckBox->blockSignals(true);
    ui->attractModeCheckBox->setChecked(m_generalSettings.attractModeEnabled);
    ui->attractModeCheckBox->blockSignals(false);
  }
  if (ui->attractIdleTimeoutSpinBox) {
    ui->attractIdleTimeoutSpinBox->blockSignals(true);
    ui->attractIdleTimeoutSpinBox->setValue(m_generalSettings.attractModeIdleTimeoutSec);
    ui->attractIdleTimeoutSpinBox->blockSignals(false);
  }
  if (ui->attractAutoScrollCheckBox) {
    ui->attractAutoScrollCheckBox->blockSignals(true);
    ui->attractAutoScrollCheckBox->setChecked(m_generalSettings.attractModeAutoScrollEnabled);
    ui->attractAutoScrollCheckBox->blockSignals(false);
  }
  if (ui->attractScrollSpeedSpinBox) {
    ui->attractScrollSpeedSpinBox->blockSignals(true);
    ui->attractScrollSpeedSpinBox->setValue(m_generalSettings.attractModeScrollSpeed);
    ui->attractScrollSpeedSpinBox->blockSignals(false);
  }
  if (ui->attractAdvanceSelectionCheckBox) {
    ui->attractAdvanceSelectionCheckBox->blockSignals(true);
    ui->attractAdvanceSelectionCheckBox->setChecked(
        m_generalSettings.attractModeAdvanceSelectionEnabled);
    ui->attractAdvanceSelectionCheckBox->blockSignals(false);
  }
  if (ui->attractAdvanceIntervalSpinBox) {
    ui->attractAdvanceIntervalSpinBox->blockSignals(true);
    ui->attractAdvanceIntervalSpinBox->setValue(
        m_generalSettings.attractModeAdvanceSelectionIntervalSec);
    ui->attractAdvanceIntervalSpinBox->blockSignals(false);
  }
  if (ui->attractAdvanceRandomCheckBox) {
    ui->attractAdvanceRandomCheckBox->blockSignals(true);
    ui->attractAdvanceRandomCheckBox->setChecked(
        m_generalSettings.attractModeAdvanceSelectionRandom);
    ui->attractAdvanceRandomCheckBox->blockSignals(false);
  }
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

  if (ui->gamepadUseDpadCheckBox) {
    ui->gamepadUseDpadCheckBox->blockSignals(true);
    ui->gamepadUseDpadCheckBox->setChecked(m_generalSettings.gamepadUseDpad);
    ui->gamepadUseDpadCheckBox->blockSignals(false);
  }
  if (ui->gamepadUseLeftStickCheckBox) {
    ui->gamepadUseLeftStickCheckBox->blockSignals(true);
    ui->gamepadUseLeftStickCheckBox->setChecked(m_generalSettings.gamepadUseLeftStick);
    ui->gamepadUseLeftStickCheckBox->blockSignals(false);
  }
  if (ui->gamepadConfirmButtonLineEdit) {
    ui->gamepadConfirmButtonLineEdit->blockSignals(true);
    ui->gamepadConfirmButtonLineEdit->setText(m_generalSettings.gamepadConfirmButton);
    ui->gamepadConfirmButtonLineEdit->blockSignals(false);
  }
  if (ui->gamepadBackButtonLineEdit) {
    ui->gamepadBackButtonLineEdit->blockSignals(true);
    ui->gamepadBackButtonLineEdit->setText(m_generalSettings.gamepadBackButton);
    ui->gamepadBackButtonLineEdit->blockSignals(false);
  }
  if (ui->gamepadToggleSidebarButtonLineEdit) {
    ui->gamepadToggleSidebarButtonLineEdit->blockSignals(true);
    ui->gamepadToggleSidebarButtonLineEdit->setText(m_generalSettings.gamepadToggleSidebarButton);
    ui->gamepadToggleSidebarButtonLineEdit->blockSignals(false);
  }

  // Kartend-1v6: artwork-cycle modifier dropdown. Populated lazily on first
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

  // Kartend-81o: load toolbar customization controls.
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
  setToolbarText(ui->toolbarHideSubcollectionsTextEdit,
                 m_generalSettings.toolbarHideSubcollectionsButtonText);
  setToolbarText(ui->toolbarTitleFilterTextEdit, m_generalSettings.toolbarTitleFilterText);

  // Store original general settings for change detection
  m_originalGeneralSettings = m_generalSettings;

  // Kartend-p1jd: hydrate the launcher-presets list from the loaded general
  // settings. Done after m_originalGeneralSettings is captured so the change
  // detector can compare the live presets against the saved baseline.
  loadLauncherPresetsToUI();

  updateGamepadCaptureUi();
}

void SettingsDialog::saveGeneralSettingsFromUI() {
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if ((mainWindow) && (mainWindow->getSettingsManager())) {
    if (ui->rememberSelectionCheckBox) {
      mainWindow->m_generalSettings.rememberSelection = ui->rememberSelectionCheckBox->isChecked();
    }
    if (ui->wrapNavigationCheckBox) {
      mainWindow->m_generalSettings.wrapNavigation = ui->wrapNavigationCheckBox->isChecked();
    }
    if (ui->selectItemOnHoverCheckBox) {
      mainWindow->m_generalSettings.selectItemOnHover = ui->selectItemOnHoverCheckBox->isChecked();
    }
    if (ui->bootSplashCheckBox) {
      mainWindow->m_generalSettings.bootSplashEnabled = ui->bootSplashCheckBox->isChecked();
    }
    if (ui->resumeFocusSplashCheckBox) {
      mainWindow->m_generalSettings.resumeFocusSplashEnabled =
          ui->resumeFocusSplashCheckBox->isChecked();
    }
    if (ui->pixmapCacheSpinBox) {
      int newCacheSize = ui->pixmapCacheSpinBox->value();
      mainWindow->m_generalSettings.pixmapCacheSizeMB = newCacheSize;
      // Apply immediately (in KB)
      QPixmapCache::setCacheLimit(newCacheSize * 1024);
    }
    if (ui->keyboardSpeedSpinBox) {
      mainWindow->m_generalSettings.keyboardRepeatIntervalMs = ui->keyboardSpeedSpinBox->value();
    }
    if (ui->keyboardRepeatDelaySpinBox) {
      mainWindow->m_generalSettings.keyboardRepeatDelayMs = ui->keyboardRepeatDelaySpinBox->value();
    }
    if (ui->clickHoldDelaySpinBox) {
      mainWindow->m_generalSettings.clickHoldDelayMs = ui->clickHoldDelaySpinBox->value();
    }
    if (ui->clickHoldRepeatIntervalSpinBox) {
      mainWindow->m_generalSettings.clickHoldRepeatIntervalMs =
          ui->clickHoldRepeatIntervalSpinBox->value();
    }
    if (ui->listKeyboardRepeatSpinBox) {
      mainWindow->m_generalSettings.listKeyboardRepeatIntervalMs =
          ui->listKeyboardRepeatSpinBox->value();
    }
    if (ui->listClickHoldRepeatSpinBox) {
      mainWindow->m_generalSettings.listClickHoldRepeatIntervalMs =
          ui->listClickHoldRepeatSpinBox->value();
    }
    if (ui->mouseWheelSpeedSpinBox) {
      mainWindow->m_generalSettings.mouseWheelRows = ui->mouseWheelSpeedSpinBox->value();
    }
    if (ui->scrollAnimationSpeedSpinBox) {
      mainWindow->m_generalSettings.scrollAnimationDurationMs =
          ui->scrollAnimationSpeedSpinBox->value();
    }
    if (ui->scrollVelocityMultiplierSpinBox) {
      mainWindow->m_generalSettings.scrollVelocityMultiplier =
          ui->scrollVelocityMultiplierSpinBox->value();
    }
    if (ui->attractModeCheckBox) {
      mainWindow->m_generalSettings.attractModeEnabled = ui->attractModeCheckBox->isChecked();
    }
    if (ui->attractIdleTimeoutSpinBox) {
      mainWindow->m_generalSettings.attractModeIdleTimeoutSec =
          ui->attractIdleTimeoutSpinBox->value();
    }
    if (ui->attractAutoScrollCheckBox) {
      mainWindow->m_generalSettings.attractModeAutoScrollEnabled =
          ui->attractAutoScrollCheckBox->isChecked();
    }
    if (ui->attractScrollSpeedSpinBox) {
      mainWindow->m_generalSettings.attractModeScrollSpeed = ui->attractScrollSpeedSpinBox->value();
    }
    if (ui->attractAdvanceSelectionCheckBox) {
      mainWindow->m_generalSettings.attractModeAdvanceSelectionEnabled =
          ui->attractAdvanceSelectionCheckBox->isChecked();
    }
    if (ui->attractAdvanceIntervalSpinBox) {
      mainWindow->m_generalSettings.attractModeAdvanceSelectionIntervalSec =
          ui->attractAdvanceIntervalSpinBox->value();
    }
    if (ui->attractAdvanceRandomCheckBox) {
      mainWindow->m_generalSettings.attractModeAdvanceSelectionRandom =
          ui->attractAdvanceRandomCheckBox->isChecked();
    }
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

    if (ui->gamepadUseDpadCheckBox) {
      mainWindow->m_generalSettings.gamepadUseDpad = ui->gamepadUseDpadCheckBox->isChecked();
    }
    if (ui->gamepadUseLeftStickCheckBox) {
      mainWindow->m_generalSettings.gamepadUseLeftStick =
          ui->gamepadUseLeftStickCheckBox->isChecked();
    }
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
    // Kartend-p1jd: launcher presets live on the dialog's m_generalSettings
    // (mutated directly by the Launchers tab) — copy them onto the main
    // window's settings before persisting so the saved snapshot includes
    // any preset add/edit/remove the user just performed.
    mainWindow->m_generalSettings.launcherPresets = m_generalSettings.launcherPresets;

    // Kartend-81o: pull customizable-toolbar fields off the dialog controls.
    if (ui->toolbarGridViewVisibleCheckBox) {
      mainWindow->m_generalSettings.toolbarShowGridViewButton =
          ui->toolbarGridViewVisibleCheckBox->isChecked();
    }
    if (ui->toolbarListViewVisibleCheckBox) {
      mainWindow->m_generalSettings.toolbarShowListViewButton =
          ui->toolbarListViewVisibleCheckBox->isChecked();
    }
    if (ui->toolbarCoverFlowViewVisibleCheckBox) {
      mainWindow->m_generalSettings.toolbarShowCoverFlowViewButton =
          ui->toolbarCoverFlowViewVisibleCheckBox->isChecked();
    }
    if (ui->toolbarHideSubcollectionsVisibleCheckBox) {
      mainWindow->m_generalSettings.toolbarShowHideSubcollectionsButton =
          ui->toolbarHideSubcollectionsVisibleCheckBox->isChecked();
    }
    if (ui->toolbarTypeFilterVisibleCheckBox) {
      mainWindow->m_generalSettings.toolbarShowTypeFilter =
          ui->toolbarTypeFilterVisibleCheckBox->isChecked();
    }
    if (ui->toolbarTitleFilterVisibleCheckBox) {
      mainWindow->m_generalSettings.toolbarShowTitleFilter =
          ui->toolbarTitleFilterVisibleCheckBox->isChecked();
    }
    if (ui->toolbarSearchModeVisibleCheckBox) {
      mainWindow->m_generalSettings.toolbarShowSearchModeButton =
          ui->toolbarSearchModeVisibleCheckBox->isChecked();
    }
    if (ui->toolbarSearchBarVisibleCheckBox) {
      mainWindow->m_generalSettings.toolbarShowSearchBar =
          ui->toolbarSearchBarVisibleCheckBox->isChecked();
    }
    if (ui->toolbarGridViewTextEdit) {
      mainWindow->m_generalSettings.toolbarGridViewButtonText = ui->toolbarGridViewTextEdit->text();
    }
    if (ui->toolbarListViewTextEdit) {
      mainWindow->m_generalSettings.toolbarListViewButtonText = ui->toolbarListViewTextEdit->text();
    }
    if (ui->toolbarCoverFlowViewTextEdit) {
      mainWindow->m_generalSettings.toolbarCoverFlowViewButtonText =
          ui->toolbarCoverFlowViewTextEdit->text();
    }
    if (ui->toolbarHideSubcollectionsTextEdit) {
      mainWindow->m_generalSettings.toolbarHideSubcollectionsButtonText =
          ui->toolbarHideSubcollectionsTextEdit->text();
    }
    if (ui->toolbarTitleFilterTextEdit) {
      mainWindow->m_generalSettings.toolbarTitleFilterText = ui->toolbarTitleFilterTextEdit->text();
    }

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

    // Kartend-81o: push the new toolbar config onto the live UI immediately so
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
