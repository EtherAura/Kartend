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
#include <QTimer>

#include "applicationmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "menucontroller.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "playlistmanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"

#include "nowplayingoverlay.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "shortcutsdialog.h"
#include "sidebarmanager.h"
#include "splashoverlay.h"
#include "stringutils.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcMainWindow)

void MainWindow::setupUI() {
  // Managers are initialized by ApplicationManager in the constructor
  getSessionManager()->initialize();

  // Load settings
  getSettingsManager()->loadCollections(m_collections);

  // Kartend-vlm7: append synthesized playlist CollectionConfigs after INI
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

  // Apply text appearance settings to ItemWidget statics
  ItemWidget::setTitleTintSaturation(m_generalSettings.titleTintSaturation);
  ItemWidget::setTitleTintLightness(m_generalSettings.titleTintLightness);
  ItemWidget::setTitleBaseColor(m_generalSettings.titleBaseColor);

  setupUIReferences();

  // Debounced persistence + refresh for menu-driven grid width changes.
  // This avoids writing settings repeatedly while the user holds +/-.
  if (!m_gridWidthSaveDebouncer) {
    m_gridWidthSaveDebouncer =
        new TimerUtils::DebouncedTimer(UIConstants::Timing::LONG_DELAY_MS, this);
    QObject::connect(m_gridWidthSaveDebouncer, &TimerUtils::DebouncedTimer::triggered, this,
                     [this]() {
                       if (m_isShuttingDown || QApplication::closingDown()) {
                         return;
                       }
                       if (getSettingsManager()) {
                         getSettingsManager()->saveCollections(m_collections);
                       }
                     });
  }

  if (!m_gridWidthPrecalcDebouncer) {
    m_gridWidthPrecalcDebouncer =
        new TimerUtils::DebouncedTimer(UIConstants::Timing::LONG_DELAY_MS, this);
    QObject::connect(m_gridWidthPrecalcDebouncer, &TimerUtils::DebouncedTimer::triggered, this,
                     [this]() {
                       if (m_isShuttingDown || QApplication::closingDown()) {
                         return;
                       }
                       if (!getScrollManager()) {
                         return;
                       }

                       // Mark this generation as the active one for the final
                       // stage.
                       m_gridWidthActiveGeneration = m_gridWidthPendingGeneration;

                       getScrollManager()->preCalculateLayout();
                       getScrollManager()->forceVirtualViewUpdate();

                       if (m_gridWidthFinalizeDebouncer) {
                         m_gridWidthFinalizeDebouncer->trigger();
                       }
                     });
  }

  if (!m_gridWidthFinalizeDebouncer) {
    m_gridWidthFinalizeDebouncer =
        new TimerUtils::DebouncedTimer(UIConstants::Timing::MEDIUM_DELAY_MS, this);
    QObject::connect(
        m_gridWidthFinalizeDebouncer, &TimerUtils::DebouncedTimer::triggered, this, [this]() {
          if (m_isShuttingDown || QApplication::closingDown()) {
            return;
          }
          if (m_gridWidthActiveGeneration != m_gridWidthPendingGeneration) {
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
  // Kartend-81o: apply persisted toolbar visibility/text overrides to the
  // freshly-constructed toolbar widgets before any layout settles.
  applyToolbarCustomization();
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
  m_searchModeButton = ui->searchModeButton;
  m_gridViewButton = ui->gridViewButton;
  m_listViewButton = ui->listViewButton;
  // Kartend-dd8: collection categorization toolbar widgets
  m_hideSubcollectionsButton = ui->hideSubcollectionsButton;
  m_typeFilterComboBox = ui->typeFilterComboBox;
  // Kartend-5h6: title-exclusion regex toolbar button
  m_titleFilterButton = ui->titleFilterButton;
  m_MetadataSidebar = ui->metadataSidebarWidget;

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

  // Kartend-qxv: Persistent "Now Playing" overlay used while a runtime-tracked
  // child process is running. Stays hidden until LaunchManager signals start.
  m_nowPlayingOverlay = new NowPlayingOverlay(ui->centralwidget);
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
  m_appContext.ui.searchModeButton = m_searchModeButton;
  m_appContext.ui.sidebar = m_MetadataSidebar;
  m_appContext.ui.loadingLabel = ui->loadingLabel;
  m_appContext.ui.loadingOverlay = m_loadingOverlay;

  // Manager references (for setup structs to use via ctx)
  m_appContext.managers.scrollManager = getScrollManager();
  m_appContext.managers.artworkManager = getArtworkManager();
  m_appContext.managers.settingsManager = getSettingsManager();
  m_appContext.managers.sessionManager = getSessionManager();
  m_appContext.managers.sidebarManager = getSidebarManager();
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
  ctx.getSidebarManager = [this]() { return getSidebarManager(); };
  ctx.getScrollManager = [this]() { return getScrollManager(); };
  ctx.getArtworkManager = [this]() { return getArtworkManager(); };
  ctx.getDatabaseManager = [this]() { return getDatabaseManager(); };
  ctx.getCurrentCollectionIndex = [this]() { return currentCollectionIndex; };
  ctx.getCollections = [this]() { return &m_collections; };
  ctx.getGeneralSettings = [this]() { return &m_generalSettings; };
  ctx.onOpenSettings = [this]() {
    if (getSettingsManager()) {
      SettingsDialogContext context;
      context.parent = this;
      context.collections = &m_collections;
      context.currentCollectionIndex = &currentCollectionIndex;
      context.sidebarManager = getSidebarManager();
      context.scrollManager = getScrollManager();
      context.navigationManager = getNavigationManager();
      context.databaseManager = getDatabaseManager();
      getSettingsManager()->openSettingsDialog(context);
    }
  };
  ctx.onShowAbout = [this]() { showAbout(); };
  ctx.onAdjustGridWidth = [this](int delta) { adjustGridWidth(delta); };

  m_menuController->setContext(ctx);
  m_menuController->setupMenuBar();
}

void MainWindow::adjustGridWidth(int delta) {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    return;
  }

  CollectionConfig &config = m_collections[currentCollectionIndex];
  int newWidth = config.gridWidth + delta;

  // Clamp to valid range
  newWidth = qBound(UIConstants::Grid::MIN_WIDTH, newWidth, UIConstants::Grid::MAX_WIDTH);

  if (newWidth == config.gridWidth) {
    return; // No change needed
  }

  // Update the collection config
  config.gridWidth = newWidth;

  // Persist the change (debounced) to avoid repeated disk writes when the user
  // holds the shortcut.
  if (m_gridWidthSaveDebouncer) {
    m_gridWidthSaveDebouncer->trigger();
  } else if (getSettingsManager()) {
    getSettingsManager()->saveCollections(m_collections);
  }

  // Apply the change to the UI using the same flow as settings dialog
  if (getScrollManager()) {
    getScrollManager()->updateGridWidth(newWidth);

    // Coalesce expensive layout + artwork refresh for repeated adjustments.
    ++m_gridWidthPendingGeneration;
    if (m_gridWidthPrecalcDebouncer) {
      m_gridWidthPrecalcDebouncer->trigger();
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

  // Update button checked states
  if (m_gridViewButton) {
    m_gridViewButton->setChecked(viewType == ViewType::Grid);
  }
  if (m_listViewButton) {
    m_listViewButton->setChecked(viewType == ViewType::List);
  }

  // Trigger a full layout refresh - viewType affects widget dimensions and
  // layout
  if (getScrollManager()) {
    getScrollManager()->updateViewType(viewType);
  }
}

void MainWindow::setupSidebar() {
  if (getSidebarManager()) {
    getSidebarManager()->setupSidebar();

    SidebarManagerSetup setup;
    setup.ctx = &m_appContext;
    setup.mainLayout = m_mainHorizontalLayout;
    setup.settingsManager = getSettingsManager();
    setup.artworkManager = getArtworkManager();
    setup.databaseManager = getDatabaseManager();

    getSidebarManager()->setupReferences(setup);

    QObject::connect(getSidebarManager(), &SidebarManager::sidebarVisibilityChanged, this,
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
