// Sibling TU: UI setup/init for MainWindow.
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPixmapCache>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSize>
#include <QTimer>
#include <QToolButton>

#include "animationmanager.h"
#include "applicationmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "detailpagemanager.h"
#include "detailspane.h"
#include "firstrunwizard.h"
#include "gridwidthdebouncer.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "itemartworklinksdialog.h"
#include "itemwidget.h"
#include "kartmanager.h"
#include "keyboardmanager.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "marqueecontroller.h"
#include "menucontroller.h"
#include "mousemanager.h"
#include "navigationmanager.h"
#include "playlistmanager.h"
#include "propertyutils.h"
#include "scrapercredentialsdialog.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "toolbarcontroller.h"
#include "viewportmanager.h"

#include "detailpageoverlay.h"
#include "detailspanemanager.h"
#include "isettingsdialog.h"
#include "isettingsmanager.h"
#include "nowplayingoverlay.h"
#include "overlaylayermanager.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "settingsutils.h"
#include "shortcutsdialog.h"
#include "splashoverlay.h"
#include "stringutils.h"
#include "textzoomhud.h"
#include "ui_mainwindow.h"
#include "uiconstants/dialog.h"
#include "uiconstants/grid.h"
#include "uiconstants/icons.h"
#include "videothumbnailextractor.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcMainWindow)

std::function<std::unique_ptr<ISettingsDialog>(QWidget *, const QList<CollectionConfig> &, int,
                                               std::function<void(const QList<CollectionConfig> &)>,
                                               std::function<void(int)>)>
MainWindow::makeSettingsDialogFactory() {
  return [](QWidget *parent, const QList<CollectionConfig> &initialCollections, int initialIndex,
            std::function<void(const QList<CollectionConfig> &)> onCollectionSaved,
            std::function<void(int)> onRescanRequired) -> std::unique_ptr<ISettingsDialog> {
    auto dlg = std::make_unique<SettingsDialog>(parent, initialCollections, initialIndex);
    // Wire the concrete Qt signals here — they cannot cross to the neutral
    // ISettingsDialog interface. The dialog object is the connection context
    // so each connection is torn down with the dialog.
    if (onCollectionSaved) {
      QObject::connect(dlg.get(), &SettingsDialog::collectionSaved, dlg.get(),
                       std::move(onCollectionSaved));
    }
    if (onRescanRequired) {
      QObject::connect(dlg.get(), &SettingsDialog::rescanRequired, dlg.get(),
                       std::move(onRescanRequired));
    }
    return dlg;
  };
}

