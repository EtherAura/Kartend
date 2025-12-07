// Main application window that owns ApplicationManager and orchestrates UI setup.
#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QMessageBox>
#include <QPixmapCache>

#include "applicationmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "shortcutsdialog.h"
#include "sidebarmanager.h"
#include "stringutils.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), stackedWidget(nullptr),
      itemsPage(nullptr), gridContainer(nullptr), m_mainContentWidget(nullptr),
      itemGrid(nullptr), m_mainHorizontalLayout(nullptr), searchBar(nullptr),
      m_searchModeButton(nullptr), loadingLabel(nullptr),
      currentCollectionIndex(-1),
      m_MetadataSidebar(nullptr) {
  m_appManager = std::make_unique<ApplicationManager>(this);
  m_appManager->initialize();
  
  ui->setupUi(this);
  setupUI();
}

MainWindow::~MainWindow() { delete ui; }
void MainWindow::keyPressEvent(QKeyEvent *event) {
  if ((getInteractionManager()) &&
      getInteractionManager()->handleGlobalKeyPress(event)) {
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  if (!updatesEnabled()) {
    return;
  }

  QMainWindow::resizeEvent(event);

  if (getArtworkManager()) {
    if (auto *timerCoordinator = getArtworkManager()->getTimerCoordinator()) {
      timerCoordinator->scheduleLayoutUpdate();
    }
  }

  // Re-center on current selection after resize completes
  // Defer re-centering until after resize animation completes -
  // prevents visual jump during resize drag
  QTimer::singleShot(UIConstants::Timing::RESIZE_RECENTER_DELAY_MS, this, [this]() {
    if (!QApplication::closingDown() && getInteractionManager()) {
      getInteractionManager()->recenterCurrentSelection();
    }
  });
}

auto MainWindow::eventFilter(QObject *watched, QEvent *event) -> bool {
  return (getInteractionManager())
             ? getInteractionManager()->eventFilter(watched, event)
             : QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshTitleCounts() {
  if (!getDatabaseManager()) {
    return;
  }
  int cur = currentCollectionIndex;
  if (cur < 0 || cur >= m_collections.size()) {
    setWindowTitle(qApp->applicationName());
    return;
  }

  QVector<int> chain;
  int walk = cur;
  while (walk >= 0 && walk < m_collections.size()) {
    chain.append(walk);
    int parentIndex = m_collections[walk].parentCollectionIndex;
    if (parentIndex < 0) {
      break;
    }
    walk = parentIndex;
  }

  QStringList parts;
  for (int idx : chain) {
    qint64 countVal =
        getDatabaseManager()->countCollectionRecursive(idx, m_collections);
    parts << StringUtils::formatCountNumber(countVal);
  }

  QString base = m_collections[cur].name;
  QString counts;
  if (parts.size() == 1) {
    counts = QString("(%1 Items)").arg(parts.first());
  } else {
    counts = QString("(%1 Items)").arg(parts.join('/'));
  }
  setWindowTitle(QString("%1 %2").arg(base, counts));
}

// Wires managers and signals; ensures sidebar metadata is refreshed when the
// sidebar becomes visible or its layout changes
void MainWindow::setupManagerConnections() {
  InteractionManagerSetup setup;
  setup.ctx = &m_appContext;  // Managers and UI elements from shared context

  loadingLabel = ui->loadingLabel;

  // Set up InteractionManager first to get interactionState
  getInteractionManager()->setupReferences(setup);
  
  // Populate interactionState in context now that InteractionManager is set up
  m_appContext.interactionState = &getInteractionManager()->state();

  // Register InteractionManager's owned sub-managers in ApplicationContext
  // This enables sub-managers to access siblings directly via ctx
  m_appContext.animationManager = getInteractionManager()->animationManager();
  m_appContext.selectionManager = getInteractionManager()->selectionManager();
  m_appContext.viewportManager = getInteractionManager()->viewportManager();
  m_appContext.mouseManager = getInteractionManager()->mouseManager();
  m_appContext.keyboardManager = getInteractionManager()->keyboardManager();
  m_appContext.eventManager = getInteractionManager()->eventManager();
  m_appContext.searchManager = getInteractionManager()->searchManager();
  m_appContext.launchManager = getInteractionManager()->launchManager();

  // Now set up NavigationManager with fully populated context
  NavigationManagerSetup navSetup;
  navSetup.ctx = &m_appContext;  // Managers and UI elements from shared context
  
  // Callbacks (not in context)
  navSetup.isShuttingDown = [this]() { return isShuttingDown(); };
  navSetup.refreshTitleCounts = [this]() { refreshTitleCounts(); };

  getNavigationManager()->setupReferences(navSetup);

  connectDatabaseManager();
  connectScrollManager();
  connectSidebarManager();
  connectSearchComponents();
  connectScrollBars();
}

void MainWindow::connectDatabaseManager() {
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemsLoaded,
                   getNavigationManager(), &NavigationManager::onItemsLoaded);
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemCountLoaded,
                   getNavigationManager(), &NavigationManager::onItemCountLoaded);
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemsRangeLoaded,
                   getNavigationManager(), &NavigationManager::onItemsRangeLoaded);
  QObject::connect(getDatabaseManager(), &DatabaseManager::errorOccurred,
                   getNavigationManager(),
                   &NavigationManager::onMediaLibraryError);
  
  // Rebuild hierarchy cache when collections are modified via settings dialog
  QObject::connect(getSettingsManager(), &SettingsManager::collectionsModified,
                   this, &MainWindow::rebuildHierarchyCache);
}

