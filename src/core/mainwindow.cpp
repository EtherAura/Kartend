// Main application window that owns ApplicationManager and orchestrates UI
// setup.
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
#include "launchmanager.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "menucontroller.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "nowplayingoverlay.h"
#include "propertyutils.h"
#include "scrollmanager.h"
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
Q_LOGGING_CATEGORY(lcMainWindow, "kartend.mainwindow")

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), stackedWidget(nullptr), itemsPage(nullptr),
      gridContainer(nullptr), m_mainContentWidget(nullptr), itemGrid(nullptr),
      m_mainHorizontalLayout(nullptr), searchBar(nullptr), m_searchModeButton(nullptr),
      loadingLabel(nullptr), currentCollectionIndex(-1), m_MetadataSidebar(nullptr) {
  m_appManager = std::make_unique<ApplicationManager>(this);
  m_appManager->initialize();

  ui->setupUi(this);
  setupUI();
}

MainWindow::~MainWindow() {
  delete ui;
}

bool MainWindow::event(QEvent *event) {
  if (event && !m_isShuttingDown && !QApplication::closingDown()) {
    switch (event->type()) {
    case QEvent::WindowDeactivate:
      if (!QApplication::activeModalWidget() && !QApplication::activePopupWidget()) {
        m_windowWasInactive = true;
      }
      break;
    case QEvent::WindowActivate:
      if (m_windowWasInactive) {
        m_windowWasInactive = false;
        if (m_startupSplashHandled && !QApplication::activeModalWidget() &&
            !QApplication::activePopupWidget()) {
          showFocusReturnSplash();
        }
      }
      break;
    default:
      break;
    }
  }

  return QMainWindow::event(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
  if ((getInteractionManager()) && getInteractionManager()->handleGlobalKeyPress(event)) {
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void MainWindow::showStartupSplash() {
  m_startupSplashHandled = true;
  if (m_generalSettings.bootSplashEnabled && m_splashOverlay) {
    m_splashOverlay->showSplash(SplashOverlay::Reason::Startup);
  }
}

void MainWindow::showFocusReturnSplash() {
  if (m_generalSettings.resumeFocusSplashEnabled && m_splashOverlay) {
    m_splashOverlay->showSplash(SplashOverlay::Reason::FocusReturn);
  }
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
  return (getInteractionManager()) ? getInteractionManager()->eventFilter(watched, event)
                                   : QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshTitleCounts() {
  if (!getDatabaseManager()) {
    return;
  }

  // Don't update title bar with counts while a scan is in progress
  // (the scan progress handler sets the title instead)
  if (m_loadingOverlay && m_loadingOverlay->isActive()) {
    return;
  }

  int cur = currentCollectionIndex;
  if (cur < 0 || cur >= m_collections.size()) {
    setWindowTitle(qApp->applicationName());
    return;
  }

  auto cachedRecursiveCountForIndex = [this](int collectionIndex) -> qint64 {
    if (!getSessionManager()) {
      return -1;
    }
    if (collectionIndex < 0 || collectionIndex >= m_collections.size()) {
      return -1;
    }
    qint64 direct = -1;
    qint64 recursive = -1;
    if (!getSessionManager()->getCollectionCounts(m_collections[collectionIndex], m_collections,
                                                  direct, recursive)) {
      return -1;
    }
    return recursive;
  };

  // Check if we're in a subfolder
  const QString &subfolder = m_collections[cur].currentSubfolder;
  if (!subfolder.isEmpty() && getScrollManager()) {
    // In a subfolder: show "SubfolderName (subfolderCount/collectionCount
    // Items)"
    QString subfolderName = subfolder;
    int lastSlash = subfolder.lastIndexOf('/');
    if (lastSlash >= 0) {
      subfolderName = subfolder.mid(lastSlash + 1);
    }

    int subfolderItemCount = getScrollManager()->getTotalItems();

    const qint64 collectionCount = cachedRecursiveCountForIndex(cur);
    QString counts;
    if (collectionCount >= 0) {
      counts = QString("(%1/%2 Items)")
                   .arg(StringUtils::formatCountNumber(subfolderItemCount))
                   .arg(StringUtils::formatCountNumber(collectionCount));
    } else {
      counts = QString("(%1 Items)").arg(StringUtils::formatCountNumber(subfolderItemCount));
    }

    int directSubfolderCount = CollectionUtils::countVirtualFolders(m_collections[cur]);
    int directSubcollectionCount = CollectionUtils::directChildrenOf(cur, m_collections).size();

    QString title = QString("%1 %2").arg(subfolderName, counts);
    QStringList childParts;
    if (directSubfolderCount > 0) {
      childParts << QString("%1 subfolders").arg(directSubfolderCount);
    }
    if (directSubcollectionCount > 0) {
      childParts << QString("%1 subcollections").arg(directSubcollectionCount);
    }
    if (!childParts.isEmpty()) {
      title += QString(" — %1").arg(childParts.join(", "));
    }
    setWindowTitle(title);
    return;
  }

  // Not in subfolder: show collection hierarchy counts
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

  // When showAllSubcollectionItems is enabled, the displayed items include all
  // descendant items. Use the actual view count for the current collection
  // rather than the cached recursive count (which may not include flattened
  // items).
  const bool showAllItems = m_collections[cur].showAllSubcollectionItems;
  const int viewTotalItems = getScrollManager() ? getScrollManager()->getTotalItems() : -1;

  QStringList parts;
  bool anyKnown = false;
  for (int i = 0; i < chain.size(); ++i) {
    int idx = chain[i];
    qint64 countVal = -1;

    // For the current collection (first in chain) with
    // showAllSubcollectionItems, use the actual view count which includes
    // flattened descendant items
    if (i == 0 && showAllItems && viewTotalItems >= 0) {
      countVal = viewTotalItems;
    } else {
      countVal = cachedRecursiveCountForIndex(idx);
    }

    if (countVal >= 0) {
      anyKnown = true;
      parts << StringUtils::formatCountNumber(countVal);
    } else {
      parts << QStringLiteral("…");
    }
  }

  QString base = m_collections[cur].name;
  QString counts;
  if (anyKnown) {
    if (parts.size() == 1) {
      counts = QString("(%1 Items)").arg(parts.first());
    } else {
      counts = QString("(%1 Items)").arg(parts.join('/'));
    }
  }

  int directSubfolderCount = CollectionUtils::countVirtualFolders(m_collections[cur]);
  int directSubcollectionCount = CollectionUtils::directChildrenOf(cur, m_collections).size();

  QString title = counts.isEmpty() ? base : QString("%1 %2").arg(base, counts);
  QStringList childParts;
  if (directSubfolderCount > 0) {
    childParts << QString("%1 subfolders").arg(directSubfolderCount);
  }
  if (directSubcollectionCount > 0) {
    childParts << QString("%1 subcollections").arg(directSubcollectionCount);
  }
  if (!childParts.isEmpty()) {
    title += QString(" — %1").arg(childParts.join(", "));
  }
  setWindowTitle(title);
}

// Wires managers and signals; ensures sidebar metadata is refreshed when the
// sidebar becomes visible or its layout changes
void MainWindow::setupManagerConnections() {
  InteractionManagerSetup setup;
  setup.ctx = &m_appContext; // Managers and UI elements from shared context

  loadingLabel = ui->loadingLabel;

  // CRITICAL: Set interactionState BEFORE setupReferences so sub-managers
  // can access it via ctx during their own setupReferences calls
  m_appContext.managers.interactionState = &getInteractionManager()->state();

  // Set up InteractionManager (its sub-managers will now get valid
  // interactionState)
  getInteractionManager()->setupReferences(setup);

  // Register InteractionManager's owned sub-managers in ApplicationContext
  // This enables sub-managers to access siblings directly via ctx
  m_appContext.managers.animationManager = getInteractionManager()->animationManager();
  m_appContext.managers.selectionManager = getInteractionManager()->selectionManager();
  m_appContext.managers.viewportManager = getInteractionManager()->viewportManager();
  m_appContext.managers.mouseManager = getInteractionManager()->mouseManager();
  m_appContext.managers.keyboardManager = getInteractionManager()->keyboardManager();
  m_appContext.managers.eventManager = getInteractionManager()->eventManager();
  m_appContext.managers.searchManager = getInteractionManager()->searchManager();
  m_appContext.managers.launchManager = getInteractionManager()->launchManager();

  // Kartend-qxv: when runtime detection is enabled, the LaunchManager spawns
  // a tracked QProcess and emits started/finished signals. Show a "Now
  // Playing" overlay while the child runs and raise the window when it exits.
  if (auto *launch = getInteractionManager()->launchManager()) {
    connect(launch, &LaunchManager::runtimeStarted, this,
            [this](const QString & /*filePath*/, const QString &displayName) {
              if (m_nowPlayingOverlay) {
                m_nowPlayingOverlay->showOverlay(displayName);
              }
            });
    connect(launch, &LaunchManager::runtimeFinished, this, [this](const QString & /*filePath*/) {
      if (m_nowPlayingOverlay) {
        m_nowPlayingOverlay->hideOverlay();
      }
      // Bring Kartend back to the foreground when the tracked child
      // exits — the user expects "return on close" behavior.
      raise();
      activateWindow();
    });
  }

  // Now set up NavigationManager with fully populated context
  NavigationManagerSetup navSetup;
  navSetup.ctx = &m_appContext; // Managers and UI elements from shared context

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

void MainWindow::updateWindowTitleWithFilter(int visible, int total) {
  if (currentCollectionIndex >= 0 && currentCollectionIndex < m_collections.size()) {
    QString base = m_collections[currentCollectionIndex].name;
    if (visible < total) {
      setWindowTitle(QString("%1 (%2/%3 items)").arg(base).arg(visible).arg(total));
    } else {
      setWindowTitle(QString("%1 (%2 items)").arg(base).arg(total));
    }
  }
  // Keep the top-bar position label in sync with the denominator.
  updateItemPositionLabel();
}

void MainWindow::updateItemPositionLabel() {
  // Kartend-tof: show "<pos> / <total>" next to the view-mode buttons.
  // Hidden until we have a concrete total. `currentSelectedIndex()` is a
  // visual index (includes subcollection tiles + virtual folders + media
  // files), which matches ScrollManager::getTotalItems() — so presenting
  // them as a fraction is coherent without further translation.
  if (!ui->itemPositionLabel) {
    return;
  }
  const int total = getScrollManager() ? getScrollManager()->getTotalItems() : 0;
  if (total <= 0) {
    ui->itemPositionLabel->clear();
    ui->itemPositionLabel->setVisible(false);
    return;
  }
  const int sel = getInteractionManager() ? getInteractionManager()->currentSelectedIndex() : -1;
  if (sel < 0) {
    ui->itemPositionLabel->setText(QString("%1").arg(total));
  } else {
    // User-facing positions are 1-based.
    ui->itemPositionLabel->setText(QString("%1 / %2").arg(sel + 1).arg(total));
  }
  ui->itemPositionLabel->setVisible(true);
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_isShuttingDown) {
    event->accept();
    return;
  }

  // Flush any pending grid-width persistence before shutdown so the final
  // user-adjusted width is not lost when closing immediately after changes.
  if (m_gridWidthSaveDebouncer && m_gridWidthSaveDebouncer->isPending() && getSettingsManager()) {
    m_gridWidthSaveDebouncer->triggerImmediate();
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

  // Persist current viewport/selection state before blocking signals.
  // This ensures the cached viewport is available for fast startup on next
  // launch.
  if (getNavigationManager()) {
    getNavigationManager()->prepareForShutdown();
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

void MainWindow::updateWindowTitleForCollection(int collectionIndex) {
  if (collectionIndex >= 0 && collectionIndex < m_collections.size()) {
    setWindowTitle(m_collections[collectionIndex].name);

    // Sync view type button states
    ViewType viewType = m_collections[collectionIndex].viewType;
    if (m_gridViewButton) {
      m_gridViewButton->setChecked(viewType == ViewType::Grid);
    }
    if (m_listViewButton) {
      m_listViewButton->setChecked(viewType == ViewType::List);
    }
  }
}

void MainWindow::rebuildHierarchyCache() {
  m_hierarchyCache.rebuild(m_collections);
}

// Delegated Getters
SidebarManager *MainWindow::getSidebarManager() const {
  return m_appManager->getSidebarManager();
}
SettingsManager *MainWindow::getSettingsManager() const {
  return m_appManager->getSettingsManager();
}
DatabaseManager *MainWindow::getDatabaseManager() const {
  return m_appManager->getDatabaseManager();
}
ScrollManager *MainWindow::getScrollManager() const {
  return m_appManager->getScrollManager();
}
NavigationManager *MainWindow::getNavigationManager() const {
  return m_appManager->getNavigationManager();
}
InteractionManager *MainWindow::getInteractionManager() const {
  return m_appManager->getInteractionManager();
}
SessionManager *MainWindow::getSessionManager() const {
  return m_appManager->getSessionManager();
}
ArtworkManager *MainWindow::getArtworkManager() const {
  return m_appManager->getArtworkManager();
}
CacheManager *MainWindow::getCacheManager() const {
  return m_appManager->getCacheManager();
}
