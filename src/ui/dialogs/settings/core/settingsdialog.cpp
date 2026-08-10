// Collection configuration dialog with tree-based hierarchy editing and live
// preview.
#include <algorithm>
#include <functional>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDialog>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmapCache>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <set>

#include "appearancecolorspanel.h"
#include "appearanceeffectspanel.h"
#include "appearancelayoutpanel.h"
#include "appearancelistpanel.h"
#include "appearancetitlespanel.h"
#include "appearancetoolbarpanel.h"
#include "applicationcontext.h"
#include "artworktabpanel.h"
#include "attractpanel.h"
#include "collectionremover.h"
#include "collectiontreewidget.h"
#include "configurationpanel.h"
#include "controlspanel.h"
#include "errordialog.h"
#include "extensionutils.h"
#include "fontspanel.h"
#include "gamepadcapturecontroller.h"
#include "gamepadmanager.h"
#include "generalsettingspanel.h"
#include "imainwindow.h"
#include "iscrolldatasource.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "launcherpresetspanel.h"
#include "launchertabpanel.h"
#include "marqueepanel.h"
#include "pathutils.h"
#include "scrapercredentialspanel.h"
#include "scrapersettingspanel.h"
#include "settingsdialog.h"
#include "sidebarpanel.h"
#include "splashpanel.h"
#include "subfolderspanel.h"
#include "toolbarpanel.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants/timing.h"
#include "uiconstants/viewport.h"

