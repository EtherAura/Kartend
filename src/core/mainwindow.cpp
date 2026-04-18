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
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "menucontroller.h"
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

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcMainWindow, "kartend.mainwindow")

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), stackedWidget(nullptr),
      itemsPage(nullptr), gridContainer(nullptr), m_mainContentWidget(nullptr),
      itemGrid(nullptr), m_mainHorizontalLayout(nullptr), searchBar(nullptr),
      m_searchModeButton(nullptr), loadingLabel(nullptr),
      currentCollectionIndex(-1), m_MetadataSidebar(nullptr) {
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
  QTimer::singleShot(
      UIConstants::Timing::RESIZE_RECENTER_DELAY_MS, this, [this]() {
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
    if (!getSessionManager()->getCollectionCounts(
            m_collections[collectionIndex], m_collections, direct, recursive)) {
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
      counts = QString("(%1 Items)")
                   .arg(StringUtils::formatCountNumber(subfolderItemCount));
    }

    int directSubfolderCount =
        CollectionUtils::countVirtualFolders(m_collections[cur]);
    int directSubcollectionCount =
        CollectionUtils::directChildrenOf(cur, m_collections).size();

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
  const int viewTotalItems =
      getScrollManager() ? getScrollManager()->getTotalItems() : -1;

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

  int directSubfolderCount =
      CollectionUtils::countVirtualFolders(m_collections[cur]);
  int directSubcollectionCount =
      CollectionUtils::directChildrenOf(cur, m_collections).size();

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
  m_appContext.interactionState = &getInteractionManager()->state();

  // Set up InteractionManager (its sub-managers will now get valid
  // interactionState)
  getInteractionManager()->setupReferences(setup);

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

void MainWindow::connectDatabaseManager() {
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemsLoaded,
                   getNavigationManager(), &NavigationManager::onItemsLoaded);
  QObject::connect(
      getDatabaseManager(), &DatabaseManager::itemCountLoadedWithToken,
      getNavigationManager(), &NavigationManager::onItemCountLoaded);
  QObject::connect(getDatabaseManager(),
                   &DatabaseManager::collectionScanCompleted,
                   getNavigationManager(),
                   &NavigationManager::onBackgroundCollectionScanCompleted);
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemCountLoaded,
                   this, [this](int) {
                     // If the initial load did not start a scan, we can
                     // re-enable scan overlays immediately. If a startup scan
                     // is in-flight, keep overlays suppressed until it
                     // completes.
                     if (m_suppressStartupScanOverlays &&
                         m_startupActiveScanCount == 0) {
                       m_suppressStartupScanOverlays = false;
                     }
                   });
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemsRangeLoaded,
                   getNavigationManager(),
                   &NavigationManager::onItemsRangeLoaded);
  // Connect visual index lookup for selection restore after sort changes
  QObject::connect(
      getDatabaseManager(), &DatabaseManager::visualIndexForPathLoaded,
      getScrollManager(), &ScrollManager::onVisualIndexForPathLoaded);
  QObject::connect(getDatabaseManager(), &DatabaseManager::errorOccurred,
                   getNavigationManager(),
                   &NavigationManager::onMediaLibraryError);

  // Cached counts are now recomputed asynchronously; refresh the title once
  // the update completes so the user sees up-to-date totals.
  QObject::connect(getDatabaseManager(), &DatabaseManager::cachedCountsUpdated,
                   this, [this]() {
                     if (!QApplication::closingDown()) {
                       refreshTitleCounts();
                     }
                   });

  // Update loading overlay with scan progress during initial collection loading
  QObject::connect(
      getDatabaseManager(), &DatabaseManager::scanProgress, this,
      [this](int current, int total, const QString &name) {
        if (m_suppressStartupScanOverlays) {
          // Keep the UI interactive on startup; progress is still
          // visible via the title bar set by scanStarting.
          return;
        }
        if (m_loadingOverlay) {
          if (m_loadingOverlay->isActive()) {
            // Update existing overlay with progress
            m_loadingOverlay->setMessage(QString("Scanning %1...").arg(name));
            m_loadingOverlay->setProgress(current, total);
          } else {
            // Show overlay with initial progress
            m_loadingOverlay->showWithProgress(
                QString("Scanning %1...").arg(name), current, total);
          }
        }
      });

  // Show overlay when a long scan starts (e.g., from fetchItemCount triggering
  // rescan)
  QObject::connect(getDatabaseManager(), &DatabaseManager::scanStarting, this,
                   [this](const QString &name, int estimatedItems) {
                     Q_UNUSED(estimatedItems)
                     // Track active scans so overlay persists until all
                     // complete (e.g., when showAllSubcollectionItems triggers
                     // multiple scans)
                     ++m_activeScanCount;

                     if (m_suppressStartupScanOverlays) {
                       ++m_startupActiveScanCount;
                     }
                     if (!m_suppressStartupScanOverlays) {
                       if (m_loadingOverlay && !m_loadingOverlay->isActive()) {
                         m_loadingOverlay->show(
                             QString("Scanning %1...").arg(name));
                       }
                     }
                     // Show "Scanning..." in title bar instead of "0 items"
                     setWindowTitle(QString("%1 (Scanning...)").arg(name));
                   });

  QObject::connect(getDatabaseManager(),
                   &DatabaseManager::collectionScanCompleted, this,
                   [this](const QString &) {
                     if (!m_suppressStartupScanOverlays) {
                       return;
                     }
                     if (m_startupActiveScanCount > 0) {
                       --m_startupActiveScanCount;
                     }
                     if (m_startupActiveScanCount == 0) {
                       m_suppressStartupScanOverlays = false;
                     }
                   });

  // Hide scan overlay once ALL scans in a batch have completed.
  // When showAllSubcollectionItems triggers scans of many descendants,
  // we only hide the overlay when the last one finishes.
  QObject::connect(
      getDatabaseManager(), &DatabaseManager::collectionScanCompleted, this,
      [this](const QString &) {
        if (m_activeScanCount > 0) {
          --m_activeScanCount;
        }
        // Only hide overlay and reload when all scans are complete
        if (m_activeScanCount == 0) {
          if (m_suppressStartupScanOverlays) {
            return;
          }
          if (m_loadingOverlay && m_loadingOverlay->isActive()) {
            m_loadingOverlay->hide();
          }
          // When showAllSubcollectionItems is enabled and all descendant
          // scans have finished, reload the collection to get accurate counts.
          // NavigationManager skips intermediate reloads while the overlay
          // is active, so we trigger the final reload here after hiding it.
          if (currentCollectionIndex >= 0 &&
              currentCollectionIndex < m_collections.size() &&
              m_collections[currentCollectionIndex].showAllSubcollectionItems) {
            // Use a longer delay (150ms) to ensure the scan worker's
            // commit is visible to the query worker before we request counts.
            QTimer::singleShot(150, this, [this]() {
              if (getNavigationManager()) {
                getNavigationManager()->safeReloadCollection(
                    currentCollectionIndex);
              }
            });
          }
        }
      });

  // Update progress during item scan/save
  QObject::connect(
      getDatabaseManager(), &DatabaseManager::scanItemsProgress, this,
      [this](int itemsProcessed, int totalItems) {
        if (m_suppressStartupScanOverlays) {
          return;
        }
        if (m_loadingOverlay && m_loadingOverlay->isActive()) {
          if (totalItems > 0) {
            // Indexing phase - we know the total
            m_loadingOverlay->setMessage(QString("Indexing %1 of %2 items...")
                                             .arg(itemsProcessed)
                                             .arg(totalItems));
            m_loadingOverlay->setProgress(itemsProcessed, totalItems);
          } else {
            // Scanning phase - total unknown, show count found so far
            m_loadingOverlay->setMessage(
                QString("Scanning... found %1 items").arg(itemsProcessed));
            // Show indeterminate progress (spinner continues)
            m_loadingOverlay->setProgress(0, 0);
          }
        }
      });

  // Rebuild hierarchy cache when collections are modified via settings dialog
  QObject::connect(getSettingsManager(), &SettingsManager::collectionsModified,
                   this, &MainWindow::rebuildHierarchyCache);
}

