// Sibling TU: UI setup/init for MainWindow.
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
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
#include "artworkcandidates.h"
#include "artworkmanager.h"
#include "artworkwizarddialog.h"
#include "bindingvisualizerdialog.h"
#include "bulkedit.h"
#include "bulkeditdialog.h"
#include "cachemanager.h"
#include "collection/presentationprofile.h"
#include "collection/themepreset.h"
#include "collectionfilesystemwatcher.h"
#include "collectionhealth.h"
#include "collectionhealthdialog.h"
#include "collectionutils.h"
#include "commandpalettedialog.h"
#include "detailpagemanager.h"
#include "detailspane.h"
#include "dialogcontroller.h"
#include "gamepadmanager.h"
#include "gridwidthdebouncer.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "itemartwork.h"
#include "itemwidget.h"
#include "kartmanager.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "layoutprofilesdialog.h"
#include "libraryonboardingwizard.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "marqueecontroller.h"
#include "menucontroller.h"
#include "metadataqueue.h"
#include "metadatareviewdialog.h"
#include "mousemanager.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "playlistmanager.h"
#include "presentationprofilesdialog.h"
#include "propertyutils.h"
#include "scraperprovidersdialog.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "toolbarcontroller.h"
#include "variantgrouping.h"
#include "variantgroupingdialog.h"
#include "viewportmanager.h"

#include "detailpageoverlay.h"
#include "detailspanemanager.h"
#include "isettingsmanager.h"
#include "nowplayingoverlay.h"
#include "overlaylayermanager.h"
#include "sessionmanager.h"
#include "settingsutils.h"
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

// Factory body moved to DialogController::makeSettingsDialogFactory.

