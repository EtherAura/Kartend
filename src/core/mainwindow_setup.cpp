// Sibling TU: UI setup/init for MainWindow.
#include <algorithm>

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
#include "collection/collectionconfig.h"
#include "collection/collectionhierarchycache.h"
#include "collection/hierarchyhelpers.h"
#include "collection/presentationprofile.h"
#include "collection/themepreset.h"
#include "collectionfilesystemwatcher.h"
#include "collectionhealth.h"
#include "collectionhealthdialog.h"
#include "commandpalettedialog.h"
#include "detailpagemanager.h"
#include "detailspane.h"
#include "dialogcontroller.h"
#include "errorpresentation.h"
#include "filtermanager.h"
#include "gamepadmanager.h"
#include "gridwidthdebouncer.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "itemartwork.h"
#include "itemmetadataactioncontroller.h"
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
#include "overlayzorderregistry.h"
#include "sessionmanager.h"
#include "settingsutils.h"
#include "splashoverlay.h"
#include "stringutils.h"
#include "systemthemewatcher.h"
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

  // Surface UUID collisions — two collections sharing one DB key, so they
  // silently read and write each other's items, history, and play counts — as
  // a modal warning at startup; loading still proceeds (Kartend audit cj462).
  // GUI path only; the headless main.cpp loader just logs.
  const QStringList uuidCollisions =
      m_appManager->getSettingsManager()->lastCollectionUuidCollisions();
  if (!uuidCollisions.isEmpty()) {
    QMessageBox::warning(
        this, tr("Collection configuration problem"),
        tr("Problems were found in your collection configuration:\n\n%1\n\n"
           "The app will still load, but please fix these — collections that share an "
           "identifier read and write each other's items, history, and play counts under "
           "one database key.")
            .arg(uuidCollisions.join(QStringLiteral("\n"))));
  }

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
  }
  rebuildHierarchyCache();
  // Defer playlist resync one event-loop tick so the freshly built hierarchy
  // cache has settled before resyncPlaylistCollections() reads it. Running
  // synchronously here would race rebuildHierarchyCache's downstream
  // signal-driven updates and produce a half-built collection list.
  //
  // Wire playlistsChanged only AFTER this initial resync: resync calls
  // ensureFavoritesPlaylist(), whose first-run playlistsChanged would otherwise
  // re-enter resync and run the full erase+reload+rebuildHierarchyCache twice at
  // startup (Kartend-r50e7). Later (runtime) playlist edits still resync.
  QTimer::singleShot(0, this, [this]() {
    resyncPlaylistCollections();
    if (PlaylistManager *pm = m_appManager->getPlaylistManager()) {
      QObject::connect(pm, &PlaylistManager::playlistsChanged, this, [this]() {
        // Playlist membership feeds the favorite:/tag token clauses baked
        // into the sorted-items query cache — drop the usage-sensitive
        // caches so a favorite toggle refreshes an actively-filtered grid
        // now, not on the next rescan. PlaylistManager writes through its
        // own DB connection, so DatabaseManager can't observe the mutation
        // itself.
        if (auto *db = m_appManager->getDatabaseManager()) {
          db->invalidateUsageSensitiveCaches();
        }
        resyncPlaylistCollections();
      });
    }
  });

  m_appManager->getSettingsManager()->loadGeneralSettings(m_generalSettings);

  // publish the persisted text-zoom multiplier into the static
  // before any widget is constructed below — the upcoming applyGlobalUiFont
  // and the scrollManager / sidebar setup paths all read zoomedFontSize().
  primeTextZoomFromSettings(m_generalSettings.appearance.uiTextZoomPercent);

  // push the persisted global UI font to QApplication before any
  // widgets are constructed below, so menus/dialogs/toolbar all pick it up on
  // their first show without a fontChange roundtrip.
  applyGlobalUiFont(m_generalSettings);

  // Apply text appearance settings to ItemWidget statics
  ItemWidget::setTitleTintSaturation(m_generalSettings.appearance.titleTintSaturation);
  ItemWidget::setTitleTintLightness(m_generalSettings.appearance.titleTintLightness);
  ItemWidget::setTitleBaseColor(m_generalSettings.appearance.titleBaseColor);

  ItemWidget::setShowTitleInPlaceholder(m_generalSettings.view.showTitleInPlaceholder);

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
            ErrorPresentation::reportSaveResult(
                m_appManager->getSettingsManager()->saveCollections(m_collections), "collections",
                false);
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
          m_appManager->getScrollManager()->centerHorizontalScrollbar(m_currentCollectionIndex,
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
    marqueeSetup.currentCollectionIndex = &m_currentCollectionIndex;
    marqueeSetup.collections = &m_collections;
    marqueeSetup.isShuttingDown = [this]() { return m_isShuttingDown; };
    m_marqueeController->setupReferences(marqueeSetup);
  }

  // Watch the desktop colour config so a runtime accent / colour-scheme change
  // (which Qt does not deliver to us as a palette-change event on KDE) triggers
  // a full re-theme. No-op on non-Linux.
  m_systemThemeWatcher = std::make_unique<SystemThemeWatcher>(this);
  connect(m_systemThemeWatcher.get(), &SystemThemeWatcher::themeChanged, this,
          &MainWindow::onSystemThemeChanged);

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

  // One-shot reconcile at startup: drop rows left orphaned by past
  // collection renames or removals so the Statistics totals line up with
  // the live collections without needing a settings-save round trip.
  // Kartend-dbqt5: the purge itself now runs on the DB worker thread and is
  // gated on a collections-set hash (skipped entirely when the set is
  // unchanged), so this defer is only about keeping the dispatch out of
  // showEvent's critical path. Gating on showEvent keeps the work out of
  // nested event loops opened by modal dialogs in test harnesses that
  // never call show().
  if (m_deferredStartupDone) {
    return;
  }
  m_deferredStartupDone = true;

  // Event-loop defer: let showEvent return and the first frame paint before
  // dispatching the purge (cheap now, but still a uuid-set hash + a queued
  // worker invoke that has no business on the paint-critical path).
  QTimer::singleShot(0, this, [this]() { maybePurgeOrphanCollectionData(); });
}