void MainWindow::connectScrollManager() {
  // Click/double-click handling is done via EventManager, not ItemWidget signals
  QObject::connect(getScrollManager(), &ScrollManager::subcollectionEntered,
                   getNavigationManager(),
                   &NavigationManager::onSubcollectionEntered);
  QObject::connect(getScrollManager(), &ScrollManager::virtualFolderEntered,
                   getNavigationManager(),
                   &NavigationManager::onVirtualFolderEntered);
  QObject::connect(getScrollManager(), &ScrollManager::requestItemsRange,
                   getNavigationManager(), &NavigationManager::fetchItemsRange);
  if (getArtworkManager()) {
    QObject::connect(getArtworkManager()->getTimerCoordinator(),
                     &TimerUtils::Coordinator::viewportUpdateRequested, this,
                     [this]() {
                       if (!QApplication::closingDown() && getArtworkManager()) {
                         getArtworkManager()->updateViewportArtwork();
                       }
                     });
    QObject::connect(getArtworkManager()->getTimerCoordinator(),
                     &TimerUtils::Coordinator::layoutUpdateRequested, this,
                     [this]() {
                       if (!QApplication::closingDown() &&
                           (getScrollManager())) {
                         getScrollManager()->handleLayoutChange();
                       }
                     });
  }
  QObject::connect(getScrollManager(), &ScrollManager::filterChanged, this,
                   [this](int visible, int total) {
                     if (!QApplication::closingDown()) {
                       updateWindowTitleWithFilter(visible, total);
                     }
                   });
}

