// Signal/slot wiring for MainWindow's owned managers.
//
// This file is the single source of truth for how managers communicate at
// runtime. Connections are organized as a flat table per sender — every
// QObject::connect() call uses a member-function pointer (no inline lambdas)
// so the manager graph can be read top-to-bottom and grep'd mechanically.
//
// Convention: high-frequency signals (DatabaseManager::itemsLoaded /
// itemsRangeLoaded, ScrollManager scroll/sort/filter signals, ArtworkManager
// TimerUtils::Coordinator viewport/layout signals) use Qt::UniqueConnection
// so that if a `connect*Manager()` ever runs a second time (dynamic
// reconfiguration, test re-init), the duplicate connection is silently
// skipped instead of firing the slot twice per emit. New connects to these
// senders should follow the same pattern.
//
// The slot handlers that those connections target live in companion TUs
// extracted by responsibility:
//
//   * mainwindow_dbevents.cpp     — DatabaseManager scan/count handlers
//                                    (releaseStartupOverlaySuppressionIfIdle,
//                                    refreshTitleCountsIfActive, onScan*,
//                                    onCollectionScanCompleted*,
//                                    refreshCollectionSummaryOnScanCompleted,
//                                    refreshFilterToolbarOnItemsLoaded)
//   * ScrollEventsController      — ScrollManager view-mode / column-resize /
//                                    CoverFlow event handlers
//                                    (onSortModeChangeRequested,
//                                    onSelectItemByIndex,
//                                    onCoverFlow*, onArtworkPreviewVisibilityChanged,
//                                    onList*ColumnWidthChanged).
//                                    Extracted from MainWindow per Kartend-hzef;
//                                    lives in scrolleventscontroller.{h,cpp}.
//   * mainwindow_scraper.cpp      — scraper-service lifecycle entry points
//                                    (openScraperDialog,
//                                    promptResumePendingScrapeIfAny) and
//                                    marquee shims (applyMarqueeSettings,
//                                    updateMarqueeArtwork)
//
// This TU keeps:
//   - the connect*() tables (the flat manager graph)
//   - the small artwork / sidebar / selection-driven slot handlers that
//     are too small to justify their own file and naturally read alongside
//     their wiring
//   - updateScrollManagerSidebarShrinking + refreshFilterToolbar +
//     connectFilterToolbar — the wiring-adjacent setup helpers that
//     existing call sites still expect to find in this TU.
//
// Manager graph (sender → receiver edges):
//
//   DatabaseManager → NavigationManager
//     itemsLoaded               → onItemsLoaded
//     itemCountLoadedWithToken  → onItemCountLoaded
//     collectionScanCompleted   → onBackgroundCollectionScanCompleted
//     itemsRangeLoaded          → onItemsRangeLoaded
//     errorOccurred             → onMediaLibraryError
//   DatabaseManager → ScrollManager
//     visualIndexForPathLoaded  → onVisualIndexForPathLoaded
//   DatabaseManager → MainWindow (UI/title/overlay state — handlers in mainwindow_dbevents.cpp)
//     itemCountLoaded           → releaseStartupOverlaySuppressionIfIdle
//     cachedCountsUpdated       → refreshTitleCountsIfActive
//     itemsLoaded               → refreshFilterToolbarOnItemsLoaded
//     scanProgress              → onScanProgress
//     scanStarting              → onScanStarting
//     collectionScanCompleted   → onCollectionScanCompletedStartup
//     collectionScanCompleted   → onCollectionScanCompletedOverlay
//     scanItemsProgress         → onScanItemsProgress
//   DatabaseManager → DetailsPaneManager (collection summary refresh)
//     collectionScanCompleted   → refreshCollectionSummaryOnScanCompleted
//     cachedCountsUpdated       → refreshCollectionSummary
//
//   SettingsManager → MainWindow / DetailsPaneManager
//     collectionsModified       → rebuildHierarchyCache
//     collectionsModified       → refreshCollectionSummary
//
//   ScrollManager → NavigationManager
//     subcollectionEntered      → onSubcollectionEntered
//     virtualFolderEntered      → onVirtualFolderEntered
//     requestItemsRange         → fetchItemsRange
//   ScrollManager → InteractionManager
//     artworkPreviewLaunchRequested → onArtworkPreviewLaunchRequested
//   ScrollManager → ScrollEventsController (Kartend-hzef extraction)
//     sortModeChangeRequested   → onSortModeChangeRequested
//     selectItemByIndex         → onSelectItemByIndex
//     coverFlowActiveChanged    → onCoverFlowActiveChanged
//     artworkPreviewVisibilityChanged → onArtworkPreviewVisibilityChanged
//     coverFlowItemActivated    → onCoverFlowItemActivated
//     listColumnWidthChanged    → onListColumnWidthChanged
//     listArtworkColumnWidthChanged → onListArtworkColumnWidthChanged
//   ScrollManager → MainWindow
//     filterChanged             → onScrollFilterChanged
//
//   ArtworkManager::TimerCoordinator → MainWindow
//     viewportUpdateRequested   → onArtworkViewportUpdateRequested
//     layoutUpdateRequested     → onArtworkLayoutUpdateRequested
//
//   InteractionManager → MainWindow
//     selectionChanged          → onInteractionSelectionChanged
//
//   DetailsPaneManager → MainWindow
//     sidebarVisibilityChanged  → onSidebarVisibilityChanged
//     sidebarLayoutChanged      → onSidebarLayoutChanged
//
//   ToolbarController → InteractionManager
//     searchModeAction.triggered → toggleSearchMode
//
//   QScrollBar (item area V/H) → NavigationManager
//     valueChanged              → onViewportChanged
//
// =====================================================================
// Organizational contract (watch item — Kartend-dkgr).
// =====================================================================
//
// 1. **Emitter-keyed**, not receiver-keyed. Each connect*() function below
//    groups every outgoing connection from one sender. Adding a new edge
//    means locating the sender's block, not the receiver's. The manager
//    graph above mirrors this grouping so the file and the doc stay in
//    sync mechanically.
//
// 2. **Member-function-pointer slots only.** No inline lambdas in this TU.
//    Every connect() resolves to a named MainWindow::on*() / refresh*() /
//    apply*() member so the manager graph above stays grep'able and so
//    new edges can't smuggle behaviour into the wiring file.
//
// 3. **Slot bodies live elsewhere when they outgrow one screen.** The
//    extracted TUs (mainwindow_dbevents.cpp, mainwindow_scraper.cpp) and
//    the extracted Controllers (ScrollEventsController per Kartend-hzef)
//    own the larger responsibility-segmented handlers; the small
//    wiring-adjacent slots that read naturally next to their connect()
//    stay here. When a new slot would push this TU's inline-handler
//    section over ~150 LOC, extract a sibling TU keyed by its emitter
//    (e.g. mainwindow_artworkevents.cpp) or — preferred for new work —
//    a sibling Controller class under src/core/.
//
// 4. **Growth ceiling: ~800 LOC.** Above that, split the connect() tables
//    by emitter family:
//       * mainwindow_wiring_data.cpp  — DatabaseManager, CacheManager,
//         SessionManager, SettingsManager, PlaylistManager, KartManager,
//         DetailPageManager emissions
//       * mainwindow_wiring_input.cpp — NavigationManager, ScrollManager,
//         InteractionManager, ArtworkManager::TimerCoordinator,
//         DetailsPaneManager, ToolbarController emissions
//    Keep the manager-graph doc in this preamble even after the split so
//    there's still one place to read the full edge list.