void MainWindow::maybePurgeOrphanCollectionData() {
  // Kartend-3vkjc: while the first-run wizard modal is open, defer instead of
  // running under its nested event loop; runDeferredStartupTasks() runs this
  // once after the wizard returns.
  if (m_startupTasksGated) {
    m_pendingOrphanPurge = true;
    return;
  }
  if (m_isShuttingDown || QApplication::closingDown()) {
    return;
  }
  if (m_appManager->getDatabaseManager()) {
    m_appManager->getDatabaseManager()->purgeOrphanCollectionData(m_collections);
  }
}

void MainWindow::setupUIReferences() {
  setWindowTitle("Kartend");

  // Apply user-configured pixmap cache size. Routes through the shared
  // helper so QPixmapCache AND CacheManager::artworkCache both pick up
  // the user setting in lockstep (Kartend-10pb).
  applyPixmapCacheBudget(m_generalSettings.media.pixmapCacheSizeMB);

  VideoThumbnailExtractor::instance()->setExtractionTimeoutMs(
      m_generalSettings.media.videoThumbnailExtractionTimeoutMs);

  m_stackedWidget = ui->stackedWidget;
  m_itemsPage = ui->itemsPage;
  m_gridContainer = ui->gridContainer;
  m_itemGrid = ui->itemGrid;
  m_mainContentWidget = ui->m_mainContentWidget;
  m_mainHorizontalLayout = ui->m_mainHorizontalLayout;
  m_searchBar = ui->searchBar;

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
  tcSetup.collectionWarningBadge = ui->collectionWarningBadge;
  tcSetup.searchBar = ui->searchBar;
  m_toolbarController->initialize(tcSetup);
  m_toolbarController->setupViewModeButton();
  m_toolbarController->setupSearchModeAction();
  m_toolbarController->setupHomeButton();
  m_toolbarController->refreshHomeButton(m_generalSettings);
  m_toolbarController->setupCollectionWarningBadge();
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
  if (m_overlayZOrderRegistry) {
    m_overlayZOrderRegistry->registerOverlay(m_loadingOverlay,
                                             OverlayZOrderRegistry::Layer::Loading);
    m_overlayZOrderRegistry->registerOverlay(m_splashOverlay, OverlayZOrderRegistry::Layer::Splash);
    m_overlayZOrderRegistry->registerOverlay(m_nowPlayingOverlay,
                                             OverlayZOrderRegistry::Layer::NowPlaying);
    m_overlayZOrderRegistry->registerOverlay(m_detailPageOverlay,
                                             OverlayZOrderRegistry::Layer::DetailPage);
    m_overlayZOrderRegistry->registerOverlay(m_textZoomHud,
                                             OverlayZOrderRegistry::Layer::TextZoomHud);
    m_loadingOverlay->setLayerManager(m_overlayZOrderRegistry.get());
    m_splashOverlay->setLayerManager(m_overlayZOrderRegistry.get());
    m_nowPlayingOverlay->setLayerManager(m_overlayZOrderRegistry.get());
    m_detailPageOverlay->setLayerManager(m_overlayZOrderRegistry.get());
    m_textZoomHud->setLayerManager(m_overlayZOrderRegistry.get());
  }
}