void MainWindow::setupUI() {
  setAcceptDrops(true);

  // Managers are initialized by ApplicationManager in the constructor
  getSessionManager()->initialize();

  // Load settings (INI is small — keep eager).
  getSettingsManager()->loadCollections(m_collections);

  // Kartend-s241: full resyncPlaylistCollections is deferred to a post-
  // showEvent QTimer below, because PlaylistManager::loadAll() hits SQLite
  // on the GUI thread (~70ms typical, way worse on cold cache). The
  // hierarchy cache MUST be populated synchronously here though — many
  // downstream wiring paths and the integration-test fixture read
  // m_appContext.hierarchyCache via the context, and they ran before the
  // deferred resync would fire. Empty cache with no playlists is the
  // correct startup state; the deferred call appends playlists + rebuilds.
  if (PlaylistManager *playlistManager = getPlaylistManager()) {
    playlistManager->initialize();
    QObject::connect(playlistManager, &PlaylistManager::playlistsChanged, this,
                     [this]() { resyncPlaylistCollections(); });
  }
  rebuildHierarchyCache();
  // Defer playlist resync one event-loop tick so the freshly built hierarchy
  // cache has settled before resyncPlaylistCollections() reads it. Running
  // synchronously here would race rebuildHierarchyCache's downstream
  // signal-driven updates and produce a half-built collection list.
  QTimer::singleShot(0, this, [this]() { resyncPlaylistCollections(); });

  getSettingsManager()->loadGeneralSettings(m_generalSettings);

  // publish the persisted text-zoom multiplier into the static
  // before any widget is constructed below — the upcoming applyGlobalUiFont
  // and the scrollManager / sidebar setup paths all read zoomedFontSize().
  primeTextZoomFromSettings(m_generalSettings.uiTextZoomPercent);

  // push the persisted global UI font to QApplication before any
  // widgets are constructed below, so menus/dialogs/toolbar all pick it up on
  // their first show without a fontChange roundtrip.
  applyGlobalUiFont(m_generalSettings);

  // Apply text appearance settings to ItemWidget statics
  ItemWidget::setTitleTintSaturation(m_generalSettings.titleTintSaturation);
  ItemWidget::setTitleTintLightness(m_generalSettings.titleTintLightness);
  ItemWidget::setTitleBaseColor(m_generalSettings.titleBaseColor);

  ItemWidget::setShowTitleInPlaceholder(m_generalSettings.showTitleInPlaceholder);

  setupUIReferences();

  // Debounced persistence + refresh for menu-driven grid width changes.
  // The helper owns the three QTimers and the generation counters; the
  // wired callbacks here add the shutdown-guard wrapping that the helper
  // intentionally stays out of.
  if (!m_gridWidthDebouncer) {
    m_gridWidthDebouncer = new GridWidthDebouncer(this);
    m_gridWidthDebouncer->wire(
        [this]() {
          if (m_isShuttingDown || QApplication::closingDown()) {
            return;
          }
          if (getSettingsManager()) {
            getSettingsManager()->saveCollections(m_collections);
          }
        },
        [this]() {
          if (m_isShuttingDown || QApplication::closingDown()) {
            return;
          }
          if (!getScrollManager()) {
            return;
          }
          getScrollManager()->preCalculateLayout();
          getScrollManager()->forceVirtualViewUpdate();
        },
        [this]() {
          if (m_isShuttingDown || QApplication::closingDown()) {
            return;
          }
          if (!getScrollManager()) {
            return;
          }
          getScrollManager()->updateVirtualView();
          if (getArtworkManager()) {
            getArtworkManager()->updateViewportArtwork();
          }
          getScrollManager()->centerHorizontalScrollbar(currentCollectionIndex, m_collections);
        });
  }

  // Wire the marquee controller. The borrowed ctx pointer is filled in-place
  // by initializeAppContext() below; the controller reads it lazily so this
  // ordering is safe.
  if (m_marqueeController) {
    MarqueeControllerSetup marqueeSetup;
    marqueeSetup.ctx = &m_appContext;
    marqueeSetup.generalSettings = &m_generalSettings;
    marqueeSetup.currentCollectionIndex = &currentCollectionIndex;
    marqueeSetup.collections = &m_collections;
    marqueeSetup.isShuttingDown = [this]() { return m_isShuttingDown; };
    m_marqueeController->setupReferences(marqueeSetup);
  }

  initializeAppContext();
  createMenuBar();
  setupSidebar();
  setupManagerConnections();
  setupArtworkManager();
  setupLastSelectedIndices();
  setupEventFilters();
  // apply persisted toolbar visibility/text overrides to the
  // freshly-constructed toolbar widgets before any layout settles.
  applyToolbarCustomization();
  // open the secondary-monitor marquee window if the user enabled it.
  // Goes through the same code path as a settings-save reapply so the
  // startup and after-save behaviours stay in lockstep.
  applyMarqueeSettings();
  // bind Ctrl+= / Ctrl+- / Ctrl+0 after managers are wired so
  // applyTextZoom() can refresh the scroll/sidebar pipeline on press.
  setupTextZoomShortcuts();
  setupVideoPauseShortcut();
  setupPreviewVolumeSlider();
  setupInitialTimers();
}

