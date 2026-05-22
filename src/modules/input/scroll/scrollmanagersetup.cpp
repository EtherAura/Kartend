// Sibling TU: setup + grid config methods for ScrollManager.
#include "applicationcontext.h"
#include "arrowkeyscrollhelper.h"
#include "artworkutils.h"
#include "coverflowcontroller.h"
#include "datasourcemanager.h"
#include "filtermanager.h"
#include "gridlayoutcalculator.h"
#include "gridutils.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "interactionstateholder.h"
#include "itemwidgetfactory.h"
#include "loggingcategories.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "scrolleventhandler.h"
#include "scrollmanager.h"
#include "searchloadingoverlay.h"
#include "selectioncoordinator.h"
#include "selectiondisplaymanager.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "timerutils.h"
#include "virtualcontainermanager.h"
#include "widgetpoolmanager.h"
#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

#include <QtGlobal>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcScrollManager().isDebugEnabled()) {                                                      \
      qCDebug(lcScrollManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

void ScrollManager::setupReferences(const ScrollManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_generalSettings = setup.getGeneralSettings();
  m_gridContainer = setup.getGridContainer();
  m_mediaScrollArea = setup.getMediaScrollArea();
  m_collections = setup.getCollections();
  m_hierarchyCache = setup.getHierarchyCache();

  // Hand the cover-flow controller its borrowed dependencies. m_context is a
  // ScrollManager member whose address is stable across collection switches,
  // so a pointer to it stays valid even as its contents are reassigned.
  if (m_coverFlow) {
    m_coverFlow->setupReferences({.ctx = m_ctx,
                                  .mediaScrollArea = m_mediaScrollArea,
                                  .gridContainer = m_gridContainer,
                                  .context = &m_context,
                                  .collections = m_collections,
                                  .dataManager = m_dataManager});
  }

  // Snapshot sibling-manager pointers for setter-based handoff to helpers.
  // These mirror ctx and are not stored as ScrollManager member fields.
  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;

  // Hand the centralized z-order coordinator down to the two helper
  // managers that own overlay widgets (SelectionOverlayManager for the
  // selection highlight, SearchLoadingOverlay for the search-loading
  // spinner). Null is safe — each manager falls back to direct raise()
  // when no layer manager is installed.
  if (m_ctx && m_overlayManager) {
    m_overlayManager->setLayerManager(m_ctx->ui.overlayLayerManager);
  }
  if (m_ctx && m_searchLoadingOverlay) {
    m_searchLoadingOverlay->setLayerManager(m_ctx->ui.overlayLayerManager);
  }

  // Apply persisted column widths from settings via display manager.
  if (m_selectionDisplay) {
    m_selectionDisplay->applyGeneralSettings(m_generalSettings);
    m_selectionDisplay->setMediaScrollArea(m_mediaScrollArea);
    m_selectionDisplay->setCollectionContext(&m_context);
    m_selectionDisplay->setMetrics(&m_metrics);
    m_selectionDisplay->setActiveWidgets(&m_activeWidgets);
    m_selectionDisplay->setSelectionCoordinator(m_selectionCoordinator.get());
    m_selectionDisplay->setArrowKeyScrollHelper(m_arrowKeyScrollHelper.get());
    m_selectionDisplay->setInteractionState(state);
    m_selectionDisplay->setArrowKeyViewUpdateTimer(&m_arrowKeyViewUpdateTimer);
    m_selectionDisplay->setEnsureWidgetCallback([this](int idx) { ensureWidgetForIndex(idx); });
    m_selectionDisplay->setItemPositionCallback([this](int idx) { return getItemPosition(idx); });
    m_selectionDisplay->setTotalItemsProvider([this] { return m_totalItems; });
    m_selectionDisplay->setDestroyingProvider([this] { return m_destroying; });
  }

  // Configure container manager with scroll area and grid container
  if (m_containerManager) {
    m_containerManager->setGridContainer(m_gridContainer);
    m_containerManager->setScrollArea(m_mediaScrollArea);
  }

  // Configure selection coordinator with grid container and callbacks
  if (m_selectionCoordinator) {
    m_selectionCoordinator->setGridContainer(m_gridContainer);
    m_selectionCoordinator->setPositionCallback([this](int idx) { return getItemPosition(idx); });
    m_selectionCoordinator->setMetricsCallback(
        [this]() { return std::make_pair(m_metrics.itemWidth, m_metrics.itemHeight); });
  }

  // Configure scroll event handler with scroll area and idle timer
  if (m_scrollEventHandler) {
    m_scrollEventHandler->setScrollArea(m_mediaScrollArea);
    m_scrollEventHandler->setIdleTimer(m_userScrollIdleTimer);
  }

  // Configure item widget factory with dependencies. Kartend-davi: route
  // sibling-manager access through ctx instead of caching individual
  // pointers in the factory.
  if (m_widgetFactory) {
    m_widgetFactory->setApplicationContext(m_ctx);
    m_widgetFactory->setCollections(m_collections);
    m_widgetFactory->setCollectionColumnWidth(
        m_selectionDisplay ? m_selectionDisplay->collectionColumnWidth() : 150);
    m_widgetFactory->setArtworkColumnWidth(
        m_selectionDisplay ? m_selectionDisplay->artworkColumnWidth() : 32);
  }
  if (m_selectionDisplay) {
    m_selectionDisplay->setWidgetFactory(m_widgetFactory.get());
  }

  // Configure search loading overlay with scroll area viewport. Kartend-davi:
  // sibling-manager access flows through ctx instead of pushing
  // IDatabaseManager pointers down explicitly.
  if (m_dataSource) {
    m_dataSource->setApplicationContext(m_ctx);
    if (m_mediaScrollArea) {
      m_dataSource->setSearchOverlayParent(m_mediaScrollArea->viewport());
    }
  }
  // Configure arrow key scroll helper with dependencies
  if (m_arrowKeyScrollHelper) {
    m_arrowKeyScrollHelper->setScrollArea(m_mediaScrollArea);
    m_arrowKeyScrollHelper->setInteractionState(state);
    m_arrowKeyScrollHelper->setScrollEventHandler(m_scrollEventHandler.get());
    m_arrowKeyScrollHelper->setGeneralSettings(m_generalSettings);
  }

  // Pass dependencies to FilterManager
  if (m_filterManager) {
    m_filterManager->setCollections(m_collections);
    m_filterManager->setHierarchyCache(m_hierarchyCache);
  }

  // Configure pre-search state manager with scroll area and grid container
  if (m_preSearchStateManager) {
    m_preSearchStateManager->setReferences(m_mediaScrollArea, m_gridContainer);
  }

  if (m_mediaScrollArea) {
    m_mediaScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (auto *horizontalScrollbar = m_mediaScrollArea->horizontalScrollBar()) {
      horizontalScrollbar->setValue(0);
      horizontalScrollbar->hide();
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────
// Data Accessors - delegate to ScrollDataStore
// ─────────────────────────────────────────────────────────────────────────

auto ScrollManager::getFilePaths() const -> const QStringList & {
  return m_dataManager->filePaths();
}

auto ScrollManager::getFileNames() const -> const QHash<QString, QString> & {
  return m_dataManager->fileNames();
}

auto ScrollManager::getSubcollectionCount() const -> int {
  return m_dataManager->subcollectionCount();
}

auto ScrollManager::getVirtualFolderCount() const -> int {
  return m_dataManager->virtualFolderCount();
}

void ScrollManager::setInitialScrollIndex(int index) {
  m_initialScrollIndex = index;
}

// Initializes virtual scrolling and prepares virtual container; primes mappings
// for aggregated views.
void ScrollManager::setupVirtualScrolling(int totalCount, const CollectionContext &context) {
  if ((!m_gridContainer) || (!m_mediaScrollArea)) {
    return;
  }

  cleanup();

  m_selectionState->reset();

  m_context = context;

  qCDebug(lcSearchDiag) << QString("setupVirtualScrolling: totalCount=%1 collIndex=%2 "
                                   "mediaDir='%3' includeSubfolders=%4 showAllSubfolderItems=%5 "
                                   "suppressVirtualFolders=%6")
                               .arg(totalCount)
                               .arg(context.currentIndex)
                               .arg(context.config.mediaDirectory)
                               .arg(context.config.folderBrowsing.includeContentSubfolders)
                               .arg(context.config.folderBrowsing.showAllSubfolderItems)
                               .arg(context.suppressVirtualFolders);

  initializeSubcollections();
  initializeVirtualFolders();

  const int subcollCount = m_dataManager->subcollectionCount();
  const int vfCount = m_dataManager->virtualFolderCount();
  qCDebug(lcSearchDiag) << QString("setupVirtualScrolling: after init subcollCount=%1 vfCount=%2")
                               .arg(subcollCount)
                               .arg(vfCount);

  if (!m_context.filePaths.isEmpty()) {
    // Preloaded data from context - copy to data manager
    m_dataManager->filePaths() = m_context.filePaths;
    m_dataManager->fileNames() = m_context.fileNames;
    // Apply unified sorting if enabled (sorts subcollections, folders, and
    // files together)
    m_dataManager->applyUnifiedSort(m_context, m_collections);
    qCDebug(lcSearchDiag)
        << QString("setupVirtualScrolling: preloaded filePaths=%1").arg(m_context.filePaths.size());
  } else {
    // On-demand loading - initialize storage with placeholder count
    // totalCount includes subcollections + virtualFolders + mediaItems
    // Storage should only hold mediaItems
    int itemCount = totalCount - subcollCount - vfCount;
    qCDebug(lcSearchDiag) << QString("setupVirtualScrolling: on-demand itemCount=%1 "
                                     "(totalCount=%2 - subcoll=%3 - vf=%4)")
                                 .arg(itemCount)
                                 .arg(totalCount)
                                 .arg(subcollCount)
                                 .arg(vfCount);
    if (itemCount < 0) {
      itemCount = 0;
    }
    m_dataManager->initializeStorage(itemCount);
  }

  m_totalItems = m_dataManager->totalItemCount();
  qCDebug(lcSearchDiag)
      << QString("setupVirtualScrolling: final m_totalItems=%1").arg(m_totalItems);

  // when the collection has hideMissingArtwork enabled the
  // FilterManager needs the latest source data + context so its baseline
  // (no-search) filter excludes media items that have no artwork. Pushing
  // unconditionally is cheap and keeps the FilterManager state consistent
  // across collection switches.
  if (m_filterManager) {
    m_filterManager->setSourceData(m_dataManager->filePaths(), m_dataManager->fileNames(),
                                   m_dataManager->filePathToDisplayName(),
                                   m_dataManager->subcollections());
    m_filterManager->setContext(m_context);
    if (m_context.config.hideMissingArtwork && !m_filterManager->isFiltered()) {
      m_filterManager->clearFilter();
      if (m_filterManager->isFiltered()) {
        m_totalItems = m_filterManager->filteredCount();
      }
    }
  }

  if (m_totalItems == 0) {
    setupEmptyVirtualScrolling();
    return;
  }

  setupNormalVirtualScrolling();

  // refresh cover-flow card list and visibility on every navigation entry
  // so switching into a collection that uses cover flow shows up correctly
  // even though handleLayoutChange isn't called here. Card-list rebuild is
  // gated on the carousel being the active view so we don't pay per-item
  // descriptor work when navigating into a grid/list collection.
  if (m_coverFlow->isActive()) {
    m_coverFlow->ensureWidget();
    m_coverFlow->applyConfig();
    m_coverFlow->rebuildCards();
  }
  m_coverFlow->applyVisibility();

  // If we have a pending selection restore, query the database now that
  // the context and data are set up
  if (IDatabaseManager *dbm = m_ctx ? m_ctx->databaseManager() : nullptr;
      !m_pendingRestoreFilePath.isEmpty() && dbm && m_collections) {
    dbm->fetchVisualIndexForPath(m_context, *m_collections, m_pendingRestoreFilePath);
  }
}