void MainWindow::connectSidebarManager() {
  QObject::connect(
      getSidebarManager(), &SidebarManager::sidebarVisibilityChanged, this,
      [this](bool visible) {
        if (visible && (getSidebarManager()) &&
            (getScrollManager()) && (getInteractionManager())) {
          int sel = getInteractionManager()->currentSelectedIndex();
          if (sel >= 0) {
            ItemWidget *widgetPtr =
                getScrollManager()->getActiveWidgets().value(sel, nullptr);
            getSidebarManager()->updateSidebarMetadata(widgetPtr);
          }
        }
        // Delay metrics recalculation to allow sidebar animation to complete
        // before recalculating grid layout dimensions
        QTimer::singleShot(UIConstants::Sidebar::METRICS_RECALC_DELAY_MS, this,
                           [this]() {
                             if (getScrollManager()) {
                               getScrollManager()->recalculateContainerMetrics();
                             }
                           });
      });

  QObject::connect(
      getSidebarManager(), &SidebarManager::sidebarLayoutChanged, this, [this]() {
        if (getScrollManager()) {
          getScrollManager()->recalculateContainerMetrics();
        }
        if ((getSidebarManager()) && (getScrollManager()) &&
            (getInteractionManager()) &&
            getSidebarManager()->isSidebarVisible()) {
          int sel = getInteractionManager()->currentSelectedIndex();
          if (sel >= 0) {
            ItemWidget *widgetPtr =
                getScrollManager()->getActiveWidgets().value(sel, nullptr);
            getSidebarManager()->updateSidebarMetadata(widgetPtr);
          }
        }
      });
}

void MainWindow::connectSearchComponents() {
  if ((m_searchModeButton) && (getInteractionManager())) {
    QObject::connect(m_searchModeButton, &QPushButton::clicked,
                     getInteractionManager(),
                     &InteractionManager::toggleSearchMode);
  }
}

void MainWindow::connectScrollBars() const {
  if (ui->itemScrollArea) {
    QScrollBar *vScrollBar = ui->itemScrollArea->verticalScrollBar();
    QScrollBar *hScrollBar = ui->itemScrollArea->horizontalScrollBar();

    if ((vScrollBar) && (getNavigationManager())) {
      QObject::connect(vScrollBar, &QScrollBar::valueChanged,
                       getNavigationManager(),
                       &NavigationManager::onViewportChanged);
    }
    if ((hScrollBar) && (getNavigationManager())) {
      QObject::connect(hScrollBar, &QScrollBar::valueChanged,
                       getNavigationManager(),
                       &NavigationManager::onViewportChanged);
    }
  }
}

void MainWindow::updateWindowTitleWithFilter(int visible, int total) {
  if (currentCollectionIndex >= 0 &&
      currentCollectionIndex < m_collections.size()) {
    QString base = m_collections[currentCollectionIndex].name;
    if (visible < total) {
      setWindowTitle(
          QString("%1 (%2/%3 items)").arg(base).arg(visible).arg(total));
    } else {
      setWindowTitle(QString("%1 (%2 items)").arg(base).arg(total));
    }
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_isShuttingDown) {
    event->accept();
    return;
  }

  m_isShuttingDown = true;

  // Hide window immediately so user sees instant visual response
  hide();

  // Remove event filters first to prevent further processing
  if (ui->itemScrollArea) {
    ui->itemScrollArea->removeEventFilter(getInteractionManager());
    if (ui->itemScrollArea->viewport()) {
      ui->itemScrollArea->viewport()->removeEventFilter(getInteractionManager());
    }
  }
  if (gridContainer) {
    gridContainer->removeEventFilter(getInteractionManager());
  }

  // Block signals early to prevent cascading updates during shutdown
  if (getInteractionManager()) {
    getInteractionManager()->blockSignals(true);
    getInteractionManager()->clearSelection();
  }
  if (getScrollManager()) {
    getScrollManager()->blockSignals(true);
  }

  currentCollectionIndex = -1;

  // Delegate shutdown to ApplicationManager for coordinated cleanup
  if (m_appManager) {
    m_appManager->shutdown(m_collections);
  }

  event->accept();
}