void MainWindow::showEvent(QShowEvent *event) {
  QMainWindow::showEvent(event);

  // One-shot reconcile at startup: drop items/collections rows left
  // orphaned by past collection renames or removals so the Statistics
  // totals line up with the live collections without needing a
  // settings-save round trip. Deferred to QTimer::singleShot(0) inside
  // showEvent so it lands after the first paint — purgeOrphanCollectionData
  // iterates the items table and runs multiple seconds on large libraries,
  // freezing an unpainted window if invoked synchronously from setupUI.
  // Gating on showEvent keeps the work out of nested event loops opened
  // by modal dialogs in test harnesses that never call show().
  if (m_deferredStartupDone) {
    return;
  }
  m_deferredStartupDone = true;

  // Defer the orphan purge off the showEvent's critical path so the window
  // paints (and the user sees the first frame) before the multi-second
  // table scan starts. Running synchronously inside showEvent blocks paint
  // and produces a visibly-frozen window on large libraries.
  QTimer::singleShot(0, this, [this]() {
    if (m_isShuttingDown || QApplication::closingDown()) {
      return;
    }
    if (getDatabaseManager()) {
      getDatabaseManager()->purgeOrphanCollectionData(m_collections);
    }
  });
}

void MainWindow::setupUIReferences() {
  setWindowTitle("Kartend");

  // Apply user-configured pixmap cache size. Routes through the shared
  // helper so QPixmapCache AND CacheManager::artworkCache both pick up
  // the user setting in lockstep (Kartend-10pb).
  applyPixmapCacheBudget(m_generalSettings.pixmapCacheSizeMB);

  VideoThumbnailExtractor::instance()->setExtractionTimeoutMs(
      m_generalSettings.videoThumbnailExtractionTimeoutMs);

  stackedWidget = ui->stackedWidget;
  itemsPage = ui->itemsPage;
  gridContainer = ui->gridContainer;
  itemGrid = ui->itemGrid;
  m_mainContentWidget = ui->m_mainContentWidget;
  m_mainHorizontalLayout = ui->m_mainHorizontalLayout;
  searchBar = ui->searchBar;

  // Hand the toolbar's stateful Qt widgets (layout-picker / filter button /
  // search-bar inline action) over to the controller, then run its setup
  // pass. The controller owns every QAction it creates from here on.
  if (!m_toolbarController) {
    m_toolbarController = new ToolbarController(this);
  }
  ToolbarController::Setup tcSetup;
  tcSetup.mainWindow = this;
  tcSetup.viewModeButton = ui->viewModeButton;
  tcSetup.filterButton = ui->filterButton;
  tcSetup.homeButton = ui->homeButton;
  tcSetup.searchBar = ui->searchBar;
  m_toolbarController->initialize(tcSetup);
  m_toolbarController->setupViewModeButton();
  m_toolbarController->setupSearchModeAction();
  m_toolbarController->setupHomeButton();
  m_toolbarController->refreshHomeButton(m_generalSettings);
  if (ui->filterButton) {
    ui->filterButton->setIcon(
        UIConstants::Icons::fromTheme({UIConstants::Icons::FILTER, "view-filter"}));
    ui->filterButton->setIconSize(QSize(18, 18));
  }
  m_MetadataSidebar = ui->detailsPaneWidget;

  // Prevent scroll area from stealing keyboard focus - we handle
  // PageUp/PageDown and arrow keys ourselves via the event filter, and
  // QScrollArea's built-in keyboard handling would consume those events before
  // our filter sees them
  if (ui->itemScrollArea) {
    ui->itemScrollArea->setFocusPolicy(Qt::NoFocus);
    if (ui->itemScrollArea->viewport()) {
      ui->itemScrollArea->viewport()->setFocusPolicy(Qt::NoFocus);
    }
  }

  // Create loading overlay (parented to central widget so it's above all
  // content)
  m_loadingOverlay = new LoadingOverlay(ui->centralwidget);

  // Create transient splash overlay above the same central content. It stays
  // independent from scan/loading progress overlays and manages its own timers.
  m_splashOverlay = new SplashOverlay(ui->centralwidget);

  // Persistent "Now Playing" overlay used while a runtime-tracked
  // child process is running. Stays hidden until LaunchManager signals start.
  m_nowPlayingOverlay = new NowPlayingOverlay(ui->centralwidget);

  // full-window item detail page. Created here so it's parented
  // to the central widget (covers everything underneath) and stays in the
  // QObject tree across the application lifetime; DetailPageManager drives
  // it via setupReferences below.
  m_detailPageOverlay = new DetailPageOverlay(ui->centralwidget);

  // transient pill that flashes the current text-zoom percent
  // on every Ctrl+/-/0 press. Parented to centralwidget so it floats above
  // all content and tracks parent resizes via its own eventFilter.
  m_textZoomHud = new TextZoomHud(ui->centralwidget);

  // Hand every just-constructed overlay to the central z-order coordinator.
  // The manager records each widget at its documented Layer and applies
  // raise() across the registered set in z-order whenever any overlay
  // requests bringToFront(this). Eliminates the prior bug class where two
  // overlays raising themselves on the same frame left their relative
  // order dependent on widget-tree creation luck. Manager-owned overlays
  // (selection-glow widget owned by SelectionOverlayManager, search-loading
  // widget owned by SearchLoadingOverlay) register themselves through their
  // managers' setLayerManager() in connect-setup code below; the loose
  // top-level overlays we just constructed are registered here directly.
  if (m_overlayLayerManager) {
    m_overlayLayerManager->registerOverlay(m_loadingOverlay, OverlayZOrderRegistry::Layer::Loading);
    m_overlayLayerManager->registerOverlay(m_splashOverlay, OverlayZOrderRegistry::Layer::Splash);
    m_overlayLayerManager->registerOverlay(m_nowPlayingOverlay,
                                           OverlayZOrderRegistry::Layer::NowPlaying);
    m_overlayLayerManager->registerOverlay(m_detailPageOverlay,
                                           OverlayZOrderRegistry::Layer::DetailPage);
    m_overlayLayerManager->registerOverlay(m_textZoomHud,
                                           OverlayZOrderRegistry::Layer::TextZoomHud);
    m_loadingOverlay->setLayerManager(m_overlayLayerManager.get());
    m_splashOverlay->setLayerManager(m_overlayLayerManager.get());
    m_nowPlayingOverlay->setLayerManager(m_overlayLayerManager.get());
    m_detailPageOverlay->setLayerManager(m_overlayLayerManager.get());
    m_textZoomHud->setLayerManager(m_overlayLayerManager.get());
  }
}