void MainWindow::connectScrollManager() {
  // Click/double-click handling is done via EventManager, not ItemWidget
  // signals
  QObject::connect(getScrollManager(), &ScrollManager::subcollectionEntered,
                   getNavigationManager(),
                   &NavigationManager::onSubcollectionEntered);
  QObject::connect(getScrollManager(), &ScrollManager::virtualFolderEntered,
                   getNavigationManager(),
                   &NavigationManager::onVirtualFolderEntered);
  QObject::connect(getScrollManager(), &ScrollManager::requestItemsRange,
                   getNavigationManager(), &NavigationManager::fetchItemsRange);
  // List view header column click triggers sort mode change
  QObject::connect(
      getScrollManager(), &ScrollManager::sortModeChangeRequested, this,
      [this](SortMode sortMode) {
        if (!getNavigationManager()) {
          return;
        }
        // Capture currently selected file path to restore after reload
        if (getInteractionManager() && getScrollManager()) {
          QString selectedPath = getInteractionManager()->selectedFilePath();
          if (!selectedPath.isEmpty()) {
            getScrollManager()->setPendingSelectionRestoreByPath(selectedPath);
          }
        }
        m_generalSettings.sortMode = sortMode;
        if (getSettingsManager()) {
          getSettingsManager()->saveGeneralSettings(m_generalSettings);
        }
        getNavigationManager()->safeReloadCollection(currentCollectionIndex);
      });
  // Restore selection by index after sort change finds the item
  QObject::connect(getScrollManager(), &ScrollManager::selectItemByIndex, this,
                   [this](int index) {
                     if (getInteractionManager()) {
                       getInteractionManager()->selectItemByIndex(index, true);
                       // Instantly scroll to make the item visible (no
                       // animation)
                       getInteractionManager()
                           ->applyImmediateViewportPositioningForSelection(
                               index);
                     }
                   });
  // Persist list column width when user resizes
  QObject::connect(getScrollManager(), &ScrollManager::listColumnWidthChanged,
                   this, [this](int width) {
                     m_generalSettings.listCollectionColumnWidth = width;
                     if (getSettingsManager()) {
                       getSettingsManager()->saveGeneralSettings(
                           m_generalSettings);
                     }
                   });
  // Persist list artwork column width when user resizes
  QObject::connect(
      getScrollManager(), &ScrollManager::listArtworkColumnWidthChanged, this,
      [this](int width) {
        m_generalSettings.listArtworkColumnWidth = width;
        if (getSettingsManager()) {
          getSettingsManager()->saveGeneralSettings(m_generalSettings);
        }
      });
  if (getArtworkManager()) {
    QObject::connect(
        getArtworkManager()->getTimerCoordinator(),
        &TimerUtils::Coordinator::viewportUpdateRequested, this, [this]() {
          if (!QApplication::closingDown() && getArtworkManager()) {
            getArtworkManager()->updateViewportArtwork();
          }
        });
    QObject::connect(
        getArtworkManager()->getTimerCoordinator(),
        &TimerUtils::Coordinator::layoutUpdateRequested, this, [this]() {
          if (!QApplication::closingDown() && (getScrollManager())) {
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
        if (visible && (getSidebarManager()) && (getScrollManager()) &&
            (getInteractionManager())) {
          int sel = getInteractionManager()->currentSelectedIndex();
          if (sel >= 0) {
            ItemWidget *widgetPtr =
                getScrollManager()->getActiveWidgets().value(sel, nullptr);
            getSidebarManager()->updateSidebarMetadata(widgetPtr);
          }
        }
        // Delay metrics recalculation to allow sidebar animation to complete
        // before recalculating grid layout dimensions
        QTimer::singleShot(
            UIConstants::Sidebar::METRICS_RECALC_DELAY_MS, this, [this]() {
              if (getScrollManager()) {
                getScrollManager()->recalculateContainerMetrics();
              }
            });
      });

  QObject::connect(
      getSidebarManager(), &SidebarManager::sidebarLayoutChanged, this,
      [this]() {
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

  // Connect view type toggle buttons
  if (m_gridViewButton) {
    QObject::connect(m_gridViewButton, &QPushButton::clicked, this,
                     [this]() { setViewType(ViewType::Grid); });
  }
  if (m_listViewButton) {
    QObject::connect(m_listViewButton, &QPushButton::clicked, this,
                     [this]() { setViewType(ViewType::List); });
  }
}

void MainWindow::connectScrollBars() const {
  if (ui->itemScrollArea) {
    const QScrollBar *vScrollBar = ui->itemScrollArea->verticalScrollBar();
    const QScrollBar *hScrollBar = ui->itemScrollArea->horizontalScrollBar();

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

  // Flush any pending grid-width persistence before shutdown so the final
  // user-adjusted width is not lost when closing immediately after changes.
  if (m_gridWidthSaveDebouncer && m_gridWidthSaveDebouncer->isPending() &&
      getSettingsManager()) {
    m_gridWidthSaveDebouncer->triggerImmediate();
  }

  m_isShuttingDown = true;

  // Hide window immediately so user sees instant visual response
  hide();

  // Remove event filters first to prevent further processing
  if (ui->itemScrollArea) {
    ui->itemScrollArea->removeEventFilter(getInteractionManager());
    if (ui->itemScrollArea->viewport()) {
      ui->itemScrollArea->viewport()->removeEventFilter(
          getInteractionManager());
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

  setupUIReferences();

  // Debounced persistence + refresh for menu-driven grid width changes.
  // This avoids writing settings repeatedly while the user holds +/-.
  if (!m_gridWidthSaveDebouncer) {
    m_gridWidthSaveDebouncer = new TimerUtils::DebouncedTimer(
        UIConstants::Timing::LONG_DELAY_MS, this);
    QObject::connect(m_gridWidthSaveDebouncer,
                     &TimerUtils::DebouncedTimer::triggered, this, [this]() {
                       if (m_isShuttingDown || QApplication::closingDown()) {
                         return;
                       }
                       if (getSettingsManager()) {
                         getSettingsManager()->saveCollections(m_collections);
                       }
                     });
  }

  if (!m_gridWidthPrecalcDebouncer) {
    m_gridWidthPrecalcDebouncer = new TimerUtils::DebouncedTimer(
        UIConstants::Timing::LONG_DELAY_MS, this);
    QObject::connect(m_gridWidthPrecalcDebouncer,
                     &TimerUtils::DebouncedTimer::triggered, this, [this]() {
                       if (m_isShuttingDown || QApplication::closingDown()) {
                         return;
                       }
                       if (!getScrollManager()) {
                         return;
                       }

                       // Mark this generation as the active one for the final
                       // stage.
                       m_gridWidthActiveGeneration =
                           m_gridWidthPendingGeneration;

                       getScrollManager()->preCalculateLayout();
                       getScrollManager()->forceVirtualViewUpdate();

                       if (m_gridWidthFinalizeDebouncer) {
                         m_gridWidthFinalizeDebouncer->trigger();
                       }
                     });
  }

  if (!m_gridWidthFinalizeDebouncer) {
    m_gridWidthFinalizeDebouncer = new TimerUtils::DebouncedTimer(
        UIConstants::Timing::MEDIUM_DELAY_MS, this);
    QObject::connect(
        m_gridWidthFinalizeDebouncer, &TimerUtils::DebouncedTimer::triggered,
        this, [this]() {
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
          getScrollManager()->centerHorizontalScrollbar(currentCollectionIndex,
                                                        m_collections);
        });
  }

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
  m_gridViewButton = ui->gridViewButton;
  m_listViewButton = ui->listViewButton;
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
  m_menuController = std::make_unique<MenuController>(this);

  MenuControllerContext ctx;
  ctx.mainWindow = this;
  ctx.ui = ui;
  ctx.getNavigationManager = [this]() { return getNavigationManager(); };
  ctx.getSettingsManager = [this]() { return getSettingsManager(); };
  ctx.getSidebarManager = [this]() { return getSidebarManager(); };
  ctx.getScrollManager = [this]() { return getScrollManager(); };
  ctx.getArtworkManager = [this]() { return getArtworkManager(); };
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
      getSettingsManager()->openSettingsDialog(context);
    }
  };
  ctx.onShowAbout = [this]() { showAbout(); };
  ctx.onAdjustGridWidth = [this](int delta) { adjustGridWidth(delta); };

  m_menuController->setContext(ctx);
  m_menuController->setupMenuBar();
}

void MainWindow::adjustGridWidth(int delta) {
  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= m_collections.size()) {
    return;
  }

  CollectionConfig &config = m_collections[currentCollectionIndex];
  int newWidth = config.gridWidth + delta;

  // Clamp to valid range
  newWidth = qBound(UIConstants::Grid::MIN_WIDTH, newWidth,
                    UIConstants::Grid::MAX_WIDTH);

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
  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= m_collections.size()) {
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

    getSidebarManager()->setupReferences(setup);

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
  if (!getSessionManager())
    return;

  for (int i = 0; i < m_collections.size(); ++i) {
    int sel = getSessionManager()->getLastSelectedIndex(m_collections[i].name);
    if (sel < 0) {
      QString hierarchical =
          CollectionUtils::hierarchicalNameFor(m_collections[i], m_collections);
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
  setup.generalSettings = &m_generalSettings;
  setup.artworkManager = getArtworkManager();
  setup.gridContainer = gridContainer;
  setup.mediaScrollArea = ui->itemScrollArea;
  setup.collections = &m_collections;
  setup.hierarchyCache = &m_hierarchyCache;

  getScrollManager()->setupReferences(setup);
  getScrollManager()->setDatabaseManager(getDatabaseManager());

  if (ui->itemScrollArea) {
    ui->itemScrollArea->installEventFilter(getInteractionManager());
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
  QTimer::singleShot(UIConstants::Sidebar::INITIAL_CENTER_SCROLL_DELAY_MS, this,
                     [this]() {
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
  // Defer collection creation until after the main window is fully shown -
  // ensures proper parent-child relationship and window stacking order
  QTimer::singleShot(0, this, [this]() {
    // Prompt user to create their first collection
    bool ok = false;
    QString name =
        QInputDialog::getText(this, tr("Create First Collection"),
                              tr("Enter a name for your first collection:"),
                              QLineEdit::Normal, "", &ok);

    if (!ok || name.trimmed().isEmpty()) {
      // User cancelled - show message and close
      QMessageBox::information(
          this, tr("No Collection Created"),
          tr("Kartend requires at least one collection to function. "
             "Please restart the application to try again."));
      return;
    }

    // Create the first collection with the given name
    CollectionConfig newCollection;
    newCollection.name = name.trimmed();
    newCollection.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
    newCollection.parentCollectionIndex = -1;
    newCollection.isSubcollection = false;
    m_collections.append(newCollection);

    // Save the new collection
    if (getSettingsManager()) {
      getSettingsManager()->saveCollections(m_collections);
    }

    // Rebuild hierarchy cache with the new collection
    rebuildHierarchyCache();

    // Now open settings dialog for the user to configure the collection
    if (getSettingsManager()) {
      currentCollectionIndex = 0;
      SettingsDialogContext context;
      context.parent = this;
      context.collections = &m_collections;
      context.currentCollectionIndex = &currentCollectionIndex;
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
        // The initial fetchItemCount path may trigger a rescan; keep the UI
        // navigable by suppressing any blocking overlay for this startup scan.
        m_suppressStartupScanOverlays = true;
        getNavigationManager()->showCollectionItems(rootIndex);
      }
    }
  });
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