void MainWindow::applyPixmapCacheBudget(int megabytes) {
  // Kartend-pyfb4: `megabytes` is the TOTAL in-memory artwork budget the user
  // asked for ("Memory allocated for caching artwork images"), and it funds
  // two independent pixmap caches — Qt's process-global QPixmapCache and
  // CacheManager::artworkCache. Handing the full figure to each meant a
  // configured 500 MB authorised ~1 GB resident, so the setting understated
  // its own cost by 2x. Split it instead, so the number means what it says.
  //
  // The artwork cache takes the floor half and QPixmapCache the remainder, so
  // an odd total loses nothing. Both halves are floored at 1 MB because this
  // is a public IAppearanceApplier entry point: the settings UI clamps to
  // [10,500], but a direct caller could otherwise drive a half to 0, which
  // QCache/QPixmapCache read as evict-everything.
  constexpr int kMinHalfMB = 1;
  const int totalMB = std::max(2 * kMinHalfMB, megabytes);
  const int artworkMB = std::max(kMinHalfMB, totalMB / 2);
  const int qtPixmapMB = std::max(kMinHalfMB, totalMB - artworkMB);
  // QPixmapCache::setCacheLimit takes KB; our settings store MB.
  QPixmapCache::setCacheLimit(qtPixmapMB * 1024);
  // CacheManager::artworkCache budget is owned by the manager itself —
  // before m_appManager->getCacheManager() is wired up (very early startup), the
  // CacheManager holds its construction-time legacy default and will
  // pick up the user value on the next call.
  if (auto *cm = m_appManager->getCacheManager()) {
    cm->setArtworkCacheBudgetMB(artworkMB);
    // The on-disk eviction budget rides along so both artwork-cache budgets
    // stay in lockstep through the one entry point (same drift-avoidance
    // rationale as Kartend-10pb). Read from the live settings rather than a
    // second parameter: every caller passes media.pixmapCacheSizeMB from the
    // same struct that carries the disk budget.
    cm->setArtworkDiskCacheBudgetMB(m_generalSettings.media.artworkDiskCacheBudgetMB);
  }
}