void MainWindow::applyPixmapCacheBudget(int megabytes) {
  // QPixmapCache::setCacheLimit takes KB; our settings store MB.
  QPixmapCache::setCacheLimit(megabytes * 1024);
  // CacheManager::artworkCache budget is owned by the manager itself —
  // before getCacheManager() is wired up (very early startup), the
  // CacheManager holds its construction-time legacy default and will
  // pick up the user value on the next call.
  if (auto *cm = getCacheManager()) {
    cm->setArtworkCacheBudgetMB(megabytes);
  }
}

void MainWindow::initializeAppContext() {
  // Collection state
  m_appContext.collection.collections = &m_collections;
  m_appContext.collection.currentCollectionIndex = &currentCollectionIndex;
  m_appContext.collection.hierarchyCache = &m_hierarchyCache;
  m_appContext.collection.generalSettings = &m_generalSettings;
  m_appContext.collection.isShuttingDown = &m_isShuttingDown;

  // Common UI elements
  m_appContext.ui.itemScrollArea = ui->itemScrollArea;
  m_appContext.ui.stackedWidget = stackedWidget;
  m_appContext.ui.itemsPage = itemsPage;
  m_appContext.ui.itemsTopBar = ui->itemsTopBar;
  m_appContext.ui.gridContainer = gridContainer;
  m_appContext.ui.menubar = ui->menubar;
  m_appContext.ui.searchBar = searchBar;
  m_appContext.ui.searchModeAction =
      m_toolbarController ? m_toolbarController->searchModeAction() : nullptr;
  m_appContext.ui.sidebar = m_MetadataSidebar;
  m_appContext.ui.loadingLabel = ui->loadingLabel;
  m_appContext.ui.loadingOverlay = m_loadingOverlay;
  m_appContext.ui.overlayLayerManager = m_overlayLayerManager.get();

  // Top-level managers — registered eagerly so ctx is fully populated before
  // any manager's setupReferences() runs.
  m_appContext.managers.scrollManager = getScrollManager();
  m_appContext.managers.artworkManager = getArtworkManager();
  m_appContext.managers.settingsManager = getSettingsManager();
  m_appContext.managers.sessionManager = getSessionManager();
  m_appContext.managers.detailsPaneManager = getDetailsPaneManager();
  m_appContext.managers.detailPageManager = getDetailPageManager();
  m_appContext.managers.databaseManager = getDatabaseManager();
  m_appContext.managers.navigationManager = getNavigationManager();
  m_appContext.managers.interactionManager = getInteractionManager();
  m_appContext.managers.playlistManager = getPlaylistManager();
  m_appContext.managers.cacheManager = getCacheManager();

  // InteractionManager-owned sub-managers exist as soon as InteractionManager
  // is constructed (its ctor allocates them via std::make_unique). Register
  // them here, before any setupReferences() runs, so dependents can resolve
  // siblings exclusively through ctx.
  if (auto *im = getInteractionManager()) {
    m_appContext.managers.animationManager = im->animationManager();
    m_appContext.managers.selectionManager = im->selectionManager();
    m_appContext.managers.viewportManager = im->viewportManager();
    m_appContext.managers.mouseManager = im->mouseManager();
    m_appContext.managers.keyboardManager = im->keyboardManager();
    m_appContext.managers.eventManager = im->eventManager();
    m_appContext.managers.searchManager = im->searchManager();
    m_appContext.managers.launchManager = im->launchManager();
    m_appContext.managers.interactionState = &im->state();
  }
}

