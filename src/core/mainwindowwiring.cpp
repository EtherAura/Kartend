// Signal/slot wiring for MainWindow's owned managers.
// Extracted from mainwindow.cpp to keep that file focused on lifecycle and UI
// setup. All functions here remain MainWindow members.
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailspanemanager.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "toolbarcontroller.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "settingsmanager.h"
#include "timerutils.h"
#include "titlefilter.h"
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

  // Refresh the consolidated filter popup each time a collection's items
  // finish loading. NavigationManager updates currentCollectionIndex through
  // a raw pointer (no Qt signal), so this is the most reliable post-switch
  // hook available — by the time itemsLoaded fires, the index points at the
  // collection the user is now viewing, and the popup's title-pattern toggle
  // needs to mirror that collection's flag.
  QObject::connect(
      getDatabaseManager(), &DatabaseManager::itemsLoaded, this,
      [this](const QStringList &, const QHash<QString, QString> &) { refreshFilterToolbar(); });

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

  // Keep the sidebar's no-selection collection summary fresh after scans
  // finish or settings edits land. cachedCountsUpdated covers
  // item-count changes that arrive without a full scan (e.g., manual deletes).
  if (getDetailsPaneManager()) {
    QObject::connect(
        getDatabaseManager(), &DatabaseManager::collectionScanCompleted, getDetailsPaneManager(),
        [this](const QString &) { getDetailsPaneManager()->refreshCollectionSummary(); });
    QObject::connect(getDatabaseManager(), &DatabaseManager::cachedCountsUpdated,
                     getDetailsPaneManager(), &DetailsPaneManager::refreshCollectionSummary);
    QObject::connect(getSettingsManager(), &SettingsManager::collectionsModified,
                     getDetailsPaneManager(), &DetailsPaneManager::refreshCollectionSummary);
  }
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
  // yield sidebar viewport space when entering CoverFlow,
  // restore the persisted per-collection state when leaving.
  QObject::connect(getScrollManager(), &ScrollManager::coverFlowActiveChanged, this,
                   [this](bool active) {
                     if (getDetailsPaneManager()) {
                       getDetailsPaneManager()->setExternallyHidden(active);
                     }
                   });
  // bug #7: lower the sidebar while the artwork preview overlay
  // is showing so the overlay (parented to the top-level window) stays on
  // top. Restored on hide.
  QObject::connect(getScrollManager(), &ScrollManager::artworkPreviewVisibilityChanged, this,
                   [this](bool visible) {
                     if (getDetailsPaneManager()) {
                       getDetailsPaneManager()->setOverlayActive(visible);
                     }
                   });
  // cover-flow activates a card → land selection on it then
  // route through the existing launch path. Subcollection / virtual-folder
  // activations are handled by ScrollManager itself via subcollectionEntered
  // / virtualFolderEntered above so they don't reach this slot. The owning
  // collection comes from DatabaseManager — the launcher / core / params
  // are configured per-collection and using currentCollectionIndex would
  // mis-launch any item inherited from a subcollection in
  // showAllSubcollectionItems mode (surfaces as "no launcher configured").
  QObject::connect(
      getScrollManager(), &ScrollManager::coverFlowItemActivated, this, [this](int index) {
        if (!getInteractionManager() || !getScrollManager()) {
          return;
        }
        getInteractionManager()->selectItemByIndex(index, true);
        const QString filePath = getScrollManager()->filePathForVisualIndex(index);
        if (filePath.isEmpty()) {
          return;
        }
        int ownerIdx = currentCollectionIndex;
        if (getDatabaseManager()) {
          const int detected = getDatabaseManager()->getCollectionIndexForFile(filePath);
          if (detected >= 0) {
            ownerIdx = detected;
          }
        }
        getInteractionManager()->launchItemWithCollection(filePath, ownerIdx);
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

  // refresh the top-bar "pos / total" label on selection and
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
      getDetailsPaneManager(), &DetailsPaneManager::sidebarVisibilityChanged, this,
      [this](bool visible) {
        if (visible && (getDetailsPaneManager()) && (getScrollManager()) &&
            (getInteractionManager())) {
          int sel = getInteractionManager()->currentSelectedIndex();
          if (sel >= 0) {
            ItemWidget *widgetPtr = getScrollManager()->getActiveWidgets().value(sel, nullptr);
            getDetailsPaneManager()->updateSidebarMetadata(widgetPtr);
          }
        }
        // push the "sidebar hidden AND would shrink" predicate
        // into ScrollManager so the upcoming metrics recompute picks the right
        // gridWidth / horizontalGridHeight pair. Overlay mode never shrinks
        // (the sidebar floats), so it stays on the primary values.
        updateScrollManagerSidebarShrinking();
        // Delay metrics recalculation to allow sidebar animation to complete
        // before recalculating grid layout dimensions
        QTimer::singleShot(UIConstants::DetailsPane::METRICS_RECALC_DELAY_MS, this, [this]() {
          if (getScrollManager()) {
            getScrollManager()->recalculateContainerMetrics();
          }
        });
      });

  QObject::connect(
      getDetailsPaneManager(), &DetailsPaneManager::sidebarLayoutChanged, this, [this]() {
        if (getScrollManager()) {
          getScrollManager()->recalculateContainerMetrics();
        }
        if ((getDetailsPaneManager()) && (getScrollManager()) && (getInteractionManager()) &&
            getDetailsPaneManager()->isSidebarVisible()) {
          int sel = getInteractionManager()->currentSelectedIndex();
          if (sel >= 0) {
            ItemWidget *widgetPtr = getScrollManager()->getActiveWidgets().value(sel, nullptr);
            getDetailsPaneManager()->updateSidebarMetadata(widgetPtr);
          }
        }
      });
}