void MainWindow::setupUI() {
  setAcceptDrops(true);

  // Managers are initialized by ApplicationManager in the constructor
  m_appManager->getSessionManager()->initialize();

  // Load settings (INI is small — keep eager).
  m_appManager->getSettingsManager()->loadCollections(m_collections);

  // Kartend-s241: full resyncPlaylistCollections is deferred to a post-
  // showEvent QTimer below, because PlaylistManager::loadAll() hits SQLite
  // on the GUI thread (~70ms typical, way worse on cold cache). The
  // hierarchy cache MUST be populated synchronously here though — many
  // downstream wiring paths and the integration-test fixture read
  // m_appContext.hierarchyCache via the context, and they ran before the
  // deferred resync would fire. Empty cache with no playlists is the
  // correct startup state; the deferred call appends playlists + rebuilds.
  if (PlaylistManager *playlistManager = m_appManager->getPlaylistManager()) {
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

  m_appManager->getSettingsManager()->loadGeneralSettings(m_generalSettings);

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
          if (m_appManager->getSettingsManager()) {
            m_appManager->getSettingsManager()->saveCollections(m_collections);
          }
        },
        [this]() {
          if (m_isShuttingDown || QApplication::closingDown()) {
            return;
          }
          if (!m_appManager->getScrollManager()) {
            return;
          }
          m_appManager->getScrollManager()->preCalculateLayout();
          m_appManager->getScrollManager()->forceVirtualViewUpdate();
        },
        [this]() {
          if (m_isShuttingDown || QApplication::closingDown()) {
            return;
          }
          if (!m_appManager->getScrollManager()) {
            return;
          }
          m_appManager->getScrollManager()->updateVirtualView();
          if (m_appManager->getArtworkManager()) {
            m_appManager->getArtworkManager()->updateViewportArtwork();
          }
          m_appManager->getScrollManager()->centerHorizontalScrollbar(currentCollectionIndex,
                                                                      m_collections);
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
  refreshCollectionFilesystemWatcher();
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
  setupCommandPaletteShortcut();
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
    if (m_appManager->getDatabaseManager()) {
      m_appManager->getDatabaseManager()->purgeOrphanCollectionData(m_collections);
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
  // before m_appManager->getCacheManager() is wired up (very early startup), the
  // CacheManager holds its construction-time legacy default and will
  // pick up the user value on the next call.
  if (auto *cm = m_appManager->getCacheManager()) {
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
  m_appContext.managers.scrollManager = m_appManager->getScrollManager();
  if (auto *sm = m_appManager->getScrollManager()) {
    // ScrollManager's ctor already wired m_filterManager off DataSourceCoordinator,
    // so this alias is non-null the moment ScrollManager exists (Kartend-yeik).
    m_appContext.managers.filterManager = sm->filterManager();
  }
  m_appContext.managers.artworkManager = m_appManager->getArtworkManager();
  m_appContext.managers.settingsManager = m_appManager->getSettingsManager();
  m_appContext.managers.sessionManager = m_appManager->getSessionManager();
  m_appContext.managers.detailsPaneManager = m_appManager->getDetailsPaneManager();
  m_appContext.managers.detailPageManager = m_appManager->getDetailPageManager();
  m_appContext.managers.databaseManager = m_appManager->getDatabaseManager();
  m_appContext.managers.navigationManager = m_appManager->getNavigationManager();
  m_appContext.managers.interactionManager = m_appManager->getInteractionManager();
  m_appContext.managers.playlistManager = m_appManager->getPlaylistManager();
  m_appContext.managers.cacheManager = m_appManager->getCacheManager();

  // InteractionManager-owned sub-managers exist as soon as InteractionManager
  // is constructed (its ctor allocates them via std::make_unique). Register
  // them here, before any setupReferences() runs, so dependents can resolve
  // siblings exclusively through ctx.
  if (auto *im = m_appManager->getInteractionManager()) {
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
  ctx.getNavigationManager = [this]() { return m_appManager->getNavigationManager(); };
  ctx.getSettingsManager = [this]() { return m_appManager->getSettingsManager(); };
  ctx.getDetailsPaneManager = [this]() { return m_appManager->getDetailsPaneManager(); };
  ctx.getScrollManager = [this]() { return m_appManager->getScrollManager(); };
  ctx.getArtworkManager = [this]() { return m_appManager->getArtworkManager(); };
  ctx.getDatabaseManager = [this]() { return m_appManager->getDatabaseManager(); };
  ctx.getInteractionManager = [this]() { return m_appManager->getInteractionManager(); };
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
    if (auto *im = m_appManager->getInteractionManager()) {
      im->launchItemWithCollection(filePath, collectionIndex);
    }
  };
  ctx.onOpenSettings = [this]() {
    if (m_appManager->getSettingsManager()) {
      SettingsDialogContext context;
      context.parent = this;
      context.collections = &m_collections;
      context.currentCollectionIndex = &currentCollectionIndex;
      context.detailsPaneManager = m_appManager->getDetailsPaneManager();
      context.scrollManager = m_appManager->getScrollManager();
      context.navigationManager = m_appManager->getNavigationManager();
      context.databaseManager = m_appManager->getDatabaseManager();
      context.createSettingsDialog = DialogController::makeSettingsDialogFactory();
      m_appManager->getSettingsManager()->openSettingsDialog(context);
      // Settings may have flipped watchFilesystem on/off or changed a
      // mediaDirectory; reconcile the watch set so the next file event
      // lands on the right collection.
      refreshCollectionFilesystemWatcher();
    }
  };
  ctx.onShowAbout = [this]() { showAbout(); };
  ctx.onAdjustGridWidth = [this](int delta) { adjustGridWidth(delta); };
  ctx.onImportKart = [this]() {
    if (auto *km = m_appManager->getKartManager()) km->importInteractive();
  };
  ctx.onExportKart = [this]() {
    if (auto *km = m_appManager->getKartManager())
      km->exportCollectionInteractive(currentCollectionIndex);
  };
  ctx.onImportTheme = [this]() { importThemeInteractive(); };
  ctx.onExportTheme = [this]() { exportThemeInteractive(); };
  ctx.onManageLayoutProfiles = [this]() { manageLayoutProfilesInteractive(); };
  ctx.onShowCollectionHealth = [this]() { showCollectionHealthInteractive(); };
  ctx.onShowVariantGrouping = [this]() { showVariantGroupingInteractive(); };
  ctx.onNavigateToItem = [this](const QString &filePath) { navigateToItem(filePath); };
  ctx.onBulkEdit = [this]() { bulkEditInteractive(); };
  ctx.onReviewMissingMetadata = [this]() { reviewMissingMetadataInteractive(); };
  ctx.onArtworkWizard = [this]() { artworkWizardInteractive(); };
  ctx.onShowBindings = [this]() { showBindingVisualizer(); };
  ctx.onNewLibraryWizard = [this]() { runNewLibraryWizard(); };
  ctx.onPresentationProfiles = [this]() { managePresentationProfilesInteractive(); };
  ctx.onShowScraperProviders = [this]() { showScraperProvidersInteractive(); };
  ctx.onShowFirstRunWizard = [this]() { showFirstRunWizard(); };
  ctx.onShowScraperCredentials = [this]() {
    m_dialogController->runScraperCredentialsDialog(&m_generalSettings,
                                                    m_appManager->getSettingsManager());
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
  const bool useAltField = m_appManager->getScrollManager() &&
                           m_appManager->getScrollManager()->sidebarShrinkingActive() &&
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
  } else if (m_appManager->getSettingsManager()) {
    m_appManager->getSettingsManager()->saveCollections(m_collections);
  }

  // Apply the change to the UI using the same flow as settings dialog
  if (m_appManager->getScrollManager()) {
    m_appManager->getScrollManager()->updateGridWidth(newWidth);

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
  if (m_appManager->getSettingsManager()) {
    m_appManager->getSettingsManager()->saveCollections(m_collections);
  }

  // Update view-mode button checked state and label.
  syncViewModeButton(viewType);
  // keep the View → Layout submenu in sync with the toolbar.
  if (m_menuController) {
    m_menuController->syncLayoutActions(viewType);
  }

  // Trigger a full layout refresh - viewType affects widget dimensions and
  // layout
  if (m_appManager->getScrollManager()) {
    m_appManager->getScrollManager()->updateViewType(viewType);
  }
}

void MainWindow::setupSidebar() {
  if (m_appManager->getDetailsPaneManager()) {
    m_appManager->getDetailsPaneManager()->setupSidebar();

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
    setup.runArtworkLinksDialog = [this](const ItemArtworkLinksInput &in) {
      return m_dialogController->runArtworkLinksDialog(in);
    };
    // Kartend-oewu: route the DetailsPane's inline edit-metadata button
    // through InteractionManager so the dialog, persistence, and sidebar
    // refresh all match the right-click "Edit metadata…" entry exactly.
    setup.runEditMetadataForItem = [this](const QString &filePath, const QString &itemName) {
      if (auto *im = m_appManager ? m_appManager->getInteractionManager() : nullptr) {
        im->editItemMetadata(filePath, itemName);
      }
    };

    m_appManager->getDetailsPaneManager()->setupReferences(setup);

    QObject::connect(m_appManager->getDetailsPaneManager(),
                     &DetailsPaneManager::sidebarVisibilityChanged, this, [this](bool visible) {
                       if (ui->actionShowSidebar) {
                         ui->actionShowSidebar->blockSignals(true);
                         ui->actionShowSidebar->setChecked(visible);
                         ui->actionShowSidebar->blockSignals(false);
                       }
                     });
  }
}

void MainWindow::refreshCollectionFilesystemWatcher() {
  if (!m_collectionWatcher) {
    m_collectionWatcher = std::make_unique<CollectionFilesystemWatcher>(this);
    m_collectionWatcher->setRescanCallback([this](int collectionIndex) {
      // Defer onto the next event-loop spin so a burst of filesystem
      // events that the debounce has already coalesced doesn't reenter
      // forceRescanCollection during the watcher's own slot dispatch.
      QTimer::singleShot(0, this, [this, collectionIndex]() {
        if (auto *nav = m_appManager ? m_appManager->getNavigationManager() : nullptr) {
          nav->forceRescanCollection(collectionIndex);
        }
      });
    });
  }
  m_collectionWatcher->configure(m_collections);
}

void MainWindow::setupArtworkManager() {
  if (!m_appManager->getArtworkManager()) return;
  ArtworkManager &artMgr = *m_appManager->getArtworkManager();

  // Kartend-davi: setupReferences must precede initializeCache because the
  // manager now reads its CacheManager through ctx instead of caching a
  // raw pointer at construction.
  ArtworkManagerSetup setup;
  setup.ctx = &m_appContext;
  artMgr.setupReferences(setup);

  artMgr.initializeCache();
}

void MainWindow::setupLastSelectedIndices() {
  if (!m_appManager->getSessionManager()) return;

  for (int i = 0; i < m_collections.size(); ++i) {
    int sel = m_appManager->getSessionManager()->getLastSelectedIndex(m_collections[i].name);
    if (sel < 0) {
      QString hierarchical = CollectionUtils::hierarchicalNameFor(m_collections[i], m_collections);
      if (!hierarchical.isEmpty() && hierarchical != m_collections[i].name) {
        int hSel = m_appManager->getSessionManager()->getLastSelectedIndex(hierarchical);
        if (hSel >= 0) {
          sel = hSel;
        }
      }
    }
    if (sel >= 0) {
      m_appManager->getSettingsManager()->setLastSelectedItem(i, sel);
    }
  }
}

void MainWindow::setupEventFilters() {
  if (!ui) {
    return;
  }

  ScrollManagerSetup setup;
  setup.ctx = &m_appContext;

  m_appManager->getScrollManager()->setupReferences(setup);

  if (ui->itemScrollArea) {
    ui->itemScrollArea->installEventFilter(m_appManager->getInteractionManager());
    if (ui->itemScrollArea->viewport()) {
      ui->itemScrollArea->viewport()->installEventFilter(m_appManager->getInteractionManager());
    }
  }
  if (gridContainer) {
    gridContainer->installEventFilter(m_appManager->getInteractionManager());
  }
}