void MainWindow::createMenuBar() {
  m_menuController = std::make_unique<MenuController>(this);

  MenuControllerContext ctx;
  ctx.mainWindow = this;
  ctx.ui = ui;
  ctx.getNavigationManager = [this]() { return getNavigationManager(); };
  ctx.getSettingsManager = [this]() { return getSettingsManager(); };
  ctx.getDetailsPaneManager = [this]() { return getDetailsPaneManager(); };
  ctx.getScrollManager = [this]() { return getScrollManager(); };
  ctx.getArtworkManager = [this]() { return getArtworkManager(); };
  ctx.getDatabaseManager = [this]() { return getDatabaseManager(); };
  ctx.getInteractionManager = [this]() { return getInteractionManager(); };
  ctx.getCurrentCollectionIndex = [this]() { return currentCollectionIndex; };
  ctx.getCollections = [this]() { return &m_collections; };
  ctx.getGeneralSettings = [this]() { return &m_generalSettings; };
  ctx.getHierarchyCache = [this]() -> const CollectionHierarchyCache * {
    return &m_hierarchyCache;
  };
  ctx.getCurrentViewType = [this]() {
    if (currentCollectionIndex >= 0 && currentCollectionIndex < m_collections.size()) {
      return m_collections[currentCollectionIndex].viewType;
    }
    return ViewType::Grid;
  };
  ctx.onSetViewType = [this](ViewType viewType) { setViewType(viewType); };
  ctx.onLaunchItem = [this](const QString &filePath, int collectionIndex) {
    if (auto *im = getInteractionManager()) {
      im->launchItemWithCollection(filePath, collectionIndex);
    }
  };
  ctx.onOpenSettings = [this]() {
    if (getSettingsManager()) {
      SettingsDialogContext context;
      context.parent = this;
      context.collections = &m_collections;
      context.currentCollectionIndex = &currentCollectionIndex;
      context.detailsPaneManager = getDetailsPaneManager();
      context.scrollManager = getScrollManager();
      context.navigationManager = getNavigationManager();
      context.databaseManager = getDatabaseManager();
      context.createSettingsDialog = makeSettingsDialogFactory();
      getSettingsManager()->openSettingsDialog(context);
    }
  };
  ctx.onShowAbout = [this]() { showAbout(); };
  ctx.onAdjustGridWidth = [this](int delta) { adjustGridWidth(delta); };
  ctx.onImportKart = [this]() {
    if (auto *km = getKartManager()) km->importInteractive();
  };
  ctx.onExportKart = [this]() {
    if (auto *km = getKartManager()) km->exportCollectionInteractive(currentCollectionIndex);
  };
  ctx.onShowFirstRunWizard = [this]() { showFirstRunWizard(); };
  ctx.onShowScraperCredentials = [this]() {
    ScraperCredentialsDialog dialog(&m_generalSettings, getSettingsManager(), this);
    dialog.exec();
  };
  ctx.onRunBatchScrape = [this]() { openScraperDialog(); };

  m_menuController->setContext(ctx);
  m_menuController->setupMenuBar();
}

