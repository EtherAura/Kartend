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
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "settingsutils.h"
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
      m_metadataSidebar(nullptr) {
  m_appManager = std::make_unique<ApplicationManager>(this);
  m_appManager->initialize();
  
  ui->setupUi(this);
  setupUI();
}

MainWindow::~MainWindow() { delete ui; }
void MainWindow::keyPressEvent(QKeyEvent *event) {
  if ((getInteractionManager() != nullptr) &&
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
  QTimer::singleShot(UIConstants::LAYOUT_UPDATE_DELAY + 50, this, [this]() {
    if (!QApplication::closingDown() && getInteractionManager() != nullptr) {
      getInteractionManager()->recenterCurrentSelection();
    }
  });
}

auto MainWindow::eventFilter(QObject *watched, QEvent *event) -> bool {
  return (getInteractionManager() != nullptr)
             ? getInteractionManager()->eventFilter(watched, event)
             : QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshTitleCounts() {
  if (getDatabaseManager() == nullptr) {
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
  setup.scrollManager = getScrollManager();
  setup.sidebarManager = getSidebarManager();
  setup.settingsManager = getSettingsManager();
  setup.databaseManager = getDatabaseManager();
  setup.navigationManager = getNavigationManager();
  setup.sessionManager = getSessionManager();
  setup.artworkManager = getArtworkManager();
  setup.itemScrollArea = ui->itemScrollArea;
  setup.gridContainer = gridContainer;
  setup.sidebar = m_metadataSidebar;
  setup.stackedWidget = stackedWidget;
  setup.itemsPage = itemsPage;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &currentCollectionIndex;
  setup.searchBar = searchBar;
  setup.searchModeButton = m_searchModeButton;
  setup.generalSettings = &m_generalSettings;
  setup.isShuttingDown = &m_isShuttingDown;
  setup.hierarchyCache = &m_hierarchyCache;

  loadingLabel = ui->loadingLabel;

  NavigationManagerSetup navSetup;
  navSetup.interactionManager = getInteractionManager();
  navSetup.settingsManager = getSettingsManager();
  navSetup.sidebarManager = getSidebarManager();
  navSetup.scrollManager = getScrollManager();
  navSetup.databaseManager = getDatabaseManager();
  navSetup.sessionManager = getSessionManager();
  navSetup.artworkManager = getArtworkManager();
  navSetup.sidebar = m_metadataSidebar;
  navSetup.currentCollectionIndex = &currentCollectionIndex;
  navSetup.collections = &m_collections;
  navSetup.hierarchyCache = &m_hierarchyCache;
  navSetup.generalSettings = &m_generalSettings;
  navSetup.searchBar = searchBar;
  navSetup.itemsPage = itemsPage;
  navSetup.stackedWidget = stackedWidget;
  navSetup.loadingLabel = loadingLabel;
  navSetup.itemScrollArea = ui->itemScrollArea;
  navSetup.gridContainer = gridContainer;
  navSetup.isShuttingDown = [this]() { return isShuttingDown(); };
  navSetup.refreshTitleCounts = [this]() { refreshTitleCounts(); };

  getNavigationManager()->setupReferences(navSetup);

  getInteractionManager()->setupReferences(setup);

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
  QObject::connect(getScrollManager(), &ScrollManager::widgetClicked, this,
                   [this](MediaItemWidget *widget, const QString &path) {
                     if (!QApplication::closingDown()) {
                       getInteractionManager()->handleWidgetClicked(widget, path);
                     }
                   });
  QObject::connect(
      getScrollManager(), &ScrollManager::widgetDoubleClickedWithCollection, this,
      [this](const QString &path, int idx) {
        if (!QApplication::closingDown()) {
          getInteractionManager()->handleWidgetDoubleClickedWithCollection(path,
                                                                        idx);
        }
      });
  QObject::connect(getScrollManager(), &ScrollManager::subcollectionEntered,
                   getNavigationManager(),
                   &NavigationManager::onSubcollectionEntered);
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
                           (getScrollManager() != nullptr)) {
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
        if (visible && (getSidebarManager() != nullptr) &&
            (getScrollManager() != nullptr) && (getInteractionManager() != nullptr)) {
          int sel = getInteractionManager()->currentSelectedIndex();
          if (sel >= 0) {
            MediaItemWidget *widgetPtr =
                getScrollManager()->getActiveWidgets().value(sel, nullptr);
            getSidebarManager()->updateSidebarMetadata(widgetPtr);
          }
        }
        QTimer::singleShot(UIConstants::SIDEBAR_METRICS_RECALC_DELAY_MS, this,
                           [this]() {
                             if (getScrollManager() != nullptr) {
                               getScrollManager()->recalculateContainerMetrics();
                             }
                           });
      });

  QObject::connect(
      getSidebarManager(), &SidebarManager::sidebarLayoutChanged, this, [this]() {
        if (getScrollManager() != nullptr) {
          getScrollManager()->recalculateContainerMetrics();
        }
        if ((getSidebarManager() != nullptr) && (getScrollManager() != nullptr) &&
            (getInteractionManager() != nullptr) &&
            getSidebarManager()->isSidebarVisible()) {
          int sel = getInteractionManager()->currentSelectedIndex();
          if (sel >= 0) {
            MediaItemWidget *widgetPtr =
                getScrollManager()->getActiveWidgets().value(sel, nullptr);
            getSidebarManager()->updateSidebarMetadata(widgetPtr);
          }
        }
      });
}

void MainWindow::connectSearchComponents() {
  if ((m_searchModeButton != nullptr) && (getInteractionManager() != nullptr)) {
    QObject::connect(m_searchModeButton, &QPushButton::clicked,
                     getInteractionManager(),
                     &InteractionManager::toggleSearchMode);
  }
}

void MainWindow::connectScrollBars() const {
  if (ui->itemScrollArea != nullptr) {
    QScrollBar *vScrollBar = ui->itemScrollArea->verticalScrollBar();
    QScrollBar *hScrollBar = ui->itemScrollArea->horizontalScrollBar();

    if ((vScrollBar != nullptr) && (getNavigationManager() != nullptr)) {
      QObject::connect(vScrollBar, &QScrollBar::valueChanged,
                       getNavigationManager(),
                       &NavigationManager::onViewportChanged);
    }
    if ((hScrollBar != nullptr) && (getNavigationManager() != nullptr)) {
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

  if (ui->itemScrollArea != nullptr) {
    ui->itemScrollArea->removeEventFilter(getInteractionManager());
    if (ui->itemScrollArea->viewport() != nullptr) {
      ui->itemScrollArea->viewport()->removeEventFilter(getInteractionManager());
    }
  }
  if (gridContainer != nullptr) {
    gridContainer->removeEventFilter(getInteractionManager());
  }

  if (getInteractionManager() != nullptr) {
    getInteractionManager()->clearSelection();
    getInteractionManager()->blockSignals(true);
  }

  if (getScrollManager() != nullptr) {
    getScrollManager()->blockSignals(true);
    getScrollManager()->cleanup();
  }

  currentCollectionIndex = -1;

  if (getSettingsManager() != nullptr) {
    getSettingsManager()->saveCollections(m_collections);
    SettingsUtils::saveMainScreenSettings(m_mainScreenConfig);
  }

  if (getCacheManager()) {
    getCacheManager()->releaseGuiResources();
    if (!QApplication::closingDown()) {
      getCacheManager()->saveToDisk();
    }
  }
  if (!QApplication::closingDown()) {
    if (getSessionManager()) {
      getSessionManager()->saveToDisk();
    }
  }

  event->accept();
}

void MainWindow::setupUI() {
  // Managers are initialized by ApplicationManager in the constructor
  getSessionManager()->initialize();
  
  // Load settings
  getSettingsManager()->loadCollections(m_collections);
  rebuildHierarchyCache();
  SettingsUtils::loadMainScreenSettings(m_mainScreenConfig);
  getSettingsManager()->loadGeneralSettings(m_generalSettings);

  setupUIReferences();
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
  m_metadataSidebar = ui->metadataSidebarWidget;
}

void MainWindow::createMenuBar() {
  setupActionExit();
  setupActionShowSidebar();
  setupActionSettings();
  setupActionAbout();
  setupFullscreenAction();
}

void MainWindow::setupActionExit() {
  if (ui->actionExit != nullptr) {
    QObject::connect(ui->actionExit, &QAction::triggered, this,
                     &QWidget::close);
    ui->actionExit->setShortcutContext(Qt::ApplicationShortcut);
    addAction(ui->actionExit);
  }
}

void MainWindow::setupActionShowSidebar() {
  if (ui->actionShowSidebar != nullptr) {
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
  if (ui->actionSettings != nullptr) {
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
  if (ui->actionAbout != nullptr) {
    QObject::connect(ui->actionAbout, &QAction::triggered,
                     [this]() { showAbout(); });
  }
}

void MainWindow::setupFullscreenAction() {
  if (m_fullscreenAction == nullptr) {
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
                         if (menuBar() != nullptr) {
                           menuBar()->hide();
                         }
                         if (m_fullscreenAction != nullptr) {
                           m_fullscreenAction->setChecked(true);
                         }
                       } else {
                         showNormal();
                         if (menuBar() != nullptr) {
                           menuBar()->show();
                         }
                         if (m_fullscreenAction != nullptr) {
                           m_fullscreenAction->setChecked(false);
                         }
                       }
                     });
  }
}

void MainWindow::setupSidebar() {
  if (getSidebarManager() != nullptr) {
    getSidebarManager()->setupSidebar();

    SidebarManagerSetup setup;
    setup.sidebar = m_metadataSidebar;
    setup.itemsPage = itemsPage;
    setup.mainLayout = m_mainHorizontalLayout;
    setup.scrollArea = (ui != nullptr) ? ui->itemScrollArea : nullptr;
    setup.settingsManager = getSettingsManager();
    setup.artworkManager = getArtworkManager();
    setup.collections = &m_collections;

    getSidebarManager()->setupReferences(setup);
  }

  if (getSidebarManager() != nullptr) {
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
  QString appName = APP_NAME;
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
  msgBox.resize(UIConstants::ABOUT_DIALOG_WIDTH,
                UIConstants::ABOUT_DIALOG_HEIGHT);
  msgBox.exec();
}

void MainWindow::setupArtworkManager() {
  if (getArtworkManager()) {
    getArtworkManager()->initializeCache();
  }
  ArtworkManager &artMgr = *getArtworkManager();

  ArtworkManagerSetup setup;
  setup.stackedWidget = stackedWidget;
  setup.itemsPage = itemsPage;
  setup.gridContainer = gridContainer;
  setup.itemScrollArea = (ui != nullptr) ? ui->itemScrollArea : nullptr;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &currentCollectionIndex;

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
  setup.gridContainer = gridContainer;
  setup.mediaScrollArea = ui->itemScrollArea;
  setup.artworkManager = getArtworkManager();
  setup.collections = &m_collections;
  setup.hierarchyCache = &m_hierarchyCache;

  getScrollManager()->setupReferences(setup);
  getScrollManager()->setDatabaseManager(getDatabaseManager());

  if ((ui != nullptr) && (ui->itemScrollArea != nullptr)) {
    ui->itemScrollArea->installEventFilter(
        getInteractionManager());
    if (ui->itemScrollArea->viewport() != nullptr) {
      ui->itemScrollArea->viewport()->installEventFilter(
          getInteractionManager());
    }
  }
  if (gridContainer != nullptr) {
    gridContainer->installEventFilter(getInteractionManager());
  }
}

void MainWindow::setupInitialTimers() {
  QTimer::singleShot(
      UIConstants::INITIAL_CENTER_SCROLL_DELAY_MS, this, [this]() {
        if (getScrollManager() != nullptr) {
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
  QTimer::singleShot(0, this, [this]() {
    if (getSettingsManager() != nullptr) {
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
  if (ui->menuView != nullptr) {
    QList<QAction *> acts = ui->menuView->actions();
    QAction *insertBefore = nullptr;
    for (QAction *action : acts) {
      if (action == ui->actionSettings) {
        insertBefore = action;
        break;
      }
    }
    if (insertBefore != nullptr) {
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