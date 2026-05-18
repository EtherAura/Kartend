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
#include "artworktabpanel.h"
#include "attractpanel.h"
#include "collectionremover.h"
#include "collectiontreewidget.h"
#include "configurationpanel.h"
#include "controlspanel.h"
#include "extensionutils.h"
#include "fontspanel.h"
#include "gamepadcapturecontroller.h"
#include "gamepadmanager.h"
#include "generalsettingspanel.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "launcherpresetspanel.h"
#include "launchertabpanel.h"
#include "mainwindow.h"
#include "marqueepanel.h"
#include "pathutils.h"
#include "scrapercredentialspanel.h"
#include "scrapersettingspanel.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "sidebarpanel.h"
#include "splashpanel.h"
#include "subfolderspanel.h"
#include "toolbarpanel.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

SettingsDialog::SettingsDialog(QWidget *parent, const QList<CollectionConfig> &initialCollections,
                               int initialIndex)
    : QDialog(parent), ui(new Ui::SettingsDialog), collectionTreeWidget(nullptr),
      currentTreeItem(nullptr), collections(initialCollections),
      originalCurrentCollectionIndex(initialIndex), currentCollectionIndex(initialIndex),
      m_workingCollections(initialCollections), m_gridWidthChangedForActiveCollection(false),
      m_newGridWidthForActiveCollection(0), m_collectionSaved(true), m_isLoading(false) {
  ui->setupUi(this);
  setWindowTitle(tr("Settings"));
  setModal(true);

  // Persistent save icon — lives in the top-right corner of the main
  // tab widget so its position is identical regardless of which page
  // the user is on. The previous location (inside the Collections
  // tree shell) disappeared on every other tab. Click handler +
  // dirty-glow wiring are set up alongside the other tab connections
  // below.
  m_saveButton = new QPushButton(this);
  m_saveButton->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
  m_saveButton->setToolTip(tr("Save changes"));
  m_saveButton->setFlat(true);
  m_saveButton->setMaximumSize(30, 30);
  m_saveButton->setEnabled(false);
  ui->tabWidget->setCornerWidget(m_saveButton, Qt::TopRightCorner);

  // Gamepad button-capture state machine. Constructed before
  // setupConnections() runs so the Detect-button click handlers can
  // dispatch through it.
  m_gamepadCapture = new GamepadCaptureController(this);

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

  // Multi-step collection-removal pipeline. Constructed before the
  // tree's Remove button is wired up so removeCollection() can dispatch.
  m_collectionRemover = new CollectionRemover(&m_model, this, this);

  // The presets list itself is hydrated by loadGeneralSettingsToUI(); the
  // pointer install here is one-shot because m_generalSettings lives for the
  // dialog's lifetime. The panel mutates the list in place and signals back
  // so checkForChanges() picks up preset edits without other panels going
  // through the panel API to read presets (settingsdialoglaunchers.cpp still
  // reads m_generalSettings.launcherPresets directly for its presets combo).
  ui->launcherPresetsPanel->setPresets(&m_generalSettings.launcherPresets);
  ui->launcherPresetsPanel->setRetroarchConfigOverride(m_generalSettings.retroarchConfigPath);
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
  // — host mirrors to mainWindow + saves + applies ItemWidget side effect
  // immediately to preserve the picker's instant-feedback UX.
  ui->appearanceColorsPanel->setModel(&m_model);
  connect(ui->appearanceColorsPanel, &AppearanceColorsPanel::changed, this,
          &SettingsDialog::checkForChanges);
  connect(ui->appearanceColorsPanel, &AppearanceColorsPanel::baseColorChanged, this,
          [this](const QString &c) {
            auto *mainWindow = qobject_cast<MainWindow *>(QObject::parent());
            if (!mainWindow || !mainWindow->getSettingsManager()) return;
            mainWindow->m_generalSettings.titleBaseColor = c;
            mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
            ItemWidget::setTitleBaseColor(c);
          });

  // Application-font panel: live-save semantics — panel mutates the
  // pointed-to GeneralSettings and emits changed(); we mirror to mainWindow,
  // persist via SettingsManager, and apply the font to the running app, all
  // without going through the deferred-save path.
  ui->fontsPanel->setModel(&m_model);
  connect(ui->fontsPanel, &FontsPanel::changed, this, [this]() {
    // QObject::parent() — explicit because the enclosing constructor's
    // own `parent` argument is in scope and shadows the member function.
    auto *mainWindow = qobject_cast<MainWindow *>(QObject::parent());
    if (!mainWindow || !mainWindow->getSettingsManager()) {
      return;
    }
    mainWindow->m_generalSettings = m_generalSettings;
    mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
    MainWindow::applyGlobalUiFont(mainWindow->m_generalSettings);
  });

  // Splash (boot + resume-focus) panel: same live-save shape as FontsPanel
  // minus the apply step — splashes are shown on next startup / focus event,
  // so persisting is sufficient.
  ui->splashPanel->setModel(&m_model);
  connect(ui->splashPanel, &SplashPanel::changed, this, [this]() {
    auto *mainWindow = qobject_cast<MainWindow *>(QObject::parent());
    if (!mainWindow || !mainWindow->getSettingsManager()) {
      return;
    }
    mainWindow->m_generalSettings = m_generalSettings;
    mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
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
  loadGeneralSettingsToUI();

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
  if (!resolveUnsavedChanges(tr("closing the dialog"), true)) {
    return;
  }
  saveGeneralSettingsFromUI();

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
  QDialog::reject();
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
  setupSpacingConnections();
  setupTreeWidgetConnections();
  setupUIConstraints();
  setupGeneralSettingsConnections();
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

void SettingsDialog::emitGridWidthChanged() {
  if (m_gridWidthChangedForActiveCollection && originalCurrentCollectionIndex >= 0 &&
      originalCurrentCollectionIndex < collections.size()) {
    emit gridWidthChanged(originalCurrentCollectionIndex, m_newGridWidthForActiveCollection);
    m_gridWidthChangedForActiveCollection = false;
  }
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
  ++g_settingsVisibleInstanceCount;
  // Delay grid width calculation until dialog geometry is finalized
  QTimer::singleShot(UIConstants::Timing::LONG_DELAY_MS, this,
                     &SettingsDialog::updateGridWidthLimits);
}

void SettingsDialog::hideEvent(QHideEvent *event) {
  if (g_settingsVisibleInstanceCount > 0) {
    --g_settingsVisibleInstanceCount;
  }
  QDialog::hideEvent(event);
}

void SettingsDialog::resizeEvent(QResizeEvent *event) {
  QDialog::resizeEvent(event);
  // Delay recalculation until resize animation/settling completes
  QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS, this,
                     &SettingsDialog::updateGridWidthLimits);
}