SettingsDialog::SettingsDialog(QWidget *parent, const QList<CollectionConfig> &initialCollections,
                               int initialIndex, const ApplicationContext *ctx)
    : QDialog(parent), ui(new Ui::SettingsDialog), collectionTreeWidget(nullptr),
      currentTreeItem(nullptr), collections(initialCollections),
      originalCurrentCollectionIndex(initialIndex), currentCollectionIndex(initialIndex),
      m_workingCollections(initialCollections), m_collectionSaved(true), m_isLoading(false),
      m_ctx(ctx) {
  ui->setupUi(this);
  setWindowTitle(tr("Settings"));
  setModal(true);

  // Resolve the save/load host ONCE here instead of re-casting QObject::parent()
  // at the eight downstream use sites (Kartend-rn0ym). A null result is normal
  // in tests / CLI / headless construction (no parent at all). But when a parent
  // widget WAS supplied and the cast still fails — a reparent or a
  // non-MainWindow construction path — the general-settings save/load silently
  // no-ops while reporting success, dropping the user's edits. Warn + assert in
  // that case so the condition is diagnosable rather than invisible; the genuine
  // headless case (no parent) stays silent.
  m_host = dynamic_cast<IMainWindow *>(QObject::parent());
  if (!m_host && QObject::parent()) {
    qCWarning(ErrorUtils::lcErrors())
        << "SettingsDialog: parent widget is not an IMainWindow; general-settings "
           "save/load will be a no-op and edits would be silently dropped";
    Q_ASSERT_X(m_host, "SettingsDialog", "parent() is non-null but not an IMainWindow");
  }

  // Persistent Save button — lives in the dialog's bottom button row, left of
  // Cancel/OK, so it's reachable from every settings page. Its text, icon and
  // initial disabled state come from settingsdialog.ui; the click handler and
  // dirty-state glow are wired with the other connections below.
  m_saveButton = ui->saveButton;

  // Gamepad button-capture state machine. Constructed before
  // setupConnections() runs so the Detect-button click handlers can
  // dispatch through it.
  m_gamepadCapture = new GamepadCaptureController(this, m_ctx);

  // Wire the SettingsModel pointer aggregate at the editable data fields
  // owned by this dialog. Done before constructing CollectionRemover so the
  // remover sees a fully-populated model.
  m_model.collections = &collections;
  m_model.workingCollections = &m_workingCollections;
  m_model.originalCollection = &originalCollection;
  m_model.generalSettings = &m_generalSettings;
  m_model.originalGeneralSettings = &m_originalGeneralSettings;
  m_model.collectionSaved = &m_collectionSaved;
  m_model.currentIndex = &currentCollectionIndex;

  // Bridge the DAT-audit hooks to MainWindow (our IMainWindow parent) so the
  // configuration panel can launch an audit for the edited collection and show
  // its last-audited time. A null parent (tests / standalone construction)
  // leaves the hooks unset; the panel guards on them (Kartend-4mqkof).
  if (auto *mainWindow = m_host) {
    m_model.openDatAudit = [mainWindow](const CollectionConfig &collection) {
      mainWindow->openDatAuditForCollection(collection);
    };
    m_model.datAuditStatus = [mainWindow](const QString &collectionUuid) {
      return mainWindow->datAuditStatusForCollection(collectionUuid);
    };
  }

  // Multi-step collection-removal pipeline. Constructed before the
  // tree's Remove button is wired up so removeCollection() can dispatch.
  m_collectionRemover = new CollectionRemover(&m_model, this, this);

  // The presets list itself is hydrated by loadGeneralSettingsToUI(); the
  // pointer install here is one-shot because m_generalSettings lives for the
  // dialog's lifetime. The panel mutates the list in place and signals back
  // so checkForChanges() picks up preset edits without other panels going
  // through the panel API to read presets (settingsdialoglaunchers.cpp still
  // reads m_generalSettings.launcherPresets directly for its presets combo).
  ui->launcherPresetsPanel->setPresets(&m_generalSettings.launchers.launcherPresets);
  ui->launcherPresetsPanel->setRetroarchConfigOverride(
      m_generalSettings.launchers.retroarchConfigPath);
  connect(ui->launcherPresetsPanel, &LauncherPresetsPanel::presetsChanged, this,
          &SettingsDialog::checkForChanges);

  // Sidebar (Details Pane) panel: emits changed() on any field mutation,
  // routed to checkForChanges. The panel handles its own pickers and
  // position-driven width-vs-height visibility internally.
  ui->sidebarPanel->setModel(&m_model);
  connect(ui->sidebarPanel, &SidebarPanel::changed, this, &SettingsDialog::checkForChanges);

  // Subfolders panel: per-collection load/save; visibility of the dependent
  // options is internal to the panel.
  ui->subfoldersPanel->setModel(&m_model);
  connect(ui->subfoldersPanel, &SubfoldersPanel::changed, this, &SettingsDialog::checkForChanges);

  // Artwork tab: per-collection asset directories + custom artwork types.
  ui->artworkPanel->setModel(&m_model);
  connect(ui->artworkPanel, &ArtworkTabPanel::changed, this, &SettingsDialog::checkForChanges);

  // Configuration tab: per-collection identity / paths / type / extensions /
  // expand-mode / show-all-subcollection-items. Cross-cutting widgets
  // (parent combo, linked-parents button, recursive-import button, browse-
  // media-dir button) remain wired by the host below.
  ui->configurationPanel->setModel(&m_model);
  connect(ui->configurationPanel, &ConfigurationPanel::changed, this,
          &SettingsDialog::checkForChanges);

  // Launcher tab: simple data fields go through panel.load/save; cross-
  // cutting widgets (additional-launchers list, default-launcher combo,
  // browse buttons) remain accessor-driven from the host.
  ui->launcherPanel->setModel(&m_model);
  connect(ui->launcherPanel, &LauncherTabPanel::changed, this, &SettingsDialog::checkForChanges);

  // Appearance > List Mode sub-sub-tab: per-collection list font size + row
  // height. Other appearance sub-sub-tabs land as separate panels.
  ui->appearanceListPanel->setModel(&m_model);
  ui->appearanceToolbarPanel->setModel(&m_model);
  ui->appearanceTitlesPanel->setModel(&m_model);
  ui->appearanceEffectsPanel->setModel(&m_model);
  ui->appearanceLayoutPanel->setModel(&m_model);
  connect(ui->appearanceListPanel, &AppearanceListPanel::changed, this,
          &SettingsDialog::checkForChanges);
  connect(ui->appearanceToolbarPanel, &AppearanceToolbarPanel::changed, this,
          &SettingsDialog::checkForChanges);
  connect(ui->appearanceTitlesPanel, &AppearanceTitlesPanel::changed, this,
          &SettingsDialog::checkForChanges);
  connect(ui->appearanceEffectsPanel, &AppearanceEffectsPanel::changed, this,
          &SettingsDialog::checkForChanges);
  connect(ui->appearanceLayoutPanel, &AppearanceLayoutPanel::changed, this,
          &SettingsDialog::checkForChanges);

  // Appearance > Colors sub-sub-tab: per-collection background / palette /
  // list-row colors / vignette plus three global title-tint fields. Pointer
  // install gives the panel a handle on m_generalSettings so it can refresh
  // / writeBack the title fields. baseColorChanged() is the live-save signal
  // — host mirrors to mainWindow + applies the ItemWidget side effect
  // immediately to preserve the picker's instant-feedback UX; the disk write
  // is debounced through scheduleLiveSettingsSave so a picker drag doesn't
  // rewrite the whole INI once per tick.
  ui->appearanceColorsPanel->setModel(&m_model);
  connect(ui->appearanceColorsPanel, &AppearanceColorsPanel::changed, this,
          &SettingsDialog::checkForChanges);
  connect(ui->appearanceColorsPanel, &AppearanceColorsPanel::baseColorChanged, this,
          [this](const QString &c) {
            auto *mainWindow = m_host;
            auto *sm = m_ctx ? m_ctx->settingsManager() : nullptr;
            if (!mainWindow || !sm) return;
            mainWindow->generalSettings().appearance.titleBaseColor = c;
            ItemWidget::setTitleBaseColor(c);
            // Repaint visible items so the new base color shows immediately
            // rather than only on the next incidental repaint (Kartend-f3ivg) —
            // mirrors saveGeneralSettingsFromUI's applyTitleTint sweep.
            if (auto *scroll = m_ctx ? m_ctx->scrollData() : nullptr) {
              const auto &active = scroll->getActiveWidgets();
              for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
                if (auto *w = it.value()) w->applyTitleTint();
              }
            }
            m_liveSettingsApplied = true; // Kartend-9cngh: revert on Cancel.
            scheduleLiveSettingsSave();
          });

  // Application-font panel: live-save semantics — panel mutates the
  // pointed-to GeneralSettings and emits changed(); we mirror to mainWindow
  // and apply the font to the running app per edit, all without going
  // through the deferred-save path; the disk write is debounced (a font-size
  // spinbox scrub used to rewrite the whole INI once per step). Mirror ONLY
  // the two font fields this panel edits, matching the baseColorChanged
  // handler's single-field mirror above — the earlier whole-struct copy
  // dragged every pending deferred edit onto the host, and the debounced
  // flush then persisted edits the user had not saved (and might Discard).
  ui->fontsPanel->setModel(&m_model);
  connect(ui->fontsPanel, &FontsPanel::changed, this, [this]() {
    auto *mainWindow = m_host;
    auto *sm = m_ctx ? m_ctx->settingsManager() : nullptr;
    if (!mainWindow || !sm) {
      return;
    }
    GeneralSettings &live = mainWindow->generalSettings();
    live.appearance.globalUiFontFamily = m_generalSettings.appearance.globalUiFontFamily;
    live.appearance.globalUiFontPointSize = m_generalSettings.appearance.globalUiFontPointSize;
    mainWindow->applyGlobalUiFontFromSettings();
    m_liveSettingsApplied = true; // Kartend-9cngh: revert on Cancel.
    scheduleLiveSettingsSave();
  });

  // Splash (boot + resume-focus) panel: same live-save shape as FontsPanel
  // minus the apply step — splashes are shown on next startup / focus event,
  // so the (debounced) persist is sufficient. Mirrors only its own splash
  // sub-struct for the same deferred-edit-leak reason as the fonts handler.
  ui->splashPanel->setModel(&m_model);
  connect(ui->splashPanel, &SplashPanel::changed, this, [this]() {
    auto *mainWindow = m_host;
    auto *sm = m_ctx ? m_ctx->settingsManager() : nullptr;
    if (!mainWindow || !sm) {
      return;
    }
    mainWindow->generalSettings().splash = m_generalSettings.splash;
    m_liveSettingsApplied = true; // Kartend-9cngh: revert on Cancel.
    scheduleLiveSettingsSave();
  });

  // Attract-mode panel: deferred-save — panel keeps m_generalSettings live;
  // checkForChanges drives the Save button state and persistence happens in
  // saveGeneralSettingsFromUI like the other deferred general fields.
  ui->attractPanel->setModel(&m_model);
  connect(ui->attractPanel, &AttractPanel::changed, this, &SettingsDialog::checkForChanges);

  // Marquee / secondary-monitor panel: same deferred-save shape.
  // MainWindow::applyMarqueeSettings() is called by settingsdialogform's
  // copy block so the window appears / disappears / re-pins to a new
  // screen as soon as the user clicks Save.
  ui->marqueePanel->setModel(&m_model);
  connect(ui->marqueePanel, &MarqueePanel::changed, this, &SettingsDialog::checkForChanges);

  // Toolbar (items-page button visibility + text overrides) panel: same
  // deferred-save shape as AttractPanel.
  ui->toolbarPanel->setModel(&m_model);
  connect(ui->toolbarPanel, &ToolbarPanel::changed, this, &SettingsDialog::checkForChanges);

  // General settings panel (startup / selection / input timing / performance
  // & history): deferred-save like AttractPanel/ToolbarPanel. The startup-
  // collection combo is populated from the loaded collections inside
  // loadGeneralSettingsToUI.
  ui->generalSettingsPanel->setModel(&m_model);
  connect(ui->generalSettingsPanel, &GeneralSettingsPanel::changed, this,
          &SettingsDialog::checkForChanges);
  // Scraper performance + behavior panel. Same deferred-save pattern:
  // setModel binds the GeneralSettings pointer; changed() flips the
  // dialog into "you have unsaved changes" mode.
  ui->scraperSettingsPanel->setModel(&m_model);
  connect(ui->scraperSettingsPanel, &ScraperSettingsPanel::changed, this,
          &SettingsDialog::checkForChanges);
  // Scraper credentials live inline inside each provider's sub-tab now
  // — no separate Credentials tab. Two panels, each filtered to one
  // provider; they share the same deferred-save model write path.
  ui->screenScraperCredentialsPanel->setProvider(QStringLiteral("screenscraper"));
  ui->screenScraperCredentialsPanel->setModel(&m_model);
  connect(ui->screenScraperCredentialsPanel, &ScraperCredentialsPanel::changed, this,
          &SettingsDialog::checkForChanges);
  ui->tmdbCredentialsPanel->setProvider(QStringLiteral("tmdb"));
  ui->tmdbCredentialsPanel->setModel(&m_model);
  connect(ui->tmdbCredentialsPanel, &ScraperCredentialsPanel::changed, this,
          &SettingsDialog::checkForChanges);
  // Kartend-ztc64: non-modal "credentials stored unencrypted" banner. Seed
  // from the persisted demotion flag so the warning survives a restart, and
  // track live changes so the banner appears when an Apply demotes the
  // credentials and clears the moment a later save re-promotes them. The
  // demotion is provider-agnostic (any failed keychain write sets it), so
  // both credential panels mirror the same state.
  if (auto *settingsManager = m_ctx ? m_ctx->settingsManager() : nullptr) {
    const auto applyDemotionNotice = [this](const QString &reason) {
      ui->screenScraperCredentialsPanel->setStorageDemotionNotice(reason);
      ui->tmdbCredentialsPanel->setStorageDemotionNotice(reason);
    };
    applyDemotionNotice(settingsManager->credentialDemotionReason());
    connect(settingsManager, &ISettingsManager::credentialStorageDemotionChanged, this,
            applyDemotionNotice);
  }

  // Controls panel (Keyboard / Gamepad / Mouse). Bind the gamepad-capture
  // controller's widget pointers to the panel's own gamepad widgets so the
  // controller doesn't need friend access to ui_settingsdialog.h anymore.
  ui->controlsPanel->setModel(&m_model);
  connect(ui->controlsPanel, &ControlsPanel::changed, this, &SettingsDialog::checkForChanges);
  ui->controlsPanel->installGamepadCaptureController(m_gamepadCapture);
  m_gamepadCapture->setWidgets({
      ui->controlsPanel->gamepadConfirmLineEdit(),
      ui->controlsPanel->gamepadBackLineEdit(),
      ui->controlsPanel->gamepadToggleSidebarLineEdit(),
      ui->controlsPanel->detectConfirmButton(),
      ui->controlsPanel->detectBackButton(),
      ui->controlsPanel->detectToggleSidebarButton(),
      ui->controlsPanel->useDpadCheckBox(),
      ui->controlsPanel->useLeftStickCheckBox(),
  });

  collectionTreeWidget = ui->collectionsTreeShell->collectionTreeWidget();
  m_treeManager =
      std::make_unique<TreeManager>(collectionTreeWidget, &collections, &m_workingCollections);

  installEventFilter(this);
  if (collectionTreeWidget) {
    collectionTreeWidget->installEventFilter(this);
    collectionTreeWidget->setFocusPolicy(Qt::WheelFocus);
  }

  // Check if there's no root collection and prompt to create one
  ensureRootCollectionExists();

  // Every mutation that commits the working collection list (per-collection
  // save, add/duplicate, remove, drag-drop reparent, recursive import) emits
  // collectionSaved — refresh the startup-collection combo from the working
  // list so collections added or renamed in-session are immediately pickable
  // instead of the combo staying frozen at its dialog-open contents.
  connect(this, &SettingsDialog::collectionSaved, this,
          &SettingsDialog::refreshStartupCollectionCombo);

  loadGeneralSettingsToUI();
  setupConnections();
  updateCollectionTreeWidget();

  if (currentCollectionIndex >= 0 && currentCollectionIndex < m_workingCollections.size()) {
    expandPathToCollection(currentCollectionIndex);
  }

  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_workingCollections.size()) {
    currentCollectionIndex = m_workingCollections.isEmpty() ? -1 : 0;
  }
  if (currentCollectionIndex >= 0 && currentCollectionIndex < m_workingCollections.size()) {
    loadCollectionToUI(currentCollectionIndex);
    originalCollection = m_workingCollections[currentCollectionIndex];
    if (auto *item = m_treeManager ? m_treeManager->itemAt(currentCollectionIndex) : nullptr) {
      collectionTreeWidget->setCurrentItem(item);
      currentTreeItem = item;
    }
  }

  originalCurrentCollectionIndex = currentCollectionIndex;
  // loadGeneralSettingsToUI() already ran above: its panels read m_generalSettings
  // and its startup-collection combo reads the main window's live collections —
  // neither changes during the collection-loading block between, so a second full
  // call (12 panel refreshes + combo repopulate + baseline recapture) was
  // redundant (Kartend-edy1c).

  // Initialize button states after setup is complete
  m_collectionSaved = true;
  updateSaveButtonStyle();
  updateDeleteButtonState();
}

