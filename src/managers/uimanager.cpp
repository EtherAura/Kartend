#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <QPixmapCache>
#include <QTimer>

#include "artworkmanager.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"
#include "uimanager.h"

// Sets up main window managers, wiring, and seeds last-selected indices
void UIManager::setupMainWindow(MainWindow *window) {
  setupManagers(window);
  SessionManager::instance().initialize();
  setupUIReferences(window);
  createMenuBar(window);
  setupSidebar(window);
  window->setupManagerConnections();
  setupArtworkManager(window);
  setupLastSelectedIndices(window);
  setupEventFilters(window);
  setupInitialTimers(window);
}

void UIManager::setupManagers(MainWindow *window) {
  window->m_settingsManager = new SettingsManager(window);
  window->m_sidebarManager = new SidebarManager(window);
  window->m_sidebarManager->setSettingsManager(window->m_settingsManager);
  window->m_sidebarManager->setCollections(&window->m_collections);
  window->m_navigationManager = new NavigationManager(window);
  window->m_interactionManager = new InteractionManager(window);

  window->m_databaseManager = new DatabaseManager(window);
  window->m_scrollManager = new ScrollManager(window);

  window->m_settingsManager->loadCollections(
      window
          ->m_collections); // NOLINT(readability-static-accessed-through-instance)
  window->m_settingsManager->loadMainScreenSettings(window->m_mainScreenConfig);
  window->m_settingsManager->loadGeneralSettings(window->m_generalSettings);
}

void UIManager::setupUIReferences(MainWindow *window) {
  window->setWindowTitle("Kartend");

  QPixmapCache::setCacheLimit(UIConstants::PIXMAP_CACHE_KB);

  window->stackedWidget = window->ui->stackedWidget;
  window->itemsPage = window->ui->itemsPage;
  window->gridContainer = window->ui->gridContainer;
  window->itemGrid = window->ui->itemGrid;
  window->m_mainContentWidget = window->ui->m_mainContentWidget;
  window->m_mainHorizontalLayout = window->ui->m_mainHorizontalLayout;
  window->searchBar = window->ui->searchBar;
  window->m_searchModeButton = window->ui->searchModeButton;
  window->m_metadataSidebar = window->ui->metadataSidebarWidget;
}

void UIManager::createMenuBar(MainWindow *window) {
  setupActionExit(window);
  setupActionShowSidebar(window);
  setupActionSettings(window);
  setupActionAbout(window);
  setupFullscreenAction(window);
}

void UIManager::setupActionExit(MainWindow *window) {
  if (window->ui->actionExit != nullptr) {
    QObject::connect(window->ui->actionExit, &QAction::triggered, window,
                     &QWidget::close);
    window->ui->actionExit->setShortcutContext(Qt::ApplicationShortcut);
    window->addAction(window->ui->actionExit);
  }
}

void UIManager::setupActionShowSidebar(MainWindow *window) {
  if (window->ui->actionShowSidebar != nullptr) {
    QObject::connect(window->ui->actionShowSidebar, &QAction::triggered,
                     [window]() {
                       if (window->m_sidebarManager) {
                         window->m_sidebarManager->toggleSidebar();
                       }
                     });
    window->ui->actionShowSidebar->setShortcutContext(Qt::ApplicationShortcut);
    window->addAction(window->ui->actionShowSidebar);
  }
}

void UIManager::setupActionSettings(MainWindow *window) {
  if (window->ui->actionSettings != nullptr) {
    QObject::connect(
        window->ui->actionSettings, &QAction::triggered, [window]() {
          if (window->m_settingsManager) {
            window->m_settingsManager->openSettingsDialog(
                window, window->m_collections, window->currentCollectionIndex,
                window->m_sidebarManager, window->m_scrollManager,
                window->m_navigationManager);
          }
        });
    window->ui->actionSettings->setShortcutContext(Qt::ApplicationShortcut);
    window->addAction(window->ui->actionSettings);
  }
}