void MainWindow::setupUI() {
  // Managers are initialized by ApplicationManager in the constructor
  getSessionManager()->initialize();
  
  // Load settings
  getSettingsManager()->loadCollections(m_collections);
  rebuildHierarchyCache();
  getSettingsManager()->loadGeneralSettings(m_generalSettings);
  
  // Apply text appearance settings to ItemWidget statics
  ItemWidget::setTitleTintSaturation(m_generalSettings.titleTintSaturation);
  ItemWidget::setTitleTintLightness(m_generalSettings.titleTintLightness);
  ItemWidget::setTitleBaseColor(m_generalSettings.titleBaseColor);
  ItemWidget::setCustomFontFamily(m_generalSettings.customFontFamily);

  setupUIReferences();
  initializeAppContext();
  createMenuBar();
  setupSidebar();
  setupManagerConnections();
  setupArtworkManager();
  setupLastSelectedIndices();
  setupEventFilters();
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
  m_MetadataSidebar = ui->metadataSidebarWidget;
  
  // Create loading overlay (parented to scroll area for correct positioning)
  m_loadingOverlay = new LoadingOverlay(ui->itemScrollArea);
}

void MainWindow::initializeAppContext() {
  // Collection state
  m_appContext.collections = &m_collections;
  m_appContext.currentCollectionIndex = &currentCollectionIndex;
  m_appContext.hierarchyCache = &m_hierarchyCache;
  m_appContext.generalSettings = &m_generalSettings;
  m_appContext.isShuttingDown = &m_isShuttingDown;

  // Common UI elements
  m_appContext.itemScrollArea = ui->itemScrollArea;
  m_appContext.stackedWidget = stackedWidget;
  m_appContext.itemsPage = itemsPage;
  m_appContext.itemsTopBar = ui->itemsTopBar;
  m_appContext.gridContainer = gridContainer;
  m_appContext.menubar = ui->menubar;
  m_appContext.searchBar = searchBar;
  m_appContext.searchModeButton = m_searchModeButton;
  m_appContext.sidebar = m_MetadataSidebar;
  m_appContext.loadingLabel = ui->loadingLabel;
  m_appContext.loadingOverlay = m_loadingOverlay;

  // Manager references (for setup structs to use via ctx)
  m_appContext.scrollManager = getScrollManager();
  m_appContext.artworkManager = getArtworkManager();
  m_appContext.settingsManager = getSettingsManager();
  m_appContext.sessionManager = getSessionManager();
  m_appContext.sidebarManager = getSidebarManager();
  m_appContext.databaseManager = getDatabaseManager();
  m_appContext.navigationManager = getNavigationManager();
  m_appContext.interactionManager = getInteractionManager();
}

void MainWindow::createMenuBar() {
  setupActionExit();
  setupActionShowSidebar();
  setupActionSettings();
  setupActionRefresh();
  setupActionAbout();
  setupActionAboutQt();
  setupFullscreenAction();
  setupShortcutsAction();
}

void MainWindow::setupActionExit() {
  if (ui->actionExit) {
    QObject::connect(ui->actionExit, &QAction::triggered, this,
                     &QWidget::close);
    ui->actionExit->setShortcutContext(Qt::ApplicationShortcut);
    addAction(ui->actionExit);
  }
}

void MainWindow::setupActionShowSidebar() {
  if (ui->actionShowSidebar) {
    QObject::connect(ui->actionShowSidebar, &QAction::triggered,
                     [this]() {
                       if (getSidebarManager()) {
                         getSidebarManager()->toggleSidebar();
                       }
                     });
    ui->actionShowSidebar->setShortcutContext(Qt::ApplicationShortcut);
    addAction(ui->actionShowSidebar);
  }
}

void MainWindow::setupActionSettings() {
  if (ui->actionSettings) {
    QObject::connect(
        ui->actionSettings, &QAction::triggered, [this]() {
          if (getSettingsManager()) {
            SettingsDialogContext context;
            context.parent = this;
            context.collections = &m_collections;
            context.currentCollectionIndex = &currentCollectionIndex;
            context.sidebarManager = getSidebarManager();
            context.scrollManager = getScrollManager();
            context.navigationManager = getNavigationManager();
            getSettingsManager()->openSettingsDialog(context);
          }
        });
    ui->actionSettings->setShortcutContext(Qt::ApplicationShortcut);
    addAction(ui->actionSettings);
  }
}

