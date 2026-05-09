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
#include <QSize>
#include <QTimer>
#include <QToolButton>

#include "applicationmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailspane.h"
#include "gridwidthdebouncer.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "kartmanager.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "menucontroller.h"
#include "navigationmanager.h"
#include "playlistmanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"

#include "detailpageoverlay.h"
#include "detailspanemanager.h"
#include "nowplayingoverlay.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "shortcutsdialog.h"
#include "splashoverlay.h"
#include "stringutils.h"
#include "textzoomhud.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcMainWindow)

void MainWindow::setupUI() {
  setAcceptDrops(true);

  // Managers are initialized by ApplicationManager in the constructor
  getSessionManager()->initialize();

  // Load settings
  getSettingsManager()->loadCollections(m_collections);

  // append synthesized playlist CollectionConfigs after INI
  // collections so playlists nest into the hierarchy and appear as virtual
  // collections. resyncPlaylistCollections also rebuilds the hierarchy cache,
  // so we don't need a separate rebuild call here.
  if (PlaylistManager *playlistManager = getPlaylistManager()) {
    playlistManager->initialize();
    QObject::connect(playlistManager, &PlaylistManager::playlistsChanged, this,
                     [this]() { resyncPlaylistCollections(); });
  }
  resyncPlaylistCollections();

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
  // bind Ctrl+= / Ctrl+- / Ctrl+0 after managers are wired so
  // applyTextZoom() can refresh the scroll/sidebar pipeline on press.
  setupTextZoomShortcuts();
  setupVideoPauseShortcut();
  setupPreviewVolumeSlider();
  setupInitialTimers();
}

void MainWindow::setupUIReferences() {
  setWindowTitle("Kartend");

  // Apply user-configured pixmap cache size (in KB, settings stores MB)
  int cacheSizeKB = m_generalSettings.pixmapCacheSizeMB * 1024;
  QPixmapCache::setCacheLimit(cacheSizeKB);

  stackedWidget = ui->stackedWidget;
  itemsPage = ui->itemsPage;
  gridContainer = ui->gridContainer;
  itemGrid = ui->itemGrid;
  m_mainContentWidget = ui->m_mainContentWidget;
  m_mainHorizontalLayout = ui->m_mainHorizontalLayout;
  searchBar = ui->searchBar;
  m_viewModeButton = ui->viewModeButton;
  setupViewModeButton();
  setupSearchModeAction();
  // Single consolidated filter button (replaces typeFilterButton +
  // titleFilterButton + hideSubcollectionsButton).
  m_filterButton = ui->filterButton;
  if (m_filterButton) {
    m_filterButton->setIcon(
        UIConstants::Icons::fromTheme({UIConstants::Icons::FILTER, "view-filter"}));
    m_filterButton->setIconSize(QSize(18, 18));
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
  m_appContext.ui.searchModeAction = m_searchModeAction;
  m_appContext.ui.sidebar = m_MetadataSidebar;
  m_appContext.ui.loadingLabel = ui->loadingLabel;
  m_appContext.ui.loadingOverlay = m_loadingOverlay;

  // Manager references (for setup structs to use via ctx)
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
                           config.gridWidthSidebarHidden > 0;
  int &activeField = useAltField ? config.gridWidthSidebarHidden : config.gridWidth;

  int newWidth = activeField + delta;

  // Clamp to valid range
  newWidth = qBound(UIConstants::Grid::MIN_WIDTH, newWidth, UIConstants::Grid::MAX_WIDTH);

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
    setup.settingsManager = getSettingsManager();
    setup.artworkManager = getArtworkManager();
    setup.databaseManager = getDatabaseManager();

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

void MainWindow::setupArtworkManager() {
  if (getArtworkManager()) {
    getArtworkManager()->initializeCache();
  }
  ArtworkManager &artMgr = *getArtworkManager();

  ArtworkManagerSetup setup;
  setup.ctx = &m_appContext;

  artMgr.setupReferences(setup);
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
  getScrollManager()->setDatabaseManager(getDatabaseManager());

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