// Handles wheel routing and whitespace click to allow deselection
auto SettingsDialog::eventFilter(QObject *obj, QEvent *event) -> bool {
  if (event->type() == QEvent::Wheel) {
    auto *wheelEvent = static_cast<QWheelEvent *>(event);

    if (obj == collectionTreeWidget) {
      return QDialog::eventFilter(obj, event);
    }

    if ((collectionTreeWidget) && collectionTreeWidget->underMouse()) {
      QWheelEvent forwardedEvent(
          collectionTreeWidget->mapFromGlobal(wheelEvent->globalPosition().toPoint()),
          wheelEvent->globalPosition().toPoint(), wheelEvent->pixelDelta(),
          wheelEvent->angleDelta(), wheelEvent->buttons(), wheelEvent->modifiers(),
          wheelEvent->phase(), wheelEvent->inverted());
      QApplication::sendEvent(collectionTreeWidget, &forwardedEvent);
      event->accept();
      return true;
    }

    event->accept();
    return true;
  }

  if ((collectionTreeWidget) &&
      (obj == collectionTreeWidget || obj == collectionTreeWidget->viewport()) &&
      event->type() == QEvent::MouseButtonPress) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    auto *src = static_cast<QWidget *>(obj);
    QPoint vpPos = collectionTreeWidget->viewport()->mapFrom(src, mouseEvent->pos());
    const QTreeWidgetItem *hit = collectionTreeWidget->itemAt(vpPos);
    if (!hit) {
      // Deselecting discards the form's route into m_workingCollections, so
      // run the same Save/Discard/Cancel gate a collection switch uses before
      // clearing. On Cancel, swallow the click and keep the selection. After
      // Save/Discard the state is clean, so TreeManager's own deselect gate
      // (driven by clearSelection() below) passes without a second prompt.
      if (!resolveUnsavedChanges(tr("deselecting"), true)) {
        event->accept();
        return true;
      }
      collectionTreeWidget->clearSelection();
      collectionTreeWidget->setCurrentItem(nullptr);
      currentTreeItem = nullptr;
      currentCollectionIndex = -1;
      event->accept();
      return true;
    }
  }

  return QDialog::eventFilter(obj, event);
}

