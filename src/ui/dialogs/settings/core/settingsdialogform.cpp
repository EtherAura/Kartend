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
#include "iscrolldatasource.h"
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

  // Kartend-i04uo: `collections` and `m_workingCollections` are kept
  // element-identical. UI edits reach m_workingCollections ONLY through this
  // method, so the bulk `collections = m_workingCollections` can't leak another
  // index's unsaved edit. revertCurrentCollectionEdits writes the same value to
  // both vectors per-index, and add/remove mutate both in lockstep (see
  // collectionremover.cpp applying one removal map to each), so the bulk-vs-
  // per-index asymmetry between save and revert never desyncs the two.
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
    ui->launcherPanel->coreLineEdit()->setToolTip(
        tr("Path to libretro core file (.so/.dll/.dylib)"));
    ui->launcherPanel->launchParamsLineEdit()->setToolTip(
        tr("Additional libretro frontend parameters"));
  } else {
    ui->launcherPanel->launchParamsLineEdit()->setToolTip(
        tr("Additional command-line parameters for the launcher"));
  }

  // Update extract archives visibility based on launcher type
  updateExtractArchivesVisibility();
}

void SettingsDialog::onContentDirectoryChanged() {
  updateFieldVisibility();
  checkForChanges();
}

void SettingsDialog::updateFieldVisibility() {
  // The line edits / buttons dereferenced below are unconditional widgets from
  // each panel's .ui, created by setupUi, so these accessors never return null
  // here — unlike the conditionally-built buttons (duplicate / editLinkedParents)
  // that callers null-guard. Direct dereference is intentional (Kartend-22s4k).
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

// Kartend-vy1xs: onGridWidthChanged() removed — its only extra duty over
// checkForChanges() was emitting the dead gridWidthChanged live-preview
// signal; the spin box now connects straight to checkForChanges().

void SettingsDialog::loadGeneralSettingsToUI() {
  auto *mainWindow = dynamic_cast<IMainWindow *>(parent());
  if (mainWindow) {
    m_generalSettings = mainWindow->generalSettings();
  }
  // Splash / Fonts / Attract / general "General" sub-tab fields are all
  // owned by their respective panels — refresh them from the working copy.
  ui->splashPanel->load();
  ui->fontsPanel->load();
  ui->attractPanel->load();
  ui->marqueePanel->load();
  ui->generalSettingsPanel->load();
  ui->scraperSettingsPanel->load();
  ui->screenScraperCredentialsPanel->load();
  ui->tmdbCredentialsPanel->load();
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
  ui->controlsPanel->load();
  // Toolbar customization fields owned by ToolbarPanel.
  ui->toolbarPanel->load();

  // Store original general settings for change detection
  m_originalGeneralSettings = m_generalSettings;

  // hydrate the launcher-presets list from the loaded general
  // settings. Done after m_originalGeneralSettings is captured so the change
  // detector can compare the live presets against the saved baseline.
  ui->launcherPresetsPanel->load();

  if (m_gamepadCapture) {
    m_gamepadCapture->refreshUi();
  }
}

ErrorUtils::Result<void> SettingsDialog::saveGeneralSettingsFromUI() {
  auto *mainWindow = dynamic_cast<IMainWindow *>(parent());
  auto *settingsManager = m_ctx ? m_ctx->settingsManager() : nullptr;
  if (mainWindow && settingsManager) {
    GeneralSettings &mwSettings = mainWindow->generalSettings();
    // Panels write straight into m_generalSettings — the dialog's live model,
    // seeded as a full copy of mainWindow's GeneralSettings when the dialog
    // opened (loadGeneralSettingsToUI). Mirror the whole struct in one
    // assignment instead of copying ~60 fields by hand: the per-field copy was
    // a regression magnet — a new GeneralSettings field silently failed to
    // propagate whenever someone forgot to add its line here (Kartend-d27fg).
    // This matches the whole-struct assign the Apply/OK paths already use in
    // settingsdialog.cpp. The live-apply side effects below still fire so the
    // change is visible while the dialog stays open.
    //
    // Canonicalize first (trim the free-text path/title fields) so the value we
    // persist — and the baseline we reset below — carry no stray surrounding
    // whitespace, keeping those fields whitespace-insensitive now that the
    // dirty-check is a whole-struct compare (Kartend-6oqat).
    m_generalSettings = m_generalSettings.normalizedForSave();
    mwSettings = m_generalSettings;

    // applyPixmapCacheBudget propagates the new size to QPixmapCache and the
    // CacheManager artworkCache in lockstep (Kartend-10pb).
    mainWindow->applyPixmapCacheBudget(m_generalSettings.media.pixmapCacheSizeMB);
    VideoThumbnailExtractor::instance()->setExtractionTimeoutMs(
        m_generalSettings.media.videoThumbnailExtractionTimeoutMs);
    // Ping MainWindow's marquee lifecycle so the marquee window appears /
    // disappears / moves to a new screen / switches mode without a restart.
    mainWindow->applyMarqueeSettings();
    ItemWidget::setTitleTintSaturation(m_generalSettings.appearance.titleTintSaturation);
    ItemWidget::setTitleTintLightness(m_generalSettings.appearance.titleTintLightness);
    ItemWidget::setTitleBaseColor(m_generalSettings.appearance.titleBaseColor);

    auto saveResult = settingsManager->saveGeneralSettings(mwSettings);
    m_generalSettings = mwSettings;

    // Apply showTitleInPlaceholder to ItemWidget + repaint visible widgets,
    // since this used to be a live-apply side-effect and the panel pattern
    // makes the field deferred-save.
    ItemWidget::setShowTitleInPlaceholder(m_generalSettings.view.showTitleInPlaceholder);
    if (auto *scrollManager = m_ctx->scrollData()) {
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
    IScrollDataSource *scrollManager = m_ctx->scrollData();
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