void UIManager::setupActionAbout(MainWindow *window) {
  if (window->ui->actionAbout != nullptr) {
    QObject::connect(window->ui->actionAbout, &QAction::triggered,
                     [window]() { UIManager::showAbout(window); });
  }
}

void UIManager::setupFullscreenAction(MainWindow *window) {
  if (window->m_fullscreenAction == nullptr) {
    window->m_fullscreenAction = new QAction(QObject::tr("Fullscreen"), window);
    window->m_fullscreenAction->setCheckable(true);
    window->m_fullscreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    window->m_fullscreenAction->setShortcutContext(Qt::ApplicationShortcut);
    window->addAction(window->m_fullscreenAction);

    setupFullscreenMenuAction(window, window->m_fullscreenAction);

    QObject::connect(window->m_fullscreenAction, &QAction::triggered,
                     [window]() {
                       bool entering = !window->isFullScreen();
                       if (entering) {
                         window->showFullScreen();
                         if (window->menuBar() != nullptr) {
                           window->menuBar()->hide();
                         }
                         if (window->m_fullscreenAction != nullptr) {
                           window->m_fullscreenAction->setChecked(true);
                         }
                       } else {
                         window->showNormal();
                         if (window->menuBar() != nullptr) {
                           window->menuBar()->show();
                         }
                         if (window->m_fullscreenAction != nullptr) {
                           window->m_fullscreenAction->setChecked(false);
                         }
                       }
                     });
  }
}

void UIManager::setupSidebar(MainWindow *window) {
  if (window->m_sidebarManager != nullptr) {
    window->m_sidebarManager->setupSidebar();
    window->m_sidebarManager->setupReferences(
        window->m_metadataSidebar, window->itemsPage,
        window->m_mainHorizontalLayout,
        (window->ui != nullptr) ? window->ui->itemScrollArea : nullptr);
  }

  if (window->m_sidebarManager != nullptr) {
    QObject::connect(window->m_sidebarManager,
                     &SidebarManager::sidebarVisibilityChanged, window,
                     [window](bool visible) {
                       if (window->ui->actionShowSidebar) {
                         window->ui->actionShowSidebar->blockSignals(true);
                         window->ui->actionShowSidebar->setChecked(visible);
                         window->ui->actionShowSidebar->blockSignals(false);
                       }
                     });
  }
}

void UIManager::showAbout(QWidget *parent) {
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

  QMessageBox msgBox(parent);
  msgBox.setWindowTitle("About");
  msgBox.setText(aboutText);
  msgBox.setTextFormat(Qt::RichText);
  msgBox.setStandardButtons(QMessageBox::Ok);
  msgBox.resize(UIConstants::ABOUT_DIALOG_WIDTH,
                UIConstants::ABOUT_DIALOG_HEIGHT);
  msgBox.exec();
}

void UIManager::updateWindowTitleForCollection(
    QWidget *window, int collectionIndex,
    const QList<CollectionConfig> &collections) {
  if (collectionIndex >= 0 && collectionIndex < collections.size()) {
    if (auto *mainWindow = qobject_cast<QMainWindow *>(window)) {
      mainWindow->setWindowTitle(collections[collectionIndex].name);
    }
  }
}

// Handles main window resize by coalescing a layout update through
// TimerCoordinator.
void UIManager::handleResizeEvent(MainWindow *window, QResizeEvent *event) {
  window->QMainWindow::resizeEvent(event);
  if (auto *timerCoordinator =
          ArtworkManager::instance().getTimerCoordinator()) {
    timerCoordinator->scheduleLayoutUpdate();
  }
}

void UIManager::setupArtworkManager(MainWindow *window) {
  ArtworkManager::initializeCache();
  ArtworkManager &artMgr = ArtworkManager::instance();
  artMgr.setupUIReferences(
      window->stackedWidget, window->itemsPage, window->gridContainer,
      (window->ui != nullptr) ? window->ui->itemScrollArea : nullptr);
  artMgr.setupDataReferences(&window->m_collections,
                             &window->currentCollectionIndex);
}

