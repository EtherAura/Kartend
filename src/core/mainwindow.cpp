#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QMessageBox>
#include <QPixmapCache>

#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionconfig.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), stackedWidget(nullptr),
      itemsPage(nullptr), gridContainer(nullptr), m_mainContentWidget(nullptr),
      itemGrid(nullptr), m_mainHorizontalLayout(nullptr), searchBar(nullptr),
      m_searchModeButton(nullptr), loadingLabel(nullptr),
      currentCollectionIndex(-1),
      m_sidebarManager(nullptr),
      m_metadataSidebar(nullptr),
      m_settingsManager(nullptr),
      m_databaseManager(nullptr), m_scrollManager(nullptr),
      m_navigationManager(nullptr), m_interactionManager(nullptr) {
  ui->setupUi(this);
  setupUI();
}

MainWindow::~MainWindow() { delete ui; }
void MainWindow::keyPressEvent(QKeyEvent *event) {
  if ((m_interactionManager != nullptr) &&
      m_interactionManager->handleGlobalKeyPress(event)) {
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  if (!updatesEnabled()) {
    return;
  }

  QMainWindow::resizeEvent(event);
  if (auto *timerCoordinator =
          ArtworkManager::instance().getTimerCoordinator()) {
    timerCoordinator->scheduleLayoutUpdate();
  }
}

auto MainWindow::eventFilter(QObject *watched, QEvent *event) -> bool {
  return (m_interactionManager != nullptr)
             ? m_interactionManager->eventFilter(watched, event)
             : QMainWindow::eventFilter(watched, event);
}

static auto formatCountNumber(qint64 value) -> QString {
  QString digits = QString::number(value);
  int pos = digits.size() - 3;
  while (pos > 0) {
    digits.insert(pos, ',');
    pos -= 3;
  }
  return digits;
}

void MainWindow::refreshTitleCounts() {
  if (m_databaseManager == nullptr) {
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
        m_databaseManager->countCollectionRecursive(idx, m_collections);
    parts << formatCountNumber(countVal);
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
  setup.scrollManager = m_scrollManager.get();
  setup.sidebarManager = m_sidebarManager.get();
  setup.settingsManager = m_settingsManager.get();
  setup.databaseManager = m_databaseManager.get();
  setup.navigationManager = m_navigationManager.get();
  setup.itemScrollArea = ui->itemScrollArea;
  setup.gridContainer = gridContainer;
  setup.sidebarWidget = m_metadataSidebar;
  setup.stackedWidget = stackedWidget;
  setup.itemsPage = itemsPage;
  setup.collections = &m_collections;
  setup.currentCollectionIndex = &currentCollectionIndex;
  setup.searchBar = searchBar;
  setup.searchModeButton = m_searchModeButton;
  setup.mainWindow = this;

  loadingLabel = ui->loadingLabel;

  NavigationManagerDependencies navDeps;
  navDeps.interactionManager = m_interactionManager.get();
  navDeps.settingsManager = m_settingsManager.get();
  navDeps.sidebarManager = m_sidebarManager.get();
  navDeps.scrollManager = m_scrollManager.get();
  navDeps.databaseManager = m_databaseManager.get();
  navDeps.metadataSidebar = m_metadataSidebar;
  navDeps.currentCollectionIndex = &currentCollectionIndex;
  navDeps.collections = &m_collections;
  navDeps.generalSettings = &m_generalSettings;
  navDeps.searchBar = searchBar;
  navDeps.itemsPage = itemsPage;
  navDeps.stackedWidget = stackedWidget;
  navDeps.loadingLabel = loadingLabel;
  navDeps.itemScrollArea = ui->itemScrollArea;
  navDeps.gridContainer = gridContainer;
  navDeps.isShuttingDown = [this]() { return isShuttingDown(); };
  navDeps.refreshTitleCounts = [this]() { refreshTitleCounts(); };

  m_navigationManager->setupReferences(navDeps);

  m_interactionManager->setupReferences(setup);

  connectDatabaseManager();
  connectScrollManager();
  connectSidebarManager();
  connectSearchComponents();
  connectScrollBars();
}

void MainWindow::connectDatabaseManager() {
  QObject::connect(m_databaseManager.get(), &DatabaseManager::itemsLoaded,
                   m_navigationManager.get(), &NavigationManager::onItemsLoaded);
  QObject::connect(m_databaseManager.get(), &DatabaseManager::errorOccurred,
                   m_navigationManager.get(),
                   &NavigationManager::onMediaLibraryError);
}

void MainWindow::connectScrollManager() {
  QObject::connect(m_scrollManager.get(), &ScrollManager::widgetClicked, this,
                   [this](MediaItemWidget *widget, const QString &path) {
                     if (!QApplication::closingDown()) {
                       m_interactionManager->handleWidgetClicked(widget, path);
                     }
                   });
  QObject::connect(
      m_scrollManager.get(), &ScrollManager::widgetDoubleClickedWithCollection, this,
      [this](const QString &path, int idx) {
        if (!QApplication::closingDown()) {
          m_interactionManager->handleWidgetDoubleClickedWithCollection(path,
                                                                        idx);
        }
      });
  QObject::connect(m_scrollManager.get(), &ScrollManager::subcollectionEntered,
                   m_navigationManager.get(),
                   &NavigationManager::onSubcollectionEntered);
  QObject::connect(
      ArtworkManager::instance().getTimerCoordinator(),
      &TimerUtils::Coordinator::viewportUpdateRequested, this, []() {
        if (!QApplication::closingDown() && ArtworkManager::s_instance.load() &&
            !ArtworkManager::s_shuttingDown.load()) {
          ArtworkManager::instance().updateViewportArtwork();
        }
      });
  QObject::connect(
      ArtworkManager::instance().getTimerCoordinator(),
      &TimerUtils::Coordinator::layoutUpdateRequested, this, [this]() {
        if (!QApplication::closingDown() && (m_scrollManager != nullptr)) {
          m_scrollManager->handleLayoutChange();
        }
      });
  QObject::connect(m_scrollManager.get(), &ScrollManager::filterChanged, this,
                   [this](int visible, int total) {
                     if (!QApplication::closingDown()) {
                       updateWindowTitleWithFilter(visible, total);
                     }
                   });
}

void MainWindow::connectSidebarManager() {
  QObject::connect(
      m_sidebarManager.get(), &SidebarManager::sidebarVisibilityChanged, this,
      [this](bool visible) {
        if (visible && (m_sidebarManager != nullptr) &&
            (m_scrollManager != nullptr) && (m_interactionManager != nullptr)) {
          int sel = m_interactionManager->currentSelectedIndex();
          if (sel >= 0) {
            MediaItemWidget *widgetPtr =
                m_scrollManager->getActiveWidgets().value(sel, nullptr);
            m_sidebarManager->updateSidebarMetadata(widgetPtr);
          }
        }
        QTimer::singleShot(UIConstants::SIDEBAR_METRICS_RECALC_DELAY_MS, this,
                           [this]() {
                             if (m_scrollManager != nullptr) {
                               m_scrollManager->recalculateContainerMetrics();
                             }
                           });
      });

  QObject::connect(
      m_sidebarManager.get(), &SidebarManager::sidebarLayoutChanged, this, [this]() {
        if (m_scrollManager != nullptr) {
          m_scrollManager->recalculateContainerMetrics();
        }
        if ((m_sidebarManager != nullptr) && (m_scrollManager != nullptr) &&
            (m_interactionManager != nullptr) &&
            m_sidebarManager->isSidebarVisible()) {
          int sel = m_interactionManager->currentSelectedIndex();
          if (sel >= 0) {
            MediaItemWidget *widgetPtr =
                m_scrollManager->getActiveWidgets().value(sel, nullptr);
            m_sidebarManager->updateSidebarMetadata(widgetPtr);
          }
        }
      });
}

void MainWindow::connectSearchComponents() {
  if ((m_searchModeButton != nullptr) && (m_interactionManager != nullptr)) {
    QObject::connect(m_searchModeButton, &QPushButton::clicked,
                     m_interactionManager.get(),
                     &InteractionManager::toggleSearchMode);
  }
}

void MainWindow::connectScrollBars() const {
  if (ui->itemScrollArea != nullptr) {
    QScrollBar *vScrollBar = ui->itemScrollArea->verticalScrollBar();
    QScrollBar *hScrollBar = ui->itemScrollArea->horizontalScrollBar();

    if ((vScrollBar != nullptr) && (m_navigationManager != nullptr)) {
      QObject::connect(vScrollBar, &QScrollBar::valueChanged,
                       m_navigationManager.get(),
                       &NavigationManager::onViewportChanged);
    }
    if ((hScrollBar != nullptr) && (m_navigationManager != nullptr)) {
      QObject::connect(hScrollBar, &QScrollBar::valueChanged,
                       m_navigationManager.get(),
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
    ui->itemScrollArea->removeEventFilter(m_interactionManager.get());
    if (ui->itemScrollArea->viewport() != nullptr) {
      ui->itemScrollArea->viewport()->removeEventFilter(m_interactionManager.get());
    }
  }
  if (gridContainer != nullptr) {
    gridContainer->removeEventFilter(m_interactionManager.get());
  }

  if (m_interactionManager != nullptr) {
    m_interactionManager->clearSelection();
    m_interactionManager->blockSignals(true);
  }

  ArtworkManager::s_shuttingDown.store(true, std::memory_order_release);
  if (ArtworkManager::s_instance.load() != nullptr) {
    ArtworkManager::instance().shutdown();
  }

  if (m_scrollManager != nullptr) {
    m_scrollManager->blockSignals(true);
    m_scrollManager->cleanup();
  }

  currentCollectionIndex = -1;

  if (m_settingsManager != nullptr) {
    m_settingsManager->saveCollections(m_collections);
    m_settingsManager->saveMainScreenSettings(m_mainScreenConfig);
  }

  CacheManager::instance()
      .releaseGuiResources(); // NOLINT(readability-static-accessed-through-instance)
  if (!QApplication::closingDown()) {
    CacheManager::instance().saveToDisk();
    SessionManager::instance().saveToDisk();
  }

  event->accept();
}

void MainWindow::setupUI() {
  setupManagers();
  SessionManager::instance().initialize();
  setupUIReferences();
  createMenuBar();
  setupSidebar();
  setupManagerConnections();
  setupArtworkManager();
  setupLastSelectedIndices();
  setupEventFilters();
  setupInitialTimers();
}

void MainWindow::setupManagers() {
  m_settingsManager = std::make_unique<SettingsManager>(this);
  m_sidebarManager = std::make_unique<SidebarManager>(this);
  m_navigationManager = std::make_unique<NavigationManager>(this);
  m_interactionManager = std::make_unique<InteractionManager>(this);

  m_databaseManager = std::make_unique<DatabaseManager>(this);
  m_scrollManager = std::make_unique<ScrollManager>(this);

  m_settingsManager->loadCollections(m_collections);
  m_settingsManager->loadMainScreenSettings(m_mainScreenConfig);
  m_settingsManager->loadGeneralSettings(m_generalSettings);
}

void MainWindow::setupUIReferences() {
  setWindowTitle("Kartend");

  QPixmapCache::setCacheLimit(UIConstants::PIXMAP_CACHE_KB);

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
                       if (m_sidebarManager) {
                         m_sidebarManager->toggleSidebar();
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
          if (m_settingsManager) {
            SettingsDialogContext context;
            context.parent = this;
            context.collections = &m_collections;
            context.currentCollectionIndex = &currentCollectionIndex;
            context.sidebarManager = m_sidebarManager.get();
            context.scrollManager = m_scrollManager.get();
            context.navigationManager = m_navigationManager.get();
            m_settingsManager->openSettingsDialog(context);
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
  if (m_sidebarManager != nullptr) {
    m_sidebarManager->setupSidebar();

    SidebarManagerSetup setup;
    setup.sidebar = m_metadataSidebar;
    setup.itemsPage = itemsPage;
    setup.mainLayout = m_mainHorizontalLayout;
    setup.scrollArea = (ui != nullptr) ? ui->itemScrollArea : nullptr;
    setup.settingsManager = m_settingsManager.get();
    setup.collections = &m_collections;

    m_sidebarManager->setupReferences(setup);
  }

  if (m_sidebarManager != nullptr) {
    QObject::connect(m_sidebarManager.get(),
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
  ArtworkManager::initializeCache();
  ArtworkManager &artMgr = ArtworkManager::instance();

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
  for (int i = 0; i < m_collections.size(); ++i) {
    int sel = SessionManager::instance().getLastSelectedIndex(
        m_collections[i].name);
    if (sel < 0) {
      QString hierarchical =
          hierarchicalNameFor(m_collections[i], m_collections);
      if (!hierarchical.isEmpty() &&
          hierarchical != m_collections[i].name) {
        int hSel =
            SessionManager::instance().getLastSelectedIndex(hierarchical);
        if (hSel >= 0) {
          sel = hSel;
        }
      }
    }
    if (sel >= 0) {
      m_settingsManager->setLastSelectedItem(i, sel);
    }
  }
}

void MainWindow::setupEventFilters() {
  ScrollManagerSetup setup;
  setup.gridContainer = gridContainer;
  setup.mediaScrollArea = ui->itemScrollArea;
  setup.collections = &m_collections;

  m_scrollManager->setupReferences(setup);
  m_scrollManager->setDatabaseManager(m_databaseManager.get());

  if ((ui != nullptr) && (ui->itemScrollArea != nullptr)) {
    ui->itemScrollArea->installEventFilter(
        m_interactionManager.get());
    if (ui->itemScrollArea->viewport() != nullptr) {
      ui->itemScrollArea->viewport()->installEventFilter(
          m_interactionManager.get());
    }
  }
  if (gridContainer != nullptr) {
    gridContainer->installEventFilter(m_interactionManager.get());
  }
}

void MainWindow::setupInitialTimers() {
  QTimer::singleShot(
      UIConstants::INITIAL_CENTER_SCROLL_DELAY_MS, this, [this]() {
        if (m_scrollManager != nullptr) {
          m_scrollManager->centerHorizontalScrollbar(
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
    if (m_settingsManager != nullptr) {
      int dummyIndex = currentCollectionIndex;
      SettingsDialogContext context;
      context.parent = this;
      context.collections = &m_collections;
      context.currentCollectionIndex = &dummyIndex;
      context.sidebarManager = m_sidebarManager.get();
      context.scrollManager = m_scrollManager.get();
      context.navigationManager = m_navigationManager.get();
      m_settingsManager->openSettingsDialog(context);

      if (!m_collections.isEmpty()) {
        currentCollectionIndex = 0;
        if (m_navigationManager) {
          m_navigationManager->showCollectionItems(0);
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
      if (m_navigationManager) {
        m_navigationManager->showCollectionItems(rootIndex);
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