void MainWindow::adjustGridWidth(int delta) {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    return;
  }

  CollectionConfig &config = m_collections[currentCollectionIndex];

  // figure out which gridWidth field is currently driving the
  // layout, and mutate that one. When sidebar is hidden in Expand mode AND the
  // alt is configured non-zero, the alt is the active field; otherwise the
  // primary gridWidth is. Sidebar shrinking state is cached on ScrollManager so
  // we don't have to recompute the predicate here.
  const bool useAltField = getScrollManager() && getScrollManager()->sidebarShrinkingActive() &&
                           config.gridLayout.gridWidthSidebarHidden > 0;
  int &activeField =
      useAltField ? config.gridLayout.gridWidthSidebarHidden : config.gridLayout.gridWidth;

  int newWidth = activeField + delta;

  // Floor only — no upper cap (4K/8K layouts go past the old 40 limit).
  newWidth = qMax(UIConstants::Grid::MIN_WIDTH, newWidth);

  if (newWidth == activeField) {
    return; // No change needed
  }

  // Update the collection config
  activeField = newWidth;

  // Persist the change (debounced) to avoid repeated disk writes when the user
  // holds the shortcut.
  if (m_gridWidthDebouncer) {
    m_gridWidthDebouncer->triggerSave();
  } else if (getSettingsManager()) {
    getSettingsManager()->saveCollections(m_collections);
  }

  // Apply the change to the UI using the same flow as settings dialog
  if (getScrollManager()) {
    getScrollManager()->updateGridWidth(newWidth);

    // Coalesce expensive layout + artwork refresh for repeated adjustments.
    if (m_gridWidthDebouncer) {
      m_gridWidthDebouncer->triggerPrecalc();
    }
  }
}

void MainWindow::setViewType(ViewType viewType) {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    return;
  }

  CollectionConfig &config = m_collections[currentCollectionIndex];
  if (config.viewType == viewType) {
    return; // No change needed
  }

  // Update the collection config
  config.viewType = viewType;

  // Persist the change immediately
  if (getSettingsManager()) {
    getSettingsManager()->saveCollections(m_collections);
  }

  // Update view-mode button checked state and label.
  syncViewModeButton(viewType);
  // keep the View → Layout submenu in sync with the toolbar.
  if (m_menuController) {
    m_menuController->syncLayoutActions(viewType);
  }

  // Trigger a full layout refresh - viewType affects widget dimensions and
  // layout
  if (getScrollManager()) {
    getScrollManager()->updateViewType(viewType);
  }
}

void MainWindow::setupSidebar() {
  if (getDetailsPaneManager()) {
    getDetailsPaneManager()->setupSidebar();

    DetailsPaneManagerSetup setup;
    setup.ctx = &m_appContext;
    setup.mainLayout = m_mainHorizontalLayout;
    // outer vertical layout + content widget enable Top/Bottom
    // dock in Expand mode. Both come straight from the .ui — no new widgets
    // needed.
    setup.outerLayout = ui->itemsPageLayout;
    setup.contentWidget = m_mainContentWidget;
    // Kartend-n8kh: the artwork-links dialog runs from here so the media
    // layer (DetailsPaneManager) doesn't #include itemartworklinksdialog.h.
    setup.runArtworkLinksDialog =
        [this](const ItemArtworkLinksInput &in) -> std::optional<QHash<QString, QString>> {
      ItemArtworkLinksDialog dialog(this);
      dialog.setItemTitle(in.itemTitle);
      dialog.setTypeRows(in.standardTypes, in.customTypes);
      dialog.setOverrides(in.overrides);
      if (!in.autoResolvedPaths.isEmpty()) {
        dialog.setAutoResolvedPaths(in.autoResolvedPaths);
      }
      if (!in.browseStartDir.isEmpty()) {
        dialog.setBrowseStartDirectory(in.browseStartDir);
      }
      if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
      }
      return dialog.overrides();
    };

    getDetailsPaneManager()->setupReferences(setup);

    QObject::connect(getDetailsPaneManager(), &DetailsPaneManager::sidebarVisibilityChanged, this,
                     [this](bool visible) {
                       if (ui->actionShowSidebar) {
                         ui->actionShowSidebar->blockSignals(true);
                         ui->actionShowSidebar->setChecked(visible);
                         ui->actionShowSidebar->blockSignals(false);
                       }
                     });
  }
}