void MainWindow::initializeAppContext() {
  // Collection state
  m_appContext.collection.collections = &m_collections;
  m_appContext.collection.currentCollectionIndex = &m_currentCollectionIndex;
  m_appContext.collection.hierarchyCache = &m_hierarchyCache;
  m_appContext.collection.generalSettings = &m_generalSettings;
  m_appContext.collection.isShuttingDown = &m_isShuttingDown;

  // Common UI elements
  m_appContext.ui.itemScrollArea = ui->itemScrollArea;
  m_appContext.ui.stackedWidget = m_stackedWidget;
  m_appContext.ui.itemsPage = m_itemsPage;
  m_appContext.ui.itemsTopBar = ui->itemsTopBar;
  m_appContext.ui.gridContainer = m_gridContainer;
  m_appContext.ui.menubar = ui->menubar;
  m_appContext.ui.searchBar = m_searchBar;
  m_appContext.ui.searchModeAction =
      m_toolbarController ? m_toolbarController->searchModeAction() : nullptr;
  m_appContext.ui.sidebar = m_MetadataSidebar;
  m_appContext.ui.loadingLabel = ui->loadingLabel;
  m_appContext.ui.loadingOverlay = m_loadingOverlay;
  m_appContext.ui.overlayZOrderRegistry = m_overlayZOrderRegistry.get();

  // Top-level managers — registered eagerly so ctx is fully populated before
  // any manager's setupReferences() runs.
  // Seeds the scroll facade plus its six role views in lockstep
  // (Kartend-h1l8f).
  m_appContext.managers.seedScrollRoles(m_appManager->getScrollManager());
  if (auto *sm = m_appManager->getScrollManager()) {
    // ScrollManager's ctor already wired m_filterManager off DataSourceCoordinator,
    // so this alias is non-null the moment ScrollManager exists (Kartend-yeik).
    // sm->filterManager() returns the concrete FilterManager*; the implicit
    // upcast to IFilterManager* relies on FilterManager's complete declaration,
    // which is reached through the filtermanager.h include below.
    m_appContext.managers.filterManager = sm->filterManager();
  }
  // Facade + role views seeded in lockstep (Kartend-dl0uz.2).
  m_appContext.managers.seedArtworkRoles(m_appManager->getArtworkManager());
  m_appContext.managers.settingsManager = m_appManager->getSettingsManager();
  m_appContext.managers.sessionManager = m_appManager->getSessionManager();
  m_appContext.managers.detailsPaneManager = m_appManager->getDetailsPaneManager();
  m_appContext.managers.detailPageManager = m_appManager->getDetailPageManager();
  m_appContext.managers.seedDatabaseRoles(m_appManager->getDatabaseManager());
  m_appContext.managers.navigationManager = m_appManager->getNavigationManager();
  m_appContext.managers.interactionManager = m_appManager->getInteractionManager();
  m_appContext.managers.playlistManager = m_appManager->getPlaylistManager();
  m_appContext.managers.cacheManager = m_appManager->getCacheManager();

  // InteractionManager-owned sub-managers exist as soon as InteractionManager
  // is constructed (its ctor allocates them via std::make_unique). Register
  // them here, before any setupReferences() runs, so dependents can resolve
  // siblings exclusively through ctx.
  if (auto *im = m_appManager->getInteractionManager()) {
    // Facade + role views seeded in lockstep (Kartend-dl0uz.2).
    m_appContext.managers.seedAnimationRoles(im->animationManager());
    m_appContext.managers.seedSelectionRoles(im->selectionManager());
    m_appContext.managers.seedViewportRoles(im->viewportManager());
    m_appContext.managers.seedMouseRoles(im->mouseManager());
    m_appContext.managers.seedKeyboardRoles(im->keyboardManager());
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
  ctx.ui = ui.get(); // Kartend-nbfgs: ui is now a unique_ptr; ctx holds the raw ptr
  ctx.getNavigationManager = [this]() { return m_appManager->getNavigationManager(); };
  ctx.getSettingsManager = [this]() { return m_appManager->getSettingsManager(); };
  ctx.getDetailsPaneManager = [this]() { return m_appManager->getDetailsPaneManager(); };
  ctx.getScrollManager = [this]() { return m_appManager->getScrollManager(); };
  ctx.getArtworkManager = [this]() { return m_appManager->getArtworkManager(); };
  ctx.getDatabaseManager = [this]() { return m_appManager->getDatabaseManager(); };
  ctx.getInteractionManager = [this]() { return m_appManager->getInteractionManager(); };
  ctx.getCurrentCollectionIndex = [this]() { return m_currentCollectionIndex; };
  ctx.getCollections = [this]() { return &m_collections; };
  ctx.getGeneralSettings = [this]() { return &m_generalSettings; };
  ctx.getHierarchyCache = [this]() -> const CollectionHierarchyCache * {
    return &m_hierarchyCache;
  };
  ctx.getCurrentViewType = [this]() {
    if (m_currentCollectionIndex >= 0 && m_currentCollectionIndex < m_collections.size()) {
      return m_collections[m_currentCollectionIndex].viewType;
    }
    return ViewType::Grid;
  };
  ctx.onSetViewType = [this](ViewType viewType) { setViewType(viewType); };
  ctx.onLaunchItem = [this](const QString &filePath, int collectionIndex) {
    if (auto *im = m_appManager->getInteractionManager()) {
      im->launchItemWithCollection(filePath, collectionIndex);
    }
  };
  ctx.onOpenSettings = [this]() { openSettingsDialog(); };
  ctx.onShowAbout = [this]() { showAbout(); };
  ctx.onAdjustGridWidth = [this](int delta) { adjustGridWidth(delta); };
  ctx.onImportKart = [this]() {
    if (auto *km = m_appManager->getKartManager()) km->importInteractive();
  };
  ctx.onExportKart = [this]() {
    if (auto *km = m_appManager->getKartManager())
      km->exportCollectionInteractive(m_currentCollectionIndex);
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
  ctx.onRunDatAudit = [this]() { openDatAuditDialog(); };

  m_menuController->setContext(ctx);
  m_menuController->setupMenuBar();
}

void MainWindow::adjustGridWidth(int delta) {
  if (m_currentCollectionIndex < 0 || m_currentCollectionIndex >= m_collections.size()) {
    return;
  }

  CollectionConfig &config = m_collections[m_currentCollectionIndex];

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
    ErrorPresentation::reportSaveResult(
        m_appManager->getSettingsManager()->saveCollections(m_collections), "collections", false);
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
  if (m_currentCollectionIndex < 0 || m_currentCollectionIndex >= m_collections.size()) {
    return;
  }

  CollectionConfig &config = m_collections[m_currentCollectionIndex];
  if (config.viewType == viewType) {
    return; // No change needed
  }

  // Update the collection config
  config.viewType = viewType;

  // Persist the change (debounced). A view-mode toggle is a per-click
  // interactive path, and saveCollections is a full INI rewrite + fsync of
  // the config directory — an inline per-click stall on a populous library.
  // Ride the same save stage the grid-width shortcuts already coalesce
  // through: the new viewType is applied to m_collections above, so a save
  // still inside the debounce window at exit is not lost —
  // ApplicationManager::shutdown persists m_collections synchronously, under
  // the same m_configReplacedOnDisk gate closeEvent applies to the
  // general-settings flush (Kartend-rbkf6 rationale).
  if (m_gridWidthDebouncer) {
    m_gridWidthDebouncer->triggerSave();
  } else if (m_appManager->getSettingsManager()) {
    ErrorPresentation::reportSaveResult(
        m_appManager->getSettingsManager()->saveCollections(m_collections), "collections", false);
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
      auto *im = m_appManager ? m_appManager->getInteractionManager() : nullptr;
      if (im && im->itemMetadataActions()) {
        im->itemMetadataActions()->editItemMetadata(filePath, itemName);
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

  // Kartend-gutqx: the InteractionManager-side installer
  // (InteractionManager::installEventFilters, run from its setupReferences)
  // is the single owner of these widget filters now. The duplicate installs
  // that used to live here were harmless at runtime (Qt dedupes delivery)
  // but guaranteed drift: only the manager-side set is removed in
  // ~InteractionManager, and only it tracks viewport recreation.
}
