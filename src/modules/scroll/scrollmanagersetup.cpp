// Sibling TU: setup + grid config methods for ScrollManager.
#include "applicationcontext.h"
#include "arrowkeyscrollhelper.h"
#include "artworkmanager.h"
#include "artworkpreviewoverlay.h"
#include "artworkutils.h"
#include "databasemanager.h"
#include "datasourcemanager.h"
#include "filtermanager.h"
#include "gridlayoutcalculator.h"
#include "gridutils.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "itemwidgetfactory.h"
#include "listheaderwidget.h"
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
#include "uiconstants.h"
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
  m_generalSettings = setup.getGeneralSettings();
  m_state = setup.getInteractionState();
  m_gridContainer = setup.getGridContainer();
  m_mediaScrollArea = setup.getMediaScrollArea();
  m_artworkManager = setup.getArtworkManager();
  m_collections = setup.getCollections();
  m_hierarchyCache = setup.getHierarchyCache();

  // Apply persisted column widths from settings via display manager.
  if (m_selectionDisplay) {
    m_selectionDisplay->applyGeneralSettings(m_generalSettings);
    m_selectionDisplay->setMediaScrollArea(m_mediaScrollArea);
    m_selectionDisplay->setCollectionContext(&m_context);
    m_selectionDisplay->setMetrics(&m_metrics);
    m_selectionDisplay->setActiveWidgets(&m_activeWidgets);
    m_selectionDisplay->setSelectionCoordinator(m_selectionCoordinator.get());
    m_selectionDisplay->setArrowKeyScrollHelper(m_arrowKeyScrollHelper.get());
    m_selectionDisplay->setInteractionState(m_state);
    m_selectionDisplay->setArrowKeyViewUpdateTimer(m_arrowKeyViewUpdateTimer);
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

  // Configure item widget factory with dependencies
  if (m_widgetFactory) {
    m_widgetFactory->setArtworkManager(m_artworkManager);
    m_widgetFactory->setCollections(m_collections);
    m_widgetFactory->setCollectionColumnWidth(
        m_selectionDisplay ? m_selectionDisplay->collectionColumnWidth() : 150);
    m_widgetFactory->setArtworkColumnWidth(
        m_selectionDisplay ? m_selectionDisplay->artworkColumnWidth() : 32);
  }
  if (m_selectionDisplay) {
    m_selectionDisplay->setWidgetFactory(m_widgetFactory.get());
  }

  // Configure search loading overlay with scroll area viewport
  if (m_dataSource && m_mediaScrollArea) {
    m_dataSource->setSearchOverlayParent(m_mediaScrollArea->viewport());
  }
  // Configure arrow key scroll helper with dependencies
  if (m_arrowKeyScrollHelper) {
    m_arrowKeyScrollHelper->setScrollArea(m_mediaScrollArea);
    m_arrowKeyScrollHelper->setInteractionState(m_state);
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
// Data Accessors - delegate to ScrollDataManager
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
                               .arg(context.config.includeContentSubfolders)
                               .arg(context.config.showAllSubfolderItems)
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

  if (m_totalItems == 0) {
    setupEmptyVirtualScrolling();
    return;
  }

  setupNormalVirtualScrolling();

  // If we have a pending selection restore, query the database now that
  // the context and data are set up
  if (!m_pendingRestoreFilePath.isEmpty() && m_databaseManager && m_collections) {
    m_databaseManager->fetchVisualIndexForPath(m_context, *m_collections, m_pendingRestoreFilePath);
  }
}