void MainWindow::showAbout() {
  QString appName = APP_DISPLAY_NAME;
  QString appVersion = APP_VERSION;
  QString appAuthor = APP_AUTHOR;

  QString buildDate = BUILD_DATE;
  buildDate.replace("_SPACE_", " ");

  if (buildDate.isEmpty()) {
    buildDate = "[BUILD_DATE not set]";
  }

  QString aboutText = QString("<h3>%1 <span style='font-size: medium; "
                              "font-weight: normal;'>v%2</span></h3>"
                              "<p>Founded by %3</p>"
                              "<p>Build Date: %4</p>")
                          .arg(appName, appVersion, appAuthor, buildDate);

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("About");
  msgBox.setText(aboutText);
  msgBox.setTextFormat(Qt::RichText);
  msgBox.setStandardButtons(QMessageBox::Ok);
  msgBox.resize(UIConstants::Dialog::ABOUT_WIDTH, UIConstants::Dialog::ABOUT_HEIGHT);
  msgBox.exec();
}

void MainWindow::showFirstRunWizard() {
  FirstRunWizard wizard(this);
  wizard.exec();
  const auto result = wizard.result();

  if (result.accepted && !result.pickedConfig.mediaDirectory.isEmpty()) {
    // Mirrors the post-add sequence in setupInitialTimersEmptyCollections:
    // append → save → rebuild hierarchy → navigate. Skipping any of these
    // would leave the freshly-created collection invisible until the next
    // restart.
    m_collections.append(result.pickedConfig);
    if (getSettingsManager()) {
      getSettingsManager()->saveCollections(m_collections);
    }
    rebuildHierarchyCache();
    if (getNavigationManager()) {
      currentCollectionIndex = m_collections.size() - 1;
      getNavigationManager()->showCollectionItems(currentCollectionIndex);
    }
  }

  // Always flip firstRunComplete — even when the user skipped without
  // picking a folder. They saw the wizard; auto-launching it again would
  // be obnoxious. Re-running stays available via Help → Setup Wizard…
  m_generalSettings.firstRunComplete = true;
  if (getSettingsManager()) {
    getSettingsManager()->saveGeneralSettings(m_generalSettings);
  }
}

void MainWindow::setupArtworkManager() {
  if (!getArtworkManager()) return;
  ArtworkManager &artMgr = *getArtworkManager();

  // Kartend-davi: setupReferences must precede initializeCache because the
  // manager now reads its CacheManager through ctx instead of caching a
  // raw pointer at construction.
  ArtworkManagerSetup setup;
  setup.ctx = &m_appContext;
  artMgr.setupReferences(setup);

  artMgr.initializeCache();
}

void MainWindow::setupLastSelectedIndices() {
  if (!getSessionManager()) return;

  for (int i = 0; i < m_collections.size(); ++i) {
    int sel = getSessionManager()->getLastSelectedIndex(m_collections[i].name);
    if (sel < 0) {
      QString hierarchical = CollectionUtils::hierarchicalNameFor(m_collections[i], m_collections);
      if (!hierarchical.isEmpty() && hierarchical != m_collections[i].name) {
        int hSel = getSessionManager()->getLastSelectedIndex(hierarchical);
        if (hSel >= 0) {
          sel = hSel;
        }
      }
    }
    if (sel >= 0) {
      getSettingsManager()->setLastSelectedItem(i, sel);
    }
  }
}

void MainWindow::setupEventFilters() {
  if (!ui) {
    return;
  }

  ScrollManagerSetup setup;
  setup.ctx = &m_appContext;

  getScrollManager()->setupReferences(setup);

  if (ui->itemScrollArea) {
    ui->itemScrollArea->installEventFilter(getInteractionManager());
    if (ui->itemScrollArea->viewport()) {
      ui->itemScrollArea->viewport()->installEventFilter(getInteractionManager());
    }
  }
  if (gridContainer) {
    gridContainer->installEventFilter(getInteractionManager());
  }
}