void UIManager::setupLastSelectedIndices(MainWindow *window) {
  for (int i = 0; i < window->m_collections.size(); ++i) {
    int sel = SessionManager::instance().getLastSelectedIndex(
        window->m_collections[i].name);
    if (sel < 0) {
      QString hierarchical =
          hierarchicalNameFor(window->m_collections[i], window->m_collections);
      if (!hierarchical.isEmpty() &&
          hierarchical != window->m_collections[i].name) {
        int hSel =
            SessionManager::instance().getLastSelectedIndex(hierarchical);
        if (hSel >= 0) {
          sel = hSel;
        }
      }
    }
    if (sel >= 0) {
      window->m_settingsManager->setLastSelectedItem(i, sel);
    }
  }
}

void UIManager::setupEventFilters(MainWindow *window) {
  window->m_scrollManager->setupReferences(window->gridContainer,
                                           window->ui->itemScrollArea);
  window->m_scrollManager->setDatabaseManager(window->m_databaseManager);
  window->m_scrollManager->setCollectionsReference(&window->m_collections);

  if ((window->ui != nullptr) && (window->ui->itemScrollArea != nullptr)) {
    window->ui->itemScrollArea->installEventFilter(
        window->m_interactionManager);
    if (window->ui->itemScrollArea->viewport() != nullptr) {
      window->ui->itemScrollArea->viewport()->installEventFilter(
          window->m_interactionManager);
    }
  }
  if (window->gridContainer != nullptr) {
    window->gridContainer->installEventFilter(window->m_interactionManager);
  }
}

void UIManager::setupInitialTimers(MainWindow *window) {
  QTimer::singleShot(
      UIConstants::INITIAL_CENTER_SCROLL_DELAY_MS, window, [window]() {
        if (window->m_scrollManager != nullptr) {
          window->m_scrollManager->centerHorizontalScrollbar(
              window->currentCollectionIndex, window->m_collections);
        }
      });

  if (window->m_collections.isEmpty()) {
    setupInitialTimersEmptyCollections(window);
  } else {
    setupInitialTimersWithCollections(window);
  }
}

void UIManager::setupInitialTimersEmptyCollections(MainWindow *window) {
  QTimer::singleShot(0, window, [window]() {
    if (window->m_settingsManager != nullptr) {
      int dummyIndex = window->currentCollectionIndex;
      window->m_settingsManager->openSettingsDialog(
          window, window->m_collections, dummyIndex, window->m_sidebarManager,
          window->m_scrollManager, window->m_navigationManager);
      if (!window->m_collections.isEmpty()) {
        window->currentCollectionIndex = 0;
        if (window->m_navigationManager) {
          window->m_navigationManager->showCollectionItems(0);
        }
      }
    }
  });
}

void UIManager::setupInitialTimersWithCollections(MainWindow *window) {
  QTimer::singleShot(0, window, [window]() {
    int rootIndex = -1;
    for (int i = 0; i < window->m_collections.size(); ++i) {
      if (window->m_collections[i].parentCollectionIndex == -1) {
        rootIndex = i;
        break;
      }
    }
    if (rootIndex < 0 && !window->m_collections.isEmpty()) {
      rootIndex = 0;
    }
    if (rootIndex >= 0) {
      if (window->m_navigationManager) {
        window->m_navigationManager->showCollectionItems(rootIndex);
      }
    }
  });
}

void UIManager::setupFullscreenMenuAction(MainWindow *window,
                                          QAction *fullscreenAction) {
  if (window->ui->menuView != nullptr) {
    QList<QAction *> acts = window->ui->menuView->actions();
    QAction *insertBefore = nullptr;
    for (QAction *action : acts) {
      if (action == window->ui->actionSettings) {
        insertBefore = action;
        break;
      }
    }
    if (insertBefore != nullptr) {
      window->ui->menuView->insertAction(insertBefore, fullscreenAction);
    } else {
      window->ui->menuView->addAction(fullscreenAction);
    }
  }
}