void MainWindow::setupActionAbout() {
  if (ui->actionAbout) {
    QObject::connect(ui->actionAbout, &QAction::triggered,
                     [this]() { showAbout(); });
  }
}

void MainWindow::setupActionAboutQt() {
  if (ui->actionAboutQt) {
    QObject::connect(ui->actionAboutQt, &QAction::triggered,
                     qApp, &QApplication::aboutQt);
  }
}

void MainWindow::setupActionRefresh() {
  if (ui->actionRefresh) {
    QObject::connect(ui->actionRefresh, &QAction::triggered, [this]() {
      if (getNavigationManager() && currentCollectionIndex >= 0) {
        getNavigationManager()->safeReloadCollection(currentCollectionIndex);
      }
    });
    ui->actionRefresh->setShortcutContext(Qt::ApplicationShortcut);
    addAction(ui->actionRefresh);
  }
}

void MainWindow::setupFullscreenAction() {
  if (!m_fullscreenAction) {
    m_fullscreenAction = new QAction(QObject::tr("Fullscreen"), this);
    m_fullscreenAction->setCheckable(true);
    m_fullscreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    m_fullscreenAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(m_fullscreenAction);

    setupFullscreenMenuAction(m_fullscreenAction);

    QObject::connect(m_fullscreenAction, &QAction::triggered,
                     [this]() {
                       bool entering = !isFullScreen();
                       if (entering) {
                         showFullScreen();
                         if (menuBar()) {
                           menuBar()->hide();
                         }
                         if (m_fullscreenAction) {
                           m_fullscreenAction->setChecked(true);
                         }
                       } else {
                         showNormal();
                         if (menuBar()) {
                           menuBar()->show();
                         }
                         if (m_fullscreenAction) {
                           m_fullscreenAction->setChecked(false);
                         }
                       }
                     });
  }
}

