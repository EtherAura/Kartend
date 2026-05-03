// Signal/slot wiring for MainWindow's owned managers.
// Extracted from mainwindow.cpp to keep that file focused on lifecycle and UI
// setup. All functions here remain MainWindow members.
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>

#include "artworkmanager.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

void MainWindow::connectDatabaseManager() {
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemsLoaded, getNavigationManager(),
                   &NavigationManager::onItemsLoaded);
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemCountLoadedWithToken,
                   getNavigationManager(), &NavigationManager::onItemCountLoaded);
  QObject::connect(getDatabaseManager(), &DatabaseManager::collectionScanCompleted,
                   getNavigationManager(), &NavigationManager::onBackgroundCollectionScanCompleted);
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemCountLoaded, this, [this](int) {
    // If the initial load did not start a scan, we can
    // re-enable scan overlays immediately. If a startup scan
    // is in-flight, keep overlays suppressed until it
    // completes.
    if (m_suppressStartupScanOverlays && m_startupActiveScanCount == 0) {
      m_suppressStartupScanOverlays = false;
    }
  });
  QObject::connect(getDatabaseManager(), &DatabaseManager::itemsRangeLoaded, getNavigationManager(),
                   &NavigationManager::onItemsRangeLoaded);
  // Connect visual index lookup for selection restore after sort changes
  QObject::connect(getDatabaseManager(), &DatabaseManager::visualIndexForPathLoaded,
                   getScrollManager(), &ScrollManager::onVisualIndexForPathLoaded);
  QObject::connect(getDatabaseManager(), &DatabaseManager::errorOccurred, getNavigationManager(),
                   &NavigationManager::onMediaLibraryError);

  // Cached counts are now recomputed asynchronously; refresh the title once
  // the update completes so the user sees up-to-date totals.
  QObject::connect(getDatabaseManager(), &DatabaseManager::cachedCountsUpdated, this, [this]() {
    if (!QApplication::closingDown()) {
      refreshTitleCounts();
    }
  });

  // Update loading overlay with scan progress during initial collection loading
  QObject::connect(getDatabaseManager(), &DatabaseManager::scanProgress, this,
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
                         m_loadingOverlay->showWithProgress(QString("Scanning %1...").arg(name),
                                                            current, total);
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
                         m_loadingOverlay->show(QString("Scanning %1...").arg(name));
                       }
                     }
                     // Show "Scanning..." in title bar instead of "0 items"
                     setWindowTitle(QString("%1 (Scanning...)").arg(name));
                   });

  QObject::connect(getDatabaseManager(), &DatabaseManager::collectionScanCompleted, this,
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
  QObject::connect(getDatabaseManager(), &DatabaseManager::collectionScanCompleted, this,
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
                             getNavigationManager()->safeReloadCollection(currentCollectionIndex);
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
            m_loadingOverlay->setMessage(
                QString("Indexing %1 of %2 items...").arg(itemsProcessed).arg(totalItems));
            m_loadingOverlay->setProgress(itemsProcessed, totalItems);
          } else {
            // Scanning phase - total unknown, show count found so far
            m_loadingOverlay->setMessage(QString("Scanning... found %1 items").arg(itemsProcessed));
            // Show indeterminate progress (spinner continues)
            m_loadingOverlay->setProgress(0, 0);
          }
        }
      });

  // Rebuild hierarchy cache when collections are modified via settings dialog
  QObject::connect(getSettingsManager(), &SettingsManager::collectionsModified, this,
                   &MainWindow::rebuildHierarchyCache);
}

void MainWindow::connectScrollManager() {
  // Click/double-click handling is done via EventManager, not ItemWidget
  // signals
  QObject::connect(getScrollManager(), &ScrollManager::subcollectionEntered, getNavigationManager(),
                   &NavigationManager::onSubcollectionEntered);
  QObject::connect(getScrollManager(), &ScrollManager::virtualFolderEntered, getNavigationManager(),
                   &NavigationManager::onVirtualFolderEntered);
  QObject::connect(getScrollManager(), &ScrollManager::requestItemsRange, getNavigationManager(),
                   &NavigationManager::fetchItemsRange);
  // Expand-mode: second activation in artwork preview overlay launches.
  if (getInteractionManager()) {
    QObject::connect(getScrollManager(), &ScrollManager::artworkPreviewLaunchRequested,
                     getInteractionManager(), &InteractionManager::onArtworkPreviewLaunchRequested);
  }
  // List view header column click triggers sort mode change
  QObject::connect(getScrollManager(), &ScrollManager::sortModeChangeRequested, this,
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
  QObject::connect(getScrollManager(), &ScrollManager::selectItemByIndex, this, [this](int index) {
    if (getInteractionManager()) {
      getInteractionManager()->selectItemByIndex(index, true);
      // Instantly scroll to make the item visible (no
      // animation)
      getInteractionManager()->applyImmediateViewportPositioningForSelection(index);
    }
  });
  // Persist list column width when user resizes
  QObject::connect(getScrollManager(), &ScrollManager::listColumnWidthChanged, this,
                   [this](int width) {
                     m_generalSettings.listCollectionColumnWidth = width;
                     if (getSettingsManager()) {
                       getSettingsManager()->saveGeneralSettings(m_generalSettings);
                     }
                   });
  // Persist list artwork column width when user resizes
  QObject::connect(getScrollManager(), &ScrollManager::listArtworkColumnWidthChanged, this,
                   [this](int width) {
                     m_generalSettings.listArtworkColumnWidth = width;
                     if (getSettingsManager()) {
                       getSettingsManager()->saveGeneralSettings(m_generalSettings);
                     }
                   });
  if (getArtworkManager()) {
    QObject::connect(getArtworkManager()->getTimerCoordinator(),
                     &TimerUtils::Coordinator::viewportUpdateRequested, this, [this]() {
                       if (!QApplication::closingDown() && getArtworkManager()) {
                         getArtworkManager()->updateViewportArtwork();
                       }
                     });
    QObject::connect(getArtworkManager()->getTimerCoordinator(),
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

  // Kartend-tof: refresh the top-bar "pos / total" label on selection and
  // collection-size transitions. The filterChanged connection above already
  // calls updateItemPositionLabel via updateWindowTitleWithFilter; here we
  // also update on direct selection moves.
  if (getInteractionManager()) {
    QObject::connect(getInteractionManager(), &InteractionManager::selectionChanged, this,
                     [this](int) {
                       if (!QApplication::closingDown()) {
                         updateItemPositionLabel();
                       }
                     });
  }
}

void MainWindow::connectSidebarManager() {
  QObject::connect(
      getSidebarManager(), &SidebarManager::sidebarVisibilityChanged, this, [this](bool visible) {
        if (visible && (getSidebarManager()) && (getScrollManager()) && (getInteractionManager())) {
          int sel = getInteractionManager()->currentSelectedIndex();
          if (sel >= 0) {
            ItemWidget *widgetPtr = getScrollManager()->getActiveWidgets().value(sel, nullptr);
            getSidebarManager()->updateSidebarMetadata(widgetPtr);
          }
        }
        // Delay metrics recalculation to allow sidebar animation to complete
        // before recalculating grid layout dimensions
        QTimer::singleShot(UIConstants::Sidebar::METRICS_RECALC_DELAY_MS, this, [this]() {
          if (getScrollManager()) {
            getScrollManager()->recalculateContainerMetrics();
          }
        });
      });

  QObject::connect(getSidebarManager(), &SidebarManager::sidebarLayoutChanged, this, [this]() {
    if (getScrollManager()) {
      getScrollManager()->recalculateContainerMetrics();
    }
    if ((getSidebarManager()) && (getScrollManager()) && (getInteractionManager()) &&
        getSidebarManager()->isSidebarVisible()) {
      int sel = getInteractionManager()->currentSelectedIndex();
      if (sel >= 0) {
        ItemWidget *widgetPtr = getScrollManager()->getActiveWidgets().value(sel, nullptr);
        getSidebarManager()->updateSidebarMetadata(widgetPtr);
      }
    }
  });
}

void MainWindow::connectSearchComponents() {
  if ((m_searchModeButton) && (getInteractionManager())) {
    QObject::connect(m_searchModeButton, &QPushButton::clicked, getInteractionManager(),
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

void MainWindow::refreshTypeFilterToolbar() {
  if (!m_typeFilterComboBox) {
    return;
  }
  // Block the activated signal so rebuilding the dropdown doesn't trigger a
  // redundant reload — the active filter is preserved by re-selecting it
  // after the rebuild.
  QSignalBlocker blocker(m_typeFilterComboBox);
  const QString previous = m_generalSettings.collectionTypeFilter;
  m_typeFilterComboBox->clear();
  // Sentinel "<All types>" maps to an empty filter string. Stored as
  // userData so the activated-handler can distinguish it from a user-typed
  // value with the same display text.
  m_typeFilterComboBox->addItem(tr("<All types>"), QString());
  for (const QString &type : CollectionUtils::collectAllCollectionTypes(m_collections)) {
    m_typeFilterComboBox->addItem(type, type);
  }
  // Re-apply the previously active filter. If the type the user had picked
  // no longer exists (collection deleted/retagged), fall back to <All types>
  // and clear the persisted filter so the toolbar reflects reality.
  int idx = previous.isEmpty() ? 0 : m_typeFilterComboBox->findData(previous);
  if (idx < 0) {
    idx = 0;
    m_generalSettings.collectionTypeFilter.clear();
    if (getSettingsManager()) {
      getSettingsManager()->saveGeneralSettings(m_generalSettings);
    }
  }
  m_typeFilterComboBox->setCurrentIndex(idx);
}

void MainWindow::connectCollectionTypeToolbar() {
  // Initial state mirrors the persisted GeneralSettings so a fresh launch
  // restores the user's last filter / hide-subs choice.
  if (m_hideSubcollectionsButton) {
    QSignalBlocker blocker(m_hideSubcollectionsButton);
    m_hideSubcollectionsButton->setChecked(m_generalSettings.hideSubcollectionTiles);
  }
  refreshTypeFilterToolbar();

  if (m_hideSubcollectionsButton) {
    QObject::connect(m_hideSubcollectionsButton, &QPushButton::toggled, this, [this](bool checked) {
      if (m_generalSettings.hideSubcollectionTiles == checked) {
        return;
      }
      m_generalSettings.hideSubcollectionTiles = checked;
      if (getSettingsManager()) {
        getSettingsManager()->saveGeneralSettings(m_generalSettings);
      }
      if (getNavigationManager() && currentCollectionIndex >= 0) {
        getNavigationManager()->safeReloadCollection(currentCollectionIndex);
      }
    });
  }

  if (m_typeFilterComboBox) {
    QObject::connect(m_typeFilterComboBox, QOverload<int>::of(&QComboBox::activated), this,
                     [this](int index) {
                       if (!m_typeFilterComboBox) {
                         return;
                       }
                       QString chosen = m_typeFilterComboBox->itemData(index).toString();
                       if (m_generalSettings.collectionTypeFilter == chosen) {
                         return;
                       }
                       m_generalSettings.collectionTypeFilter = chosen;
                       if (getSettingsManager()) {
                         getSettingsManager()->saveGeneralSettings(m_generalSettings);
                       }
                       if (getNavigationManager() && currentCollectionIndex >= 0) {
                         getNavigationManager()->safeReloadCollection(currentCollectionIndex);
                       }
                     });
  }
}

void MainWindow::connectScrollBars() const {
  if (ui->itemScrollArea) {
    const QScrollBar *vScrollBar = ui->itemScrollArea->verticalScrollBar();
    const QScrollBar *hScrollBar = ui->itemScrollArea->horizontalScrollBar();

    if ((vScrollBar) && (getNavigationManager())) {
      QObject::connect(vScrollBar, &QScrollBar::valueChanged, getNavigationManager(),
                       &NavigationManager::onViewportChanged);
    }
    if ((hScrollBar) && (getNavigationManager())) {
      QObject::connect(hScrollBar, &QScrollBar::valueChanged, getNavigationManager(),
                       &NavigationManager::onViewportChanged);
    }
  }
}