void MainWindow::updateScrollManagerSidebarShrinking() {
  if (!getScrollManager() || !getDetailsPaneManager()) {
    return;
  }
  // Use DetailsPaneManager's tracked index rather than MainWindow::currentCollectionIndex
  // because this can be called from inside applySidebarStateForCollection (via
  // its sidebarVisibilityChanged emission) before MainWindow has caught up to
  // the new index after a collection switch.
  const int idx = getDetailsPaneManager()->currentCollectionIndex();
  if (idx < 0 || idx >= m_collections.size()) {
    getScrollManager()->setSidebarShrinkingActive(false);
    return;
  }
  const CollectionConfig &collection = m_collections[idx];
  // Overlay mode never shrinks the grid (the sidebar floats over content), so
  // the alt gridWidth only applies in Expand mode while the sidebar is hidden.
  if (collection.sidebarMode != DetailsPaneMode::Expand) {
    getScrollManager()->setSidebarShrinkingActive(false);
    return;
  }
  const bool sidebarHidden = !getDetailsPaneManager()->isSidebarVisible();
  // The pane only shrinks the grid along its own dock axis. A vertical-axis
  // dock (Top/Bottom) takes height, so vertical-scrolling views (Grid/List/
  // CoverFlow) — whose layout dimension is items-per-row, i.e. horizontal —
  // are unaffected by it. Symmetrically, an L/R dock leaves Horizontal view's
  // items-per-column dimension untouched. When the pane doesn't reduce the
  // relevant axis at all, the alt-when-hidden value should stay in effect
  // even while the pane is visible — toggling it never reclaimed any space
  // along that axis, so the user's "more items" value isn't a hidden-only
  // arrangement, it's the only correct one for that combo.
  const bool paneIsHorizontalDock =
      CollectionUtils::isDetailsPaneHorizontal(collection.sidebarPosition);
  const bool relevantAxisIsHorizontal = (collection.viewType != ViewType::Horizontal);
  const bool paneAffectsRelevantAxis =
      relevantAxisIsHorizontal ? !paneIsHorizontalDock : paneIsHorizontalDock;
  getScrollManager()->setSidebarShrinkingActive(sidebarHidden || !paneAffectsRelevantAxis);
}

void MainWindow::connectSearchComponents() {
  // Search-mode toggle lives inside the QLineEdit as a QAction owned by the
  // ToolbarController. Wire it to InteractionManager's existing toggle slot
  // so the cycling behavior remains identical to the legacy QPushButton.
  if (m_toolbarController && m_toolbarController->searchModeAction() && getInteractionManager()) {
    QObject::connect(m_toolbarController->searchModeAction(), &QAction::triggered,
                     getInteractionManager(), &InteractionManager::toggleSearchMode);
  }
}

void MainWindow::refreshFilterToolbar() {
  if (m_toolbarController) {
    m_toolbarController->refreshFilterToolbar();
  }
}

void MainWindow::connectFilterToolbar() {
  if (m_toolbarController) {
    m_toolbarController->connectFilterToolbar();
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