void MainWindow::setupShortcutsAction() {
  if (!m_shortcutsAction) {
    m_shortcutsAction = new QAction(QObject::tr("Keyboard Shortcuts"), this);
    m_shortcutsAction->setShortcut(QKeySequence(Qt::Key_F1));
    m_shortcutsAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(m_shortcutsAction);

    // Add to Help menu if it exists
    if (ui->menuHelp) {
      ui->menuHelp->addAction(m_shortcutsAction);
    }

    QObject::connect(m_shortcutsAction, &QAction::triggered, [this]() {
      ShortcutsDialog dialog(this);
      dialog.exec();
    });
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

    getSidebarManager()->setupReferences(setup);
  }

  if (getSidebarManager()) {
    QObject::connect(getSidebarManager(),
                     &SidebarManager::sidebarVisibilityChanged, this,
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
  msgBox.resize(UIConstants::Dialog::ABOUT_WIDTH,
                UIConstants::Dialog::ABOUT_HEIGHT);
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
    int sel = getSessionManager()->getLastSelectedIndex(
        m_collections[i].name);
    if (sel < 0) {
      QString hierarchical =
          CollectionUtils::hierarchicalNameFor(m_collections[i], m_collections);
      if (!hierarchical.isEmpty() &&
          hierarchical != m_collections[i].name) {
        int hSel =
            getSessionManager()->getLastSelectedIndex(hierarchical);
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
  ScrollManagerSetup setup;
  setup.ctx = &m_appContext;
  setup.generalSettings = &m_generalSettings;
  setup.artworkManager = getArtworkManager();

  getScrollManager()->setupReferences(setup);
  getScrollManager()->setDatabaseManager(getDatabaseManager());

  if ((ui) && (ui->itemScrollArea)) {
    ui->itemScrollArea->installEventFilter(
        getInteractionManager());
    if (ui->itemScrollArea->viewport()) {
      ui->itemScrollArea->viewport()->installEventFilter(
          getInteractionManager());
    }
  }
  if (gridContainer) {
    gridContainer->installEventFilter(getInteractionManager());
  }
}

void MainWindow::setupInitialTimers() {
  QTimer::singleShot(
      UIConstants::Sidebar::INITIAL_CENTER_SCROLL_DELAY_MS, this, [this]() {
        if (getScrollManager()) {
          getScrollManager()->centerHorizontalScrollbar(
              currentCollectionIndex, m_collections);
        }
      });

  if (m_collections.isEmpty()) {
    setupInitialTimersEmptyCollections();
  } else {
    setupInitialTimersWithCollections();
  }
}

void MainWindow::setupInitialTimersEmptyCollections() {
  // Defer settings dialog until after the main window is fully shown -
  // ensures proper parent-child relationship and window stacking order
  QTimer::singleShot(0, this, [this]() {
    if (getSettingsManager()) {
      int dummyIndex = currentCollectionIndex;
      SettingsDialogContext context;
      context.parent = this;
      context.collections = &m_collections;
      context.currentCollectionIndex = &dummyIndex;
      context.sidebarManager = getSidebarManager();
      context.scrollManager = getScrollManager();
      context.navigationManager = getNavigationManager();
      getSettingsManager()->openSettingsDialog(context);

      if (!m_collections.isEmpty()) {
        currentCollectionIndex = 0;
        if (getNavigationManager()) {
          getNavigationManager()->showCollectionItems(0);
        }
      }
    }
  });
}

void MainWindow::setupInitialTimersWithCollections() {
  // Defer collection loading until after the main window is fully shown -
  // allows Qt to complete layout calculations before populating the grid
  QTimer::singleShot(0, this, [this]() {
    int rootIndex = -1;
    for (int i = 0; i < m_collections.size(); ++i) {
      if (m_collections[i].parentCollectionIndex == -1) {
        rootIndex = i;
        break;
      }
    }
    if (rootIndex < 0 && !m_collections.isEmpty()) {
      rootIndex = 0;
    }
    if (rootIndex >= 0) {
      if (getNavigationManager()) {
        getNavigationManager()->showCollectionItems(rootIndex);
      }
    }
  });
}

void MainWindow::setupFullscreenMenuAction(QAction *fullscreenAction) {
  if (ui->menuView) {
    QList<QAction *> acts = ui->menuView->actions();
    QAction *insertBefore = nullptr;
    for (QAction *action : acts) {
      if (action == ui->actionSettings) {
        insertBefore = action;
        break;
      }
    }
    if (insertBefore) {
      ui->menuView->insertAction(insertBefore, fullscreenAction);
    } else {
      ui->menuView->addAction(fullscreenAction);
    }
  }
}

void MainWindow::updateWindowTitleForCollection(int collectionIndex) {
  if (collectionIndex >= 0 && collectionIndex < m_collections.size()) {
    setWindowTitle(m_collections[collectionIndex].name);
  }
}

void MainWindow::rebuildHierarchyCache() {
  m_hierarchyCache.rebuild(m_collections);
}

// Delegated Getters
SidebarManager *MainWindow::getSidebarManager() const { return m_appManager->getSidebarManager(); }
SettingsManager *MainWindow::getSettingsManager() const { return m_appManager->getSettingsManager(); }
DatabaseManager *MainWindow::getDatabaseManager() const { return m_appManager->getDatabaseManager(); }
ScrollManager *MainWindow::getScrollManager() const { return m_appManager->getScrollManager(); }
NavigationManager *MainWindow::getNavigationManager() const { return m_appManager->getNavigationManager(); }
InteractionManager *MainWindow::getInteractionManager() const { return m_appManager->getInteractionManager(); }
SessionManager *MainWindow::getSessionManager() const { return m_appManager->getSessionManager(); }
ArtworkManager *MainWindow::getArtworkManager() const { return m_appManager->getArtworkManager(); }
CacheManager *MainWindow::getCacheManager() const { return m_appManager->getCacheManager(); }