SettingsDialog::~SettingsDialog() {
  delete ui;
}

void SettingsDialog::accept() {
  if (m_gamepadCapture) {
    m_gamepadCapture->stop();
  }
  // OK means "save and close", so commit pending edits outright rather than
  // asking (Kartend-1g46b). Routing this through resolveUnsavedChanges raised
  // "Save changes before closing the dialog?" — the prompt that belongs to
  // Cancel, where the question is genuinely open. On an OK press it asks the
  // user to repeat an intent they just expressed, and offers Discard, which
  // contradicts the button they pressed. reject() below still prompts.
  if (!commitUnsavedChanges(true)) {
    return;
  }
  // Surface QSettings disk-write failures (full disk, EROFS, permission
  // refusal) to the user instead of letting the dialog close over a stale
  // on-disk config. The user can correct the underlying problem and click
  // OK again without losing any in-dialog edits.
  if (auto result = saveGeneralSettingsFromUI(); result.isError()) {
    ErrorDialog::showError(this, result.error());
    return;
  }

  // Prompt user to rescan if database-affecting changes were saved
  if (!m_rescanRequired.isEmpty()) {
    QMessageBox::StandardButton reply =
        QMessageBox::question(this, tr("Rescan Required"),
                              tr("Some changes affect the database and require a rescan to take "
                                 "effect.\n\n"
                                 "Would you like to rescan now?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (reply == QMessageBox::Yes) {
      // Only rescan the currently viewed collection to avoid concurrent
      // database operations If the user modified multiple collections, they can
      // manually rescan others
      if (m_rescanRequired.contains(originalCurrentCollectionIndex)) {
        emit rescanRequired(originalCurrentCollectionIndex);
      } else if (!m_rescanRequired.isEmpty()) {
        // Fall back to first affected collection if current wasn't modified
        emit rescanRequired(*m_rescanRequired.begin());
      }
    }
    m_rescanRequired.clear();
  }

  QDialog::accept();
}

void SettingsDialog::reject() {
  if (m_gamepadCapture) {
    m_gamepadCapture->stop();
  }
  if (!resolveUnsavedChanges(tr("closing the dialog"), true)) {
    return;
  }
  restoreLiveAppliedSettings();
  QDialog::reject();
}

void SettingsDialog::scheduleLiveSettingsSave() {
  // Lazily constructed: most dialog sessions never touch a live-save panel.
  if (!m_liveSaveTimer) {
    m_liveSaveTimer = new QTimer(this);
    m_liveSaveTimer->setSingleShot(true);
    m_liveSaveTimer->setInterval(UIConstants::Timing::SETTINGS_LIVE_SAVE_DEBOUNCE_MS);
    connect(m_liveSaveTimer, &QTimer::timeout, this, [this]() {
      auto *mainWindow = m_host;
      auto *sm = m_ctx ? m_ctx->settingsManager() : nullptr;
      if (!mainWindow || !sm || mainWindow->isConfigReplacedOnDisk()) {
        // Config-replaced: a freshly imported kartend.cfg is on disk and the
        // app is quitting; flushing the stale in-memory struct would
        // overwrite it.
        return;
      }
      // The per-edit handlers already mirrored their state onto MainWindow's
      // live struct; this flush only pays the disk write. Errors surface the
      // same ErrorDialog the per-edit save used to show — just once per
      // settled burst instead of once per edit signal.
      auto result = sm->saveGeneralSettings(mainWindow->generalSettings());
      if (result.isError()) {
        ErrorDialog::showError(this, result.error());
      }
    });
  }
  m_liveSaveTimer->start(); // restart: the burst's last edit wins the window
}

void SettingsDialog::restoreLiveAppliedSettings() {
  // Cancel a pending debounced live-save first — this path re-persists the
  // baseline below (or intentionally leaves disk untouched when nothing was
  // live-applied), and a late timer fire would redundantly rewrite it.
  if (m_liveSaveTimer) {
    m_liveSaveTimer->stop();
  }
  if (!m_liveSettingsApplied) {
    return;
  }
  auto *mainWindow = m_host;
  auto *sm = m_ctx ? m_ctx->settingsManager() : nullptr;
  if (!mainWindow || !sm) {
    return;
  }
  // The base-color / fonts / splash panels live-save to disk + the running app
  // as the user edits, bypassing the deferred path resolveUnsavedChanges undoes.
  // The (modal) dialog is the only thing mutating MainWindow's settings while it
  // is open, and each live handler mirrors only its own fields (base color /
  // font family+size / splash sub-struct), so assigning the last-saved baseline
  // back reverts exactly those live-applied edits without disturbing anything
  // else (Kartend-9cngh).
  mainWindow->generalSettings() = m_originalGeneralSettings;
  if (!mainWindow->isConfigReplacedOnDisk()) {
    const auto result = sm->saveGeneralSettings(mainWindow->generalSettings());
    if (result.isError()) {
      ErrorDialog::showError(this, result.error());
    }
  }
  // When the config was replaced on disk (profile import, app quitting), the
  // in-memory revert above keeps the running UI consistent but the baseline
  // must NOT be written back — it would overwrite the imported [General]
  // section. The visual restores below stay unconditional.
  ItemWidget::setTitleBaseColor(m_originalGeneralSettings.appearance.titleBaseColor);
  // Repaint visible items so the reverted base color is reflected immediately
  // (Kartend-f3ivg) — mirrors the live-apply path above.
  if (auto *scroll = m_ctx ? m_ctx->scrollData() : nullptr) {
    const auto &active = scroll->getActiveWidgets();
    for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
      if (auto *w = it.value()) w->applyTitleTint();
    }
  }
  mainWindow->applyGlobalUiFontFromSettings();
}

void SettingsDialog::setupButtonConnections() {
  connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);

  connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

  connect(ui->collectionsTreeShell->addCollectionButton(), &QPushButton::clicked, this,
          &SettingsDialog::addCollection);
  connect(ui->collectionsTreeShell->removeCollectionButton(), &QPushButton::clicked, this,
          &SettingsDialog::removeCollection);
  if (ui->collectionsTreeShell->duplicateCollectionButton()) {
    connect(ui->collectionsTreeShell->duplicateCollectionButton(), &QPushButton::clicked, this,
            &SettingsDialog::duplicateCollection);
  }
  if (ui->configurationPanel->editLinkedParentsButton()) {
    connect(ui->configurationPanel->editLinkedParentsButton(), &QPushButton::clicked, this,
            &SettingsDialog::onEditLinkedParents);
  }
  // wire the Settings Mode selector. Default is `Current` to
  // preserve legacy single-collection save behavior.
  if (ui->collectionsTreeShell->settingsScopeComboBox()) {
    ui->collectionsTreeShell->settingsScopeComboBox()->setCurrentIndex(
        static_cast<int>(m_settingsScope));
    connect(ui->collectionsTreeShell->settingsScopeComboBox(),
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::onSettingsScopeChanged);
  }
  connect(ui->launcherPanel->browseLauncherButton(), &QPushButton::clicked, this,
          &SettingsDialog::browseLauncher);
  connect(ui->launcherPanel->browseCoreButton(), &QPushButton::clicked, this,
          &SettingsDialog::browseCore);
  connect(ui->configurationPanel->browseMediaDirButton(), &QPushButton::clicked, this,
          &SettingsDialog::browseMediaDir);
  // Artwork-tab browse buttons (artwork dir, video dir, manual dir,
  // placeholder artwork) live on ArtworkTabPanel now.
  // browseStartupVideoButton + browseHomeViewIconButton handlers live in
  // GeneralSettingsPanel now.
  if (ui->configurationPanel->recursiveImportContentButton()) {
    connect(ui->configurationPanel->recursiveImportContentButton(), &QPushButton::clicked, this,
            &SettingsDialog::onRecursiveImportContent);
  }
}

void SettingsDialog::setupConnections() {
  setupButtonConnections();
  setupBasicUIConnections();
  setupFormFieldConnections();
  setupTreeWidgetConnections();
  setupUIConstraints();
  setupGeneralSettingsConnections();
  setupNavigation();
}

void SettingsDialog::onSettingsScopeChanged(int comboIndex) {
  // clamp combo index defensively in case the.ui file is
  // edited and adds/removes entries; only emit when the scope actually
  // changes so dependent UI doesn't churn.
  SettingsScope newScope = SettingsScope::Current;
  switch (comboIndex) {
  case 1:
    newScope = SettingsScope::CurrentAndSubcollections;
    break;
  case 2:
    newScope = SettingsScope::All;
    break;
  default:
    newScope = SettingsScope::Current;
    break;
  }
  if (newScope == m_settingsScope) {
    return;
  }
  m_settingsScope = newScope;
  applyScopeFieldGating();
  emit settingsScopeChanged(m_settingsScope);
}

void SettingsDialog::applyScopeFieldGating() {
  // when the user picks a wider scope, edits to fields outside
  // the curated propagation subset only ever affect the currently-selected
  // collection. Disable those controls so the UI matches the propagation
  // behavior — paths, extensions, launcher/core, extract & scan flags, and
  // parent linkage. The list mirrors copyAppearanceAndLayoutFields()'s
  // exclusions in settingsdialogtree.cpp.
  const bool enabled = (m_settingsScope == SettingsScope::Current);
  QWidget *const gatedFields[] = {
      ui->configurationPanel->parentCollectionComboBox(),
      ui->configurationPanel->mediaDirLineEdit(),
      ui->configurationPanel->browseMediaDirButton(),
      ui->configurationPanel->recursiveImportContentButton(),
      ui->artworkPanel,
      ui->configurationPanel->fileExtensionsLineEdit(),
      ui->launcherPanel->launcherLineEdit(),
      ui->launcherPanel->browseLauncherButton(),
      ui->launcherPanel->coreLineEdit(),
      ui->launcherPanel->browseCoreButton(),
      ui->launcherPanel->launchParamsLineEdit(),
      ui->launcherPanel->launcherNameLineEdit(),
      ui->launcherPanel->additionalLaunchersList(),
      ui->launcherPanel->addAdditionalLauncherButton(),
      ui->launcherPanel->editAdditionalLauncherButton(),
      ui->launcherPanel->removeAdditionalLauncherButton(),
      ui->launcherPanel->defaultLauncherComboBox(),
      ui->launcherPanel->extractArchivesCheckBox(),
      ui->launcherPanel->extractedExtensionLineEdit(),
      ui->subfoldersPanel,
      ui->configurationPanel,
  };
  for (QWidget *w : gatedFields) {
    if (w) {
      w->setEnabled(enabled);
    }
  }
}

auto SettingsDialog::spacingInternalToUi(int spacing) -> int {
  return spacing - UIConstants::Viewport::SPACING_MIN;
}

auto SettingsDialog::spacingUiToInternal(int spacing) -> int {
  return spacing + UIConstants::Viewport::SPACING_MIN;
}

// Add a new collection, optionally inheriting from current selection;
// initialize defaults.
auto SettingsDialog::promptUnsavedChanges(const QString &actionDescription)
    -> QMessageBox::StandardButton {
  QMessageBox messageBox(this);
  messageBox.setIcon(QMessageBox::Warning);
  messageBox.setWindowTitle(tr("Unsaved Changes"));
  messageBox.setText(tr("Save changes before %1?").arg(actionDescription.trimmed()));
  messageBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  messageBox.setDefaultButton(QMessageBox::Save);
  return static_cast<QMessageBox::StandardButton>(messageBox.exec());
}

namespace {
// Refcounted across every live instance — the main window queries
// this via isAnyInstanceVisible() to skip its focus-return splash
// when the user dismisses the settings dialog.
int g_settingsVisibleInstanceCount = 0;
} // namespace

bool SettingsDialog::isAnyInstanceVisible() {
  return g_settingsVisibleInstanceCount > 0;
}

void SettingsDialog::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  // Per-instance guard: Qt does not guarantee one hideEvent per showEvent
  // across versions (Qt 6.8 double-fires showEvent on an already-visible
  // window), so a raw ++/-- pair drifts. Count this instance at most once.
  if (!m_countedVisible) {
    m_countedVisible = true;
    ++g_settingsVisibleInstanceCount;
  }
}

void SettingsDialog::hideEvent(QHideEvent *event) {
  if (m_countedVisible) {
    m_countedVisible = false;
    --g_settingsVisibleInstanceCount;
  }
  // Persist dialog geometry + rail splitter position so the layout the user
  // settled on is restored the next time the dialog opens.
  saveDialogState();
  QDialog::hideEvent(event);
}
