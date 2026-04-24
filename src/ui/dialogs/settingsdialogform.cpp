// Sibling translation unit for SettingsDialog: form/spacing connections,
// field extraction, change detection, browse helpers, load/save.
#include <QAbstractItemView>
#include <QColorDialog>
#include <QDir>
#include <QEasingCurve>
#include <QFileDialog>
#include <QFontDialog>
#include <QGraphicsDropShadowEffect>
#include <QInputDialog>
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
#include <algorithm>
#include <functional>
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


void SettingsDialog::revertCurrentCollectionEdits() {
  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= m_workingCollections.size()) {
    return;
  }

  m_workingCollections[currentCollectionIndex] = originalCollection;
  if (currentCollectionIndex >= 0 &&
      currentCollectionIndex < collections.size()) {
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

  const QMessageBox::StandardButton decision =
      promptUnsavedChanges(actionDescription);
  if (decision == QMessageBox::Cancel) {
    return false;
  }
  if (decision == QMessageBox::Save) {
    if (currentCollectionIndex >= 0 &&
        currentCollectionIndex < m_workingCollections.size()) {
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
  auto validatePath = [this](const QString &path,
                             const QString &fieldName) -> bool {
    if (path.isEmpty()) {
      return true; // Empty paths are allowed (optional fields)
    }
    auto result = PathUtils::validatePathSecurity(path);
    if (result.isError()) {
      QMessageBox::warning(
          this, tr("Invalid Path"),
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

  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= collections.size()) {
    return false;
  }

  return checkBasicFieldChanges() || checkExtensionChanges() ||
         checkTreeNameChanges() || checkParentCollectionChanges() ||
         checkDimensionChanges() || checkColorChanges() ||
         checkListModeChanges() || checkBackgroundChanges();
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
      m_saveButtonGlowAnim =
          new QPropertyAnimation(m_saveButtonGlow, "blurRadius", this);
      m_saveButtonGlowAnim->setDuration(1200);
      m_saveButtonGlowAnim->setStartValue(6.0);
      m_saveButtonGlowAnim->setEndValue(22.0);
      m_saveButtonGlowAnim->setEasingCurve(QEasingCurve::InOutSine);
      m_saveButtonGlowAnim->setLoopCount(-1);
      // Ping-pong between start/end by alternating direction on each loop.
      QObject::connect(
          m_saveButtonGlowAnim, &QPropertyAnimation::currentLoopChanged, this,
          [this](int) {
            m_saveButtonGlowAnim->setDirection(
                m_saveButtonGlowAnim->direction() ==
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
    if (m_saveButtonGlowAnim &&
        m_saveButtonGlowAnim->state() == QAbstractAnimation::Running) {
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
    bool hasSelection = currentTreeItem &&
                        itemToCollectionIndex.contains(currentTreeItem);
    ui->removeCollectionButton->setEnabled(hasSelection &&
                                           !collections.isEmpty());
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
    ui->coreLineEdit->setToolTip(
        "Path to RetroArch core file (.so/.dll/.dylib)");
    ui->launchParamsLineEdit->setToolTip("Additional RetroArch parameters");
  } else {
    ui->launchParamsLineEdit->setToolTip(
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
  bool isRetroArch =
      ui->launcherLineEdit->text().contains("retroarch", Qt::CaseInsensitive);
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
  if (!m_isLoading &&
      currentCollectionIndex == originalCurrentCollectionIndex &&
      originalCurrentCollectionIndex >= 0 &&
      originalCurrentCollectionIndex < collections.size()) {
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
    ui->rememberSelectionCheckBox->setChecked(
        m_generalSettings.rememberSelection);
    ui->rememberSelectionCheckBox->blockSignals(false);
  }
  if (ui->wrapNavigationCheckBox) {
    ui->wrapNavigationCheckBox->blockSignals(true);
    ui->wrapNavigationCheckBox->setChecked(m_generalSettings.wrapNavigation);
    ui->wrapNavigationCheckBox->blockSignals(false);
  }
  if (ui->pixmapCacheSpinBox) {
    ui->pixmapCacheSpinBox->blockSignals(true);
    ui->pixmapCacheSpinBox->setValue(m_generalSettings.pixmapCacheSizeMB);
    ui->pixmapCacheSpinBox->blockSignals(false);
  }
  if (ui->keyboardSpeedSpinBox) {
    ui->keyboardSpeedSpinBox->blockSignals(true);
    ui->keyboardSpeedSpinBox->setValue(
        m_generalSettings.keyboardRepeatIntervalMs);
    ui->keyboardSpeedSpinBox->blockSignals(false);
  }
  if (ui->keyboardRepeatDelaySpinBox) {
    ui->keyboardRepeatDelaySpinBox->blockSignals(true);
    ui->keyboardRepeatDelaySpinBox->setValue(
        m_generalSettings.keyboardRepeatDelayMs);
    ui->keyboardRepeatDelaySpinBox->blockSignals(false);
  }
  if (ui->clickHoldDelaySpinBox) {
    ui->clickHoldDelaySpinBox->blockSignals(true);
    ui->clickHoldDelaySpinBox->setValue(m_generalSettings.clickHoldDelayMs);
    ui->clickHoldDelaySpinBox->blockSignals(false);
  }
  if (ui->clickHoldRepeatIntervalSpinBox) {
    ui->clickHoldRepeatIntervalSpinBox->blockSignals(true);
    ui->clickHoldRepeatIntervalSpinBox->setValue(
        m_generalSettings.clickHoldRepeatIntervalMs);
    ui->clickHoldRepeatIntervalSpinBox->blockSignals(false);
  }
  if (ui->listKeyboardRepeatSpinBox) {
    ui->listKeyboardRepeatSpinBox->blockSignals(true);
    ui->listKeyboardRepeatSpinBox->setValue(
        m_generalSettings.listKeyboardRepeatIntervalMs);
    ui->listKeyboardRepeatSpinBox->blockSignals(false);
  }
  if (ui->listClickHoldRepeatSpinBox) {
    ui->listClickHoldRepeatSpinBox->blockSignals(true);
    ui->listClickHoldRepeatSpinBox->setValue(
        m_generalSettings.listClickHoldRepeatIntervalMs);
    ui->listClickHoldRepeatSpinBox->blockSignals(false);
  }
  if (ui->mouseWheelSpeedSpinBox) {
    ui->mouseWheelSpeedSpinBox->blockSignals(true);
    ui->mouseWheelSpeedSpinBox->setValue(m_generalSettings.mouseWheelRows);
    ui->mouseWheelSpeedSpinBox->blockSignals(false);
  }
  if (ui->scrollAnimationSpeedSpinBox) {
    ui->scrollAnimationSpeedSpinBox->blockSignals(true);
    ui->scrollAnimationSpeedSpinBox->setValue(
        m_generalSettings.scrollAnimationDurationMs);
    ui->scrollAnimationSpeedSpinBox->blockSignals(false);
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
  if (ui->startupCollectionComboBox) {
    ui->startupCollectionComboBox->blockSignals(true);
    ui->startupCollectionComboBox->clear();
    ui->startupCollectionComboBox->addItem(tr("(Default)"), QString());
    if (mainWindow) {
      for (const CollectionConfig &cfg : std::as_const(mainWindow->m_collections)) {
        ui->startupCollectionComboBox->addItem(cfg.name, cfg.name);
      }
    }
    int idx = ui->startupCollectionComboBox->findData(
        m_generalSettings.startupCollection);
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
    ui->gamepadUseLeftStickCheckBox->setChecked(
        m_generalSettings.gamepadUseLeftStick);
    ui->gamepadUseLeftStickCheckBox->blockSignals(false);
  }
  if (ui->gamepadConfirmButtonLineEdit) {
    ui->gamepadConfirmButtonLineEdit->blockSignals(true);
    ui->gamepadConfirmButtonLineEdit->setText(
        m_generalSettings.gamepadConfirmButton);
    ui->gamepadConfirmButtonLineEdit->blockSignals(false);
  }
  if (ui->gamepadBackButtonLineEdit) {
    ui->gamepadBackButtonLineEdit->blockSignals(true);
    ui->gamepadBackButtonLineEdit->setText(m_generalSettings.gamepadBackButton);
    ui->gamepadBackButtonLineEdit->blockSignals(false);
  }
  if (ui->gamepadToggleSidebarButtonLineEdit) {
    ui->gamepadToggleSidebarButtonLineEdit->blockSignals(true);
    ui->gamepadToggleSidebarButtonLineEdit->setText(
        m_generalSettings.gamepadToggleSidebarButton);
    ui->gamepadToggleSidebarButtonLineEdit->blockSignals(false);
  }

  // Store original general settings for change detection
  m_originalGeneralSettings = m_generalSettings;

  updateGamepadCaptureUi();
}

void SettingsDialog::saveGeneralSettingsFromUI() {
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if ((mainWindow) && (mainWindow->getSettingsManager())) {
    if (ui->rememberSelectionCheckBox) {
      mainWindow->m_generalSettings.rememberSelection =
          ui->rememberSelectionCheckBox->isChecked();
    }
    if (ui->wrapNavigationCheckBox) {
      mainWindow->m_generalSettings.wrapNavigation =
          ui->wrapNavigationCheckBox->isChecked();
    }
    if (ui->pixmapCacheSpinBox) {
      int newCacheSize = ui->pixmapCacheSpinBox->value();
      mainWindow->m_generalSettings.pixmapCacheSizeMB = newCacheSize;
      // Apply immediately (in KB)
      QPixmapCache::setCacheLimit(newCacheSize * 1024);
    }
    if (ui->keyboardSpeedSpinBox) {
      mainWindow->m_generalSettings.keyboardRepeatIntervalMs =
          ui->keyboardSpeedSpinBox->value();
    }
    if (ui->keyboardRepeatDelaySpinBox) {
      mainWindow->m_generalSettings.keyboardRepeatDelayMs =
          ui->keyboardRepeatDelaySpinBox->value();
    }
    if (ui->clickHoldDelaySpinBox) {
      mainWindow->m_generalSettings.clickHoldDelayMs =
          ui->clickHoldDelaySpinBox->value();
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
      mainWindow->m_generalSettings.mouseWheelRows =
          ui->mouseWheelSpeedSpinBox->value();
    }
    if (ui->scrollAnimationSpeedSpinBox) {
      mainWindow->m_generalSettings.scrollAnimationDurationMs =
          ui->scrollAnimationSpeedSpinBox->value();
    }
    if (ui->titleSaturationSpinBox) {
      mainWindow->m_generalSettings.titleTintSaturation =
          ui->titleSaturationSpinBox->value();
      // Apply to ItemWidget static settings
      ItemWidget::setTitleTintSaturation(ui->titleSaturationSpinBox->value());
    }
    if (ui->titleLightnessSpinBox) {
      mainWindow->m_generalSettings.titleTintLightness =
          ui->titleLightnessSpinBox->value();
      // Apply to ItemWidget static settings
      ItemWidget::setTitleTintLightness(ui->titleLightnessSpinBox->value());
    }
    if (ui->baseColorEdit) {
      mainWindow->m_generalSettings.titleBaseColor =
          ui->baseColorEdit->text().trimmed();
      // Apply to ItemWidget static settings
      ItemWidget::setTitleBaseColor(ui->baseColorEdit->text().trimmed());
    }
    if (ui->startupCollectionComboBox) {
      mainWindow->m_generalSettings.startupCollection =
          ui->startupCollectionComboBox->currentData().toString();
    }
    // Note: customFontFamily is now saved per-collection, not in general
    // settings

    auto singleKeyFromEdit = [](QKeySequenceEdit *edit,
                                int fallbackKey) -> int {
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

    mainWindow->m_generalSettings.keyNavUp = singleKeyFromEdit(
        ui->keyNavUpEdit, mainWindow->m_generalSettings.keyNavUp);
    mainWindow->m_generalSettings.keyNavDown = singleKeyFromEdit(
        ui->keyNavDownEdit, mainWindow->m_generalSettings.keyNavDown);
    mainWindow->m_generalSettings.keyNavLeft = singleKeyFromEdit(
        ui->keyNavLeftEdit, mainWindow->m_generalSettings.keyNavLeft);
    mainWindow->m_generalSettings.keyNavRight = singleKeyFromEdit(
        ui->keyNavRightEdit, mainWindow->m_generalSettings.keyNavRight);
    mainWindow->m_generalSettings.keyConfirm = singleKeyFromEdit(
        ui->keyConfirmEdit, mainWindow->m_generalSettings.keyConfirm);
    mainWindow->m_generalSettings.keyBack = singleKeyFromEdit(
        ui->keyBackEdit, mainWindow->m_generalSettings.keyBack);
    mainWindow->m_generalSettings.keySearch = singleKeyFromEdit(
        ui->keySearchEdit, mainWindow->m_generalSettings.keySearch);

    if (ui->gamepadUseDpadCheckBox) {
      mainWindow->m_generalSettings.gamepadUseDpad =
          ui->gamepadUseDpadCheckBox->isChecked();
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
      const QString v =
          ui->gamepadToggleSidebarButtonLineEdit->text().trimmed();
      if (!v.isEmpty()) {
        mainWindow->m_generalSettings.gamepadToggleSidebarButton = v;
      }
    }
    mainWindow->getSettingsManager()->saveGeneralSettings(
        mainWindow->m_generalSettings);
    m_generalSettings = mainWindow->m_generalSettings;

    // Refresh all visible widgets to apply text appearance changes immediately
    ScrollManager *scrollManager = mainWindow->getScrollManager();
    if (scrollManager) {
      const auto &activeWidgets = scrollManager->getActiveWidgets();
      for (auto it = activeWidgets.constBegin(); it != activeWidgets.constEnd();
           ++it) {
        ItemWidget *widget = it.value();
        if (widget) {
          widget->applyTitleTint();
        }
      }
    }
  }
}