#include <QAction>
#include <QApplication>
#include <QScrollBar>
#include <QTimer>

#include "applicationmanager.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "dbeventscontroller.h"
#include "detailspanemanager.h"
#include "errorpresentation.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "marqueecontroller.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "scrapercontroller.h"
#include "scrolleventscontroller.h"
#include "scrollmanager.h"
#include "settingsmanager.h"
#include "timerutils.h"
#include "toolbarcontroller.h"
#include "ui_mainwindow.h"
#include "uiconstants/detailspaneconstants.h"

// =====================================================================
// Small slot handlers that live alongside their connect()s — too small
// to justify their own TU. Larger responsibility-segmented handlers live
// in mainwindow_dbevents.cpp / mainwindow_scraper.cpp or in extracted
// Controllers under src/core/ (see ScrollEventsController).
// =====================================================================

void MainWindow::onArtworkViewportUpdateRequested() {
  if (!QApplication::closingDown() && m_appManager->getArtworkManager()) {
    m_appManager->getArtworkManager()->updateViewportArtwork();
  }
}

void MainWindow::onArtworkLayoutUpdateRequested() {
  if (!QApplication::closingDown() && m_appManager->getScrollManager()) {
    m_appManager->getScrollManager()->handleLayoutChange();
  }
}

void MainWindow::onScrollFilterChanged(int visible, int total) {
  if (!QApplication::closingDown()) {
    updateWindowTitleWithFilter(visible, total);
  }
}

void MainWindow::onInteractionSelectionChanged(int /*index*/) {
  // Refresh the top-bar "pos / total" label on selection and collection-size
  // transitions. The filterChanged connection already calls
  // updateItemPositionLabel via updateWindowTitleWithFilter; this also updates
  // on direct selection moves.
  if (!QApplication::closingDown()) {
    updateItemPositionLabel();
    // Push the new selection's artwork to the marquee through the controller's
    // debounced refresh so a wheel/arrow storm coalesces into a single
    // trailing-edge update.
    if (m_marqueeController) {
      m_marqueeController->requestArtworkRefresh();
    }
  }
}

void MainWindow::onSidebarVisibilityChanged(bool visible) {
  if (visible && m_appManager->getDetailsPaneManager() && m_appManager->getScrollManager() &&
      m_appManager->getInteractionManager()) {
    int sel = m_appManager->getInteractionManager()->currentSelectedIndex();
    if (sel >= 0) {
      ItemWidget *widgetPtr =
          m_appManager->getScrollManager()->getActiveWidgets().value(sel, nullptr);
      m_appManager->getDetailsPaneManager()->updateSidebarMetadata(widgetPtr);
    }
  }
  // Push the "sidebar hidden AND would shrink" predicate into ScrollManager so
  // the upcoming metrics recompute picks the right gridWidth /
  // horizontalGridHeight pair. Overlay mode never shrinks (the sidebar
  // floats), so it stays on the primary values.
  updateScrollManagerSidebarShrinking();
  // Delay metrics recalculation to allow sidebar animation to complete before
  // recalculating grid layout dimensions.
  QTimer::singleShot(UIConstants::DetailsPane::METRICS_RECALC_DELAY_MS, this, [this]() {
    if (m_appManager->getScrollManager()) {
      m_appManager->getScrollManager()->recalculateContainerMetrics();
    }
  });
}

void MainWindow::onSidebarLayoutChanged() {
  if (m_appManager->getScrollManager()) {
    m_appManager->getScrollManager()->recalculateContainerMetrics();
  }
  if (m_appManager->getDetailsPaneManager() && m_appManager->getScrollManager() &&
      m_appManager->getInteractionManager() &&
      m_appManager->getDetailsPaneManager()->isSidebarVisible()) {
    int sel = m_appManager->getInteractionManager()->currentSelectedIndex();
    if (sel >= 0) {
      ItemWidget *widgetPtr =
          m_appManager->getScrollManager()->getActiveWidgets().value(sel, nullptr);
      m_appManager->getDetailsPaneManager()->updateSidebarMetadata(widgetPtr);
    }
  }
}

// =====================================================================
// Connection tables — every connect is a one-liner using member-function
// pointers. Read top-to-bottom for the manager graph.
// =====================================================================

void MainWindow::connectDatabaseManager() {
  auto *db = m_appManager->getDatabaseManager();
  auto *nav = m_appManager->getNavigationManager();
  auto *scroll = m_appManager->getScrollManager();
  auto *settings = m_appManager->getSettingsManager();
  auto *details = m_appManager->getDetailsPaneManager();

  // DatabaseManager → NavigationManager
  QObject::connect(db, &DatabaseManager::itemsLoaded, nav, &NavigationManager::onItemsLoaded,
                   Qt::UniqueConnection);
  // Kartend-elte: refresh the per-item state-flag registry whenever a
  // collection's items reload so the badges paint without a per-tile DB
  // hop. Slot body lives in refreshItemStateFlagsRegistry() so this TU
  // stays lambda-free per the file-header convention.
  QObject::connect(db, &DatabaseManager::itemsLoaded, this,
                   &MainWindow::refreshItemStateFlagsRegistry, Qt::UniqueConnection);
  QObject::connect(db, &DatabaseManager::itemCountLoadedWithToken, nav,
                   &NavigationManager::onItemCountLoaded, Qt::UniqueConnection);
  QObject::connect(db, &DatabaseManager::collectionScanCompleted, nav,
                   &NavigationManager::onBackgroundCollectionScanCompleted, Qt::UniqueConnection);
  QObject::connect(db, &DatabaseManager::itemsRangeLoaded, nav,
                   &NavigationManager::onItemsRangeLoaded, Qt::UniqueConnection);
  QObject::connect(db, &DatabaseManager::errorOccurred, nav,
                   &NavigationManager::onMediaLibraryError);
  // NavigationManager raises media-library errors as a signal; MainWindow owns
  // the ErrorDialog so the input layer stays free of UI-chrome includes.
  QObject::connect(nav, &NavigationManager::mediaLibraryErrorRaised, this,
                   [this](const ErrorUtils::ErrorContext &error) {
                     ErrorPresentation::showError(window(), error);
                   });

  // DatabaseManager → ScrollManager
  QObject::connect(db, &DatabaseManager::visualIndexForPathLoaded, scroll,
                   &ScrollManager::onVisualIndexForPathLoaded);

  // DatabaseManager → DbEventsController (extracted from MainWindow,
  // Kartend-hzef step 2). Context is wired here because every manager and
  // callback the controller reads back is already resolved above.
  DbEventsControllerContext dec;
  dec.getLoadingOverlay = [this]() { return m_loadingOverlay; };
  dec.getNavigationManager = [this]() { return m_appManager->getNavigationManager(); };
  dec.getDetailsPaneManager = [this]() { return m_appManager->getDetailsPaneManager(); };
  dec.getCurrentCollectionIndex = [this]() { return currentCollectionIndex; };
  dec.getCollections = [this]() -> const QList<CollectionConfig> * { return &m_collections; };
  dec.refreshTitleCounts = [this]() { refreshTitleCounts(); };
  dec.refreshFilterToolbar = [this]() { refreshFilterToolbar(); };
  dec.setWindowTitle = [this](const QString &title) { setWindowTitle(title); };
  m_dbEventsController->setContext(dec);

  auto *decCtl = m_dbEventsController.get();
  QObject::connect(db, &DatabaseManager::itemCountLoaded, decCtl,
                   &DbEventsController::releaseStartupOverlaySuppressionIfIdle);
  QObject::connect(db, &DatabaseManager::cachedCountsUpdated, decCtl,
                   &DbEventsController::refreshTitleCountsIfActive);
  QObject::connect(db, &DatabaseManager::itemsLoaded, decCtl,
                   &DbEventsController::refreshFilterToolbarOnItemsLoaded, Qt::UniqueConnection);
  QObject::connect(db, &DatabaseManager::scanProgress, decCtl, &DbEventsController::onScanProgress);
  QObject::connect(db, &DatabaseManager::scanStarting, decCtl, &DbEventsController::onScanStarting);
  QObject::connect(db, &DatabaseManager::collectionScanCompleted, decCtl,
                   &DbEventsController::onCollectionScanCompletedStartup);
  QObject::connect(db, &DatabaseManager::collectionScanCompleted, decCtl,
                   &DbEventsController::onCollectionScanCompletedOverlay);
  QObject::connect(db, &DatabaseManager::scanItemsProgress, decCtl,
                   &DbEventsController::onScanItemsProgress);

  // SettingsManager → MainWindow / DetailsPaneManager
  QObject::connect(settings, &SettingsManager::collectionsModified, this,
                   &MainWindow::rebuildHierarchyCache);

  // DatabaseManager / SettingsManager → DetailsPaneManager (sidebar summary)
  if (details) {
    QObject::connect(db, &DatabaseManager::collectionScanCompleted, decCtl,
                     &DbEventsController::refreshCollectionSummaryOnScanCompleted);
    QObject::connect(db, &DatabaseManager::cachedCountsUpdated, details,
                     &DetailsPaneManager::refreshCollectionSummary);
    QObject::connect(settings, &SettingsManager::collectionsModified, details,
                     &DetailsPaneManager::refreshCollectionSummary);
  }

  // Kartend-hzef step 3: wire the ScraperController's context here too —
  // every closure target manager is already resolved above.
  if (m_scraperController) {
    ScraperControllerContext sc;
    sc.getParentWindow = [this]() -> QWidget * { return this; };
    sc.getCollections = [this]() { return &m_collections; };
    sc.getGeneralSettings = [this]() { return &m_generalSettings; };
    sc.getCurrentCollectionIndex = [this]() { return currentCollectionIndex; };
    sc.getApplicationContext = [this]() -> const ApplicationContext * { return &m_appContext; };
    sc.getDatabaseManager = [this]() { return m_appManager->getDatabaseManager(); };
    sc.getNavigationManager = [this]() { return m_appManager->getNavigationManager(); };
    sc.getDetailsPaneManager = [this]() { return m_appManager->getDetailsPaneManager(); };
    sc.getInteractionManager = [this]() { return m_appManager->getInteractionManager(); };
    sc.getScrollManager = [this]() { return m_appManager->getScrollManager(); };
    m_scraperController->setContext(sc);
  }
}

void MainWindow::connectScrollManager() {
  auto *scroll = m_appManager->getScrollManager();
  auto *nav = m_appManager->getNavigationManager();
  auto *interaction = m_appManager->getInteractionManager();
  auto *artwork = m_appManager->getArtworkManager();

  // ScrollManager → NavigationManager
  QObject::connect(scroll, &ScrollManager::subcollectionEntered, nav,
                   &NavigationManager::onSubcollectionEntered);
  QObject::connect(scroll, &ScrollManager::virtualFolderEntered, nav,
                   &NavigationManager::onVirtualFolderEntered);
  QObject::connect(scroll, &ScrollManager::requestItemsRange, nav,
                   &NavigationManager::fetchItemsRange, Qt::UniqueConnection);

  // ScrollManager → InteractionManager (expand-mode artwork preview launch)
  if (interaction) {
    QObject::connect(scroll, &ScrollManager::artworkPreviewLaunchRequested, interaction,
                     &InteractionManager::onArtworkPreviewLaunchRequested);
  }

  // ScrollManager → ScrollEventsController (extracted from MainWindow,
  // Kartend-hzef). Context is wired here because every manager pointer
  // the controller needs is already resolved above.
  ScrollEventsControllerContext sec;
  sec.getNavigationManager = [this]() { return m_appManager->getNavigationManager(); };
  sec.getInteractionManager = [this]() { return m_appManager->getInteractionManager(); };
  sec.getScrollManager = [this]() { return m_appManager->getScrollManager(); };
  sec.getSettingsManager = [this]() { return m_appManager->getSettingsManager(); };
  sec.getDetailsPaneManager = [this]() { return m_appManager->getDetailsPaneManager(); };
  sec.getDatabaseManager = [this]() { return m_appManager->getDatabaseManager(); };
  sec.getGeneralSettings = [this]() { return &m_generalSettings; };
  sec.getCurrentCollectionIndex = [this]() { return currentCollectionIndex; };
  m_scrollEventsController->setContext(sec);

  auto *secCtl = m_scrollEventsController.get();
  QObject::connect(scroll, &ScrollManager::sortModeChangeRequested, secCtl,
                   &ScrollEventsController::onSortModeChangeRequested, Qt::UniqueConnection);
  QObject::connect(scroll, &ScrollManager::selectItemByIndex, secCtl,
                   &ScrollEventsController::onSelectItemByIndex, Qt::UniqueConnection);
  QObject::connect(scroll, &ScrollManager::coverFlowActiveChanged, secCtl,
                   &ScrollEventsController::onCoverFlowActiveChanged, Qt::UniqueConnection);
  QObject::connect(scroll, &ScrollManager::artworkPreviewVisibilityChanged, secCtl,
                   &ScrollEventsController::onArtworkPreviewVisibilityChanged,
                   Qt::UniqueConnection);
  QObject::connect(scroll, &ScrollManager::coverFlowItemActivated, secCtl,
                   &ScrollEventsController::onCoverFlowItemActivated, Qt::UniqueConnection);
  QObject::connect(scroll, &ScrollManager::listColumnWidthChanged, secCtl,
                   &ScrollEventsController::onListColumnWidthChanged, Qt::UniqueConnection);
  QObject::connect(scroll, &ScrollManager::listArtworkColumnWidthChanged, secCtl,
                   &ScrollEventsController::onListArtworkColumnWidthChanged, Qt::UniqueConnection);
  QObject::connect(scroll, &ScrollManager::filterChanged, this, &MainWindow::onScrollFilterChanged,
                   Qt::UniqueConnection);

  // ArtworkManager::TimerCoordinator → MainWindow
  if (artwork) {
    QObject::connect(artwork->getTimerCoordinator(),
                     &TimerUtils::Coordinator::viewportUpdateRequested, this,
                     &MainWindow::onArtworkViewportUpdateRequested, Qt::UniqueConnection);
    QObject::connect(artwork->getTimerCoordinator(),
                     &TimerUtils::Coordinator::layoutUpdateRequested, this,
                     &MainWindow::onArtworkLayoutUpdateRequested, Qt::UniqueConnection);
  }

  // InteractionManager → MainWindow
  if (interaction) {
    QObject::connect(interaction, &InteractionManager::selectionChanged, this,
                     &MainWindow::onInteractionSelectionChanged);
  }
}

void MainWindow::connectSidebarManager() {
  auto *details = m_appManager->getDetailsPaneManager();
  // DetailsPaneManager → MainWindow
  QObject::connect(details, &DetailsPaneManager::sidebarVisibilityChanged, this,
                   &MainWindow::onSidebarVisibilityChanged);
  QObject::connect(details, &DetailsPaneManager::sidebarLayoutChanged, this,
                   &MainWindow::onSidebarLayoutChanged);
}

void MainWindow::updateScrollManagerSidebarShrinking() {
  if (!m_appManager->getScrollManager() || !m_appManager->getDetailsPaneManager()) {
    return;
  }
  // Use DetailsPaneManager's tracked index rather than MainWindow::currentCollectionIndex
  // because this can be called from inside applySidebarStateForCollection (via
  // its sidebarVisibilityChanged emission) before MainWindow has caught up to
  // the new index after a collection switch.
  const int idx = m_appManager->getDetailsPaneManager()->currentCollectionIndex();
  if (idx < 0 || idx >= m_collections.size()) {
    m_appManager->getScrollManager()->setSidebarShrinkingActive(false);
    return;
  }
  const CollectionConfig &collection = m_collections[idx];
  // Overlay mode never shrinks the grid (the sidebar floats over content), so
  // the alt gridWidth only applies in Expand mode while the sidebar is hidden.
  if (collection.sidebar.sidebarMode != DetailsPaneMode::Expand) {
    m_appManager->getScrollManager()->setSidebarShrinkingActive(false);
    return;
  }
  const bool sidebarHidden = !m_appManager->getDetailsPaneManager()->isSidebarVisible();
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
      CollectionUtils::isDetailsPaneHorizontal(collection.sidebar.sidebarPosition);
  const bool relevantAxisIsHorizontal = (collection.viewType != ViewType::Horizontal);
  const bool paneAffectsRelevantAxis =
      relevantAxisIsHorizontal ? !paneIsHorizontalDock : paneIsHorizontalDock;
  m_appManager->getScrollManager()->setSidebarShrinkingActive(sidebarHidden ||
                                                              !paneAffectsRelevantAxis);
}

void MainWindow::connectSearchComponents() {
  // Search-mode toggle lives inside the QLineEdit as a QAction owned by the
  // ToolbarController. Wire it to InteractionManager's existing toggle slot
  // so the cycling behavior remains identical to the legacy QPushButton.
  if (m_toolbarController && m_toolbarController->searchModeAction() &&
      m_appManager->getInteractionManager()) {
    QObject::connect(m_toolbarController->searchModeAction(), &QAction::triggered,
                     m_appManager->getInteractionManager(), &InteractionManager::toggleSearchMode);
  }
}

void MainWindow::refreshFilterToolbar() {
  if (m_toolbarController) {
    m_toolbarController->refreshFilterToolbar();
  }
}

void MainWindow::refreshItemStateFlagsRegistry() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    ItemWidget::clearStateFlagsRegistry();
    return;
  }
  const CollectionConfig &cfg = m_collections.at(currentCollectionIndex);
  const QString expanded = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expanded);
  IDatabaseManager *db = m_appManager ? m_appManager->getDatabaseManager() : nullptr;
  if (uuid.isEmpty() || !db) {
    ItemWidget::clearStateFlagsRegistry();
    return;
  }
  const auto raw = db->loadItemStateFlagsForCollection(uuid);
  QHash<QString, ItemWidget::StateFlags> registry;
  registry.reserve(raw.size());
  for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
    ItemWidget::StateFlags f;
    f.pinned = it->isPinned;
    f.hidden = it->isHidden;
    f.continueLater = it->continueLater;
    registry.insert(it.key(), f);
  }
  ItemWidget::setStateFlagsRegistry(registry);
}

void MainWindow::connectFilterToolbar() {
  if (m_toolbarController) {
    m_toolbarController->connectFilterToolbar();
  }
}

void MainWindow::connectScrollBars() const {
  if (!ui->itemScrollArea || !m_appManager->getNavigationManager()) {
    return;
  }
  if (const QScrollBar *vScrollBar = ui->itemScrollArea->verticalScrollBar()) {
    QObject::connect(vScrollBar, &QScrollBar::valueChanged, m_appManager->getNavigationManager(),
                     &NavigationManager::onViewportChanged);
  }
  if (const QScrollBar *hScrollBar = ui->itemScrollArea->horizontalScrollBar()) {
    QObject::connect(hScrollBar, &QScrollBar::valueChanged, m_appManager->getNavigationManager(),
                     &NavigationManager::onViewportChanged);
  }
}
