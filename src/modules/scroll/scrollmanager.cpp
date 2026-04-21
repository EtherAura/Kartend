// Manages virtual scrolling, widget pooling, and grid layout for large item
// collections.
#include "scrollmanager.h"
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
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "scrolleventhandler.h"
#include "searchloadingoverlay.h"
#include "selectioncoordinator.h"
#include "selectiondisplaymanager.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "timerutils.h"
#include "uiconstants.h"
#include "virtualcontainermanager.h"
#include "widgetpoolmanager.h"
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
#include <algorithm>

#include <QtGlobal>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcScrollManager, "kartend.scrollmanager")
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcScrollManager().isDebugEnabled()) {                                  \
      qCDebug(lcScrollManager) << msg;                                         \
    }                                                                          \
  } while (0)

// Temporary diagnostic logging (release-safe) gated by env var.
// Enable with: `KARTEND_SEARCH_DIAG=1 kartend`
static inline bool searchDiagEnabled() {
  return qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG");
}

#define diagLog(msg)                                                           \
  do {                                                                         \
    if (searchDiagEnabled()) {                                                 \
      qWarning() << "[SearchDiag][ScrollManager]" << msg;                      \
    }                                                                          \
  } while (0)

// Initializes timers for throttle, arrow-key updates, and a short idle window
// to treat any scrollbar interaction as user-driven scrolling
ScrollManager::ScrollManager(QObject *parent) : QObject(parent) {
  // Widget pool for recycling ItemWidgets
  m_widgetPool = std::make_unique<WidgetPoolManager>(this);

  // Data source manager: owns FilterManager + ScrollDataManager +
  // PreSearchStateManager + SearchLoadingOverlay (Kartend-gg2).
  m_dataSource = std::make_unique<DataSourceManager>(this);
  m_filterManager = m_dataSource->filterManager();
  m_dataManager = m_dataSource->dataManager();
  m_preSearchStateManager = m_dataSource->preSearchStateManager();
  m_searchLoadingOverlay = m_dataSource->searchLoadingOverlay();
  connect(m_dataSource.get(), &DataSourceManager::filterChanged, this,
          &ScrollManager::filterChanged);

  // Selection display manager: owns overlay + state tracker + list header +
  // artwork preview overlay (Kartend-3u5).
  m_selectionDisplay = std::make_unique<SelectionDisplayManager>(this);
  m_overlayManager = m_selectionDisplay->overlay();
  m_selectionState = m_selectionDisplay->state();

  // Forward list-mode signals from display manager out through ScrollManager.
  connect(m_selectionDisplay.get(),
          &SelectionDisplayManager::sortModeChangeRequested, this,
          &ScrollManager::sortModeChangeRequested);
  connect(m_selectionDisplay.get(),
          &SelectionDisplayManager::listColumnWidthChanged, this,
          &ScrollManager::listColumnWidthChanged);
  connect(m_selectionDisplay.get(),
          &SelectionDisplayManager::listArtworkColumnWidthChanged, this,
          &ScrollManager::listArtworkColumnWidthChanged);

  connect(m_overlayManager, &SelectionOverlayManager::animationFinished, this,
          [this]() {
            // Update widget selection states when animation finishes
            if (m_selectionState->needsCommitUpdate(
                    m_selectionState->lastSelectedIndex())) {
              if (auto *prevSel = m_activeWidgets.value(
                      m_selectionState->committedSelectedIndex(), nullptr)) {
                prevSel->setSelected(false);
              }
            }
            if (auto *newSel = m_activeWidgets.value(
                    m_selectionState->lastSelectedIndex(), nullptr)) {
              newSel->setSelected(true);
            }
            m_selectionState->commitSelection(
                m_selectionState->lastSelectedIndex());

            // Hide overlay if not in force-visible mode
            if (!m_overlayManager->shouldKeepVisible()) {
              m_overlayManager->hide();
            } else {
              m_overlayManager->raise();
            }
            updateVirtualView();
          });

  // SearchLoadingOverlay is now owned by m_dataSource (Kartend-gg2).

  // Virtual container manager for container lifecycle
  m_containerManager = std::make_unique<VirtualContainerManager>(this);
  m_containerManager->setOverlayManager(m_overlayManager);

  // Selection coordinator for selection state and movement analysis
  m_selectionCoordinator = std::make_unique<SelectionCoordinator>(this);
  m_selectionCoordinator->setOverlayManager(m_overlayManager);

  // Scroll event handler for scroll event wiring
  m_scrollEventHandler = std::make_unique<ScrollEventHandler>(this);
  connect(m_scrollEventHandler.get(), &ScrollEventHandler::scrollChanged, this,
          &ScrollManager::onScrollChanged);
  connect(m_scrollEventHandler.get(), &ScrollEventHandler::userScrollEnded,
          this, &ScrollManager::updateVirtualView);
  connect(m_scrollEventHandler.get(), &ScrollEventHandler::sliderMoved, this,
          &ScrollManager::onSliderMoved);

  // Item widget factory for creating and configuring widgets
  m_widgetFactory = std::make_unique<ItemWidgetFactory>(this);
  m_widgetFactory->setWidgetPool(m_widgetPool.get());
  connect(m_widgetFactory.get(), &ItemWidgetFactory::subcollectionDoubleClicked,
          this, &ScrollManager::onSubcollectionDoubleClicked);
  connect(m_widgetFactory.get(), &ItemWidgetFactory::virtualFolderDoubleClicked,
          this, &ScrollManager::onVirtualFolderDoubleClicked);
  connect(m_widgetFactory.get(), &ItemWidgetFactory::requestItemsRange, this,
          [this](int offset, int limit) {
            debugLog("requestItemsRange: offset="
                     << offset << "limit=" << limit
                     << "totalItems=" << m_totalItems << "mediaFileCount="
                     << (m_dataManager ? m_dataManager->fileCount() : -1));
            emit requestItemsRange(offset, limit);
          });

  // Arrow key scroll helper for centering animation
  m_arrowKeyScrollHelper = std::make_unique<ArrowKeyScrollHelper>(this);
  connect(m_arrowKeyScrollHelper.get(),
          &ArrowKeyScrollHelper::requestViewUpdate, this,
          &ScrollManager::updateVirtualView);

  // ScrollDataManager and PreSearchStateManager are now owned by m_dataSource
  // (Kartend-gg2). Raw aliases (m_dataManager, m_preSearchStateManager) were
  // set up at construction.

  // Note: SelectionStateTracker is now owned by m_selectionDisplay; the
  // m_selectionState raw alias was set up above.

  // Throttle timer - only fires once per interval, ignores subsequent triggers
  m_scrollTimer = new QTimer(this);
  m_scrollTimer->setSingleShot(true);
  m_scrollTimer->setInterval(UIConstants::Timing::SCROLL_THROTTLE_DELAY_MS);
  connect(m_scrollTimer, &QTimer::timeout, this,
          &ScrollManager::onThrottledUpdate);

  // Arrow key view update timer - delegates to helper
  m_arrowKeyViewUpdateTimer = new QTimer(this);
  m_arrowKeyViewUpdateTimer->setSingleShot(true);
  m_arrowKeyViewUpdateTimer->setInterval(
      UIConstants::Keyboard::VIEW_UPDATE_INTERVAL_MS);
  connect(m_arrowKeyViewUpdateTimer, &QTimer::timeout, this,
          &ScrollManager::onArrowKeyViewUpdate);

  // Debounce timer - restarts on each trigger, fires after inactivity
  m_userScrollIdleTimer = new TimerUtils::DebouncedTimer(
      UIConstants::Mouse::USER_SCROLL_IDLE_TIMER_MS, this);
  connect(m_userScrollIdleTimer, &TimerUtils::DebouncedTimer::triggered, this,
          [this]() {
            if (m_scrollEventHandler) {
              m_scrollEventHandler->setUserScrollActive(false);
            }
          });

  // Prewarm timer - replenishes widget pool after scroll activity settles
  m_prewarmIdleTimer = new TimerUtils::DebouncedTimer(
      UIConstants::Widget::Pool::PREWARM_IDLE_MS, this);
  connect(
      m_prewarmIdleTimer, &TimerUtils::DebouncedTimer::triggered, this,
      [this]() {
        if (m_widgetPool) {
          // Prune stale widgets that weren't reused during collection switch
          // This reclaims memory from the soft-cleared pool
          m_widgetPool->pruneStaleWidgets();

          int visibleRows = (getLastVisibleRow() - getFirstVisibleRow()) + 1;
          m_widgetPool->setVisibleMetrics(visibleRows, m_metrics.itemsPerRow);
          // Use async prewarm to avoid blocking UI during idle replenishment
          m_widgetPool->prewarmAsync();
        }
      });
}

// Destructor disconnects scroll events, clears timers, deletes widgets and
// container
ScrollManager::~ScrollManager() {
  m_destroying = true;
  disconnectScrollEvents();

  TimerUtils::stopAndDisconnectTimers(
      {m_scrollTimer, m_arrowKeyViewUpdateTimer});

  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      widget->hide();
      widget->deleteLater();
    }
  }
  m_activeWidgets.clear();

  // Discard any saved pre-search state (widgets will be cleaned up by manager)
  if (m_preSearchStateManager) {
    m_preSearchStateManager->discardSavedState();
  }

  if (m_widgetPool) {
    m_widgetPool->clear();
  }
  cleanupVirtualContainer();

  // Clean up list header (parented to viewport, not container) via the
  // display manager which owns it.
  if (m_selectionDisplay) {
    m_selectionDisplay->destroyListHeader();
  }
}

// Widget pool management - delegates to WidgetPoolManager/ItemWidgetFactory
ItemWidget *ScrollManager::acquireWidget() {
  if (!m_widgetPool) {
    return new ItemWidget(m_virtualContainer);
  }
  m_widgetPool->setWidgetParent(m_virtualContainer);
  return m_widgetPool->acquire();
}

void ScrollManager::releaseWidget(ItemWidget *widget) {
  if (!widget) {
    return;
  }
  // Clear artwork state before returning widget to pool - prevents stale
  // pending entries from blocking new artwork when the widget is recycled
  if (m_artworkManager) {
    m_artworkManager->clearPendingArtworkForWidget(widget);
  }
  if (m_widgetFactory) {
    int visibleRows = (getLastVisibleRow() - getFirstVisibleRow()) + 1;
    m_widgetFactory->releaseWidget(widget, visibleRows, m_metrics.itemsPerRow);
  } else if (m_widgetPool) {
    int visibleRows = (getLastVisibleRow() - getFirstVisibleRow()) + 1;
    m_widgetPool->setVisibleMetrics(visibleRows, m_metrics.itemsPerRow);
    m_widgetPool->release(widget);
  } else {
    widget->deleteLater();
  }
}

// ScrollManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, QWidget *, GridContainer,
                      gridContainer)
SETUP_GETTER_DEF(ScrollManagerSetup, QScrollArea *, MediaScrollArea,
                 mediaScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, ArtworkManager *, ArtworkManager,
                      artworkManager)
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, const QList<CollectionConfig> *,
                      Collections, collections)
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, const CollectionHierarchyCache *,
                      HierarchyCache, hierarchyCache)
SETUP_GETTER_DEF_CTX_ONLY(ScrollManagerSetup, InteractionStateHolder *,
                          InteractionState, interactionState)
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, const GeneralSettings *,
                      GeneralSettings, generalSettings)

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
  }

  // Configure container manager with scroll area and grid container
  if (m_containerManager) {
    m_containerManager->setGridContainer(m_gridContainer);
    m_containerManager->setScrollArea(m_mediaScrollArea);
  }

  // Configure selection coordinator with grid container and callbacks
  if (m_selectionCoordinator) {
    m_selectionCoordinator->setGridContainer(m_gridContainer);
    m_selectionCoordinator->setPositionCallback(
        [this](int idx) { return getItemPosition(idx); });
    m_selectionCoordinator->setMetricsCallback([this]() {
      return std::make_pair(m_metrics.itemWidth, m_metrics.itemHeight);
    });
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
void ScrollManager::setupVirtualScrolling(int totalCount,
                                          const CollectionContext &context) {
  if ((!m_gridContainer) || (!m_mediaScrollArea)) {
    return;
  }

  cleanup();

  m_selectionState->reset();

  m_context = context;

  diagLog(QString("setupVirtualScrolling: totalCount=%1 collIndex=%2 "
                  "mediaDir='%3' includeSubfolders=%4 showAllSubfolderItems=%5 "
                  "suppressVirtualFolders=%6")
              .arg(totalCount)
              .arg(context.currentIndex)
              .arg(context.config.mediaDirectory)
              .arg(context.config.includeContentSubfolders)
              .arg(context.config.showAllSubfolderItems)
              .arg(context.suppressVirtualFolders));

  initializeSubcollections();
  initializeVirtualFolders();

  const int subcollCount = m_dataManager->subcollectionCount();
  const int vfCount = m_dataManager->virtualFolderCount();
  diagLog(
      QString("setupVirtualScrolling: after init subcollCount=%1 vfCount=%2")
          .arg(subcollCount)
          .arg(vfCount));

  if (!m_context.filePaths.isEmpty()) {
    // Preloaded data from context - copy to data manager
    m_dataManager->filePaths() = m_context.filePaths;
    m_dataManager->fileNames() = m_context.fileNames;
    // Apply unified sorting if enabled (sorts subcollections, folders, and
    // files together)
    m_dataManager->applyUnifiedSort(m_context, m_collections);
    diagLog(QString("setupVirtualScrolling: preloaded filePaths=%1")
                .arg(m_context.filePaths.size()));
  } else {
    // On-demand loading - initialize storage with placeholder count
    // totalCount includes subcollections + virtualFolders + mediaItems
    // Storage should only hold mediaItems
    int itemCount = totalCount - subcollCount - vfCount;
    diagLog(QString("setupVirtualScrolling: on-demand itemCount=%1 "
                    "(totalCount=%2 - subcoll=%3 - vf=%4)")
                .arg(itemCount)
                .arg(totalCount)
                .arg(subcollCount)
                .arg(vfCount));
    if (itemCount < 0) {
      itemCount = 0;
    }
    m_dataManager->initializeStorage(itemCount);
  }

  m_totalItems = m_dataManager->totalItemCount();
  diagLog(QString("setupVirtualScrolling: final m_totalItems=%1")
              .arg(m_totalItems));

  if (m_totalItems == 0) {
    setupEmptyVirtualScrolling();
    return;
  }

  setupNormalVirtualScrolling();

  // If we have a pending selection restore, query the database now that
  // the context and data are set up
  if (!m_pendingRestoreFilePath.isEmpty() && m_databaseManager &&
      m_collections) {
    m_databaseManager->fetchVisualIndexForPath(m_context, *m_collections,
                                               m_pendingRestoreFilePath);
  }
}


void ScrollManager::updateViewType(ViewType viewType) {
  if (m_context.config.viewType == viewType) {
    return;
  }
  m_context.config.viewType = viewType;

  // Update the factory's context as well
  if (m_widgetFactory) {
    m_widgetFactory->setCollectionContext(m_context);
  }

  // Reset scroll position before layout change - grid and list modes have
  // different row heights, so the old scroll position is meaningless
  if (m_mediaScrollArea && m_mediaScrollArea->verticalScrollBar()) {
    m_mediaScrollArea->verticalScrollBar()->setValue(0);
  }

  handleLayoutChange();
}

void ScrollManager::updateGridWidth(int newGridWidth) {
  if (m_context.config.gridWidth == newGridWidth) {
    return;
  }
  m_context.config.gridWidth = newGridWidth;
  if (!m_virtualContainer) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (!widget) {
      continue;
    }
    QPoint position = getItemPosition(it.key());
    widget->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                        m_metrics.itemHeight);
  }
  updateVirtualView();
}

// Updates active widgets for current viewport and triggers artwork updates
// unless suppression is enforced without selection allowance
void ScrollManager::updateVirtualView() {
  if (m_destroying || QApplication::closingDown()) {
    return;
  }
  if ((!m_virtualContainer) || (!m_mediaScrollArea)) {
    if (m_emptyViewDebugBudget > 0) {
      --m_emptyViewDebugBudget;
      debugLog("updateVirtualView: early return - missing container/scrollArea "
               "(virtualContainer="
               << (m_virtualContainer != nullptr)
               << " scrollArea=" << (m_mediaScrollArea != nullptr) << ")");
    }
    return;
  }
  if (m_metrics.itemsPerRow <= 0) {
    if (m_emptyViewDebugBudget > 0) {
      --m_emptyViewDebugBudget;
      debugLog("updateVirtualView: early return - itemsPerRow<=0 (itemsPerRow="
               << m_metrics.itemsPerRow << ")");
    }
    return;
  }

  QSet<int> needed = calculateNeededIndices();

  if (needed.isEmpty()) {
    if (m_emptyViewDebugBudget > 0) {
      --m_emptyViewDebugBudget;
      debugLog("updateVirtualView: needed is EMPTY (totalItems="
               << m_totalItems << "itemsPerRow=" << m_metrics.itemsPerRow
               << "itemWxH=" << m_metrics.itemWidth << "x"
               << m_metrics.itemHeight << ")");
    }
    return;
  }

  for (int visualIndex : needed) {
    ensureWidgetForIndex(visualIndex);
  }

  if (m_activeWidgets.isEmpty() && m_emptyViewDebugBudget > 0) {
    --m_emptyViewDebugBudget;
    debugLog("updateVirtualView: NO widgets materialized (needed="
             << needed.size() << "firstRow=" << getFirstVisibleRow()
             << "lastRow=" << getLastVisibleRow() << "totalItems="
             << m_totalItems << "itemsPerRow=" << m_metrics.itemsPerRow
             << "itemWxH=" << m_metrics.itemWidth << "x" << m_metrics.itemHeight
             << "virtualContainer=" << (m_virtualContainer != nullptr)
             << "scrollArea=" << (m_mediaScrollArea != nullptr) << ")");
  }

  removeUnneededWidgets(needed);
  updateArtworkIfAllowed();

  if (m_overlayManager) {
    if (m_overlayManager->isForceVisible() &&
        m_selectionState->hasSelection()) {
      refreshSelectionOverlayState();
    }
    m_overlayManager->raise();
  }
}

auto ScrollManager::calculateNeededIndices() const -> QSet<int> {
  int firstVisible = getFirstVisibleRow();
  int lastVisible = getLastVisibleRow();
  int startRow = qMax(0, firstVisible - 1);
  int endRow = lastVisible + 1;

  int maxRow =
      ((m_totalItems + m_metrics.itemsPerRow - 1) / m_metrics.itemsPerRow) - 1;
  if (maxRow < 0) {
    return {};
  }
  endRow = std::min(endRow, maxRow);

  QSet<int> needed;
  for (int rowIndex = startRow; rowIndex <= endRow; ++rowIndex) {
    for (int columnIndex = 0; columnIndex < m_metrics.itemsPerRow;
         ++columnIndex) {
      int visualIndex = (rowIndex * m_metrics.itemsPerRow) + columnIndex;
      if (visualIndex < m_totalItems) {
        needed.insert(visualIndex);
      }
    }
  }
  return needed;
}

void ScrollManager::removeUnneededWidgets(const QSet<int> &needed) {
  QList<int> existing = m_activeWidgets.keys();
  for (int visualIndex : existing) {
    if (!needed.contains(visualIndex)) {
      if (ItemWidget *widget = m_activeWidgets.value(visualIndex)) {
        releaseWidget(widget);
      }
      m_activeWidgets.remove(visualIndex);
    }
  }
}

void ScrollManager::updateArtworkIfAllowed() {
  if (!QApplication::closingDown() && m_artworkManager) {
    const bool suppressArtwork = m_state && m_state->artwork().suppressArtwork;
    const bool allowDuringSelection =
        m_state && m_state->artwork().allowDuringSelection;
    if (!suppressArtwork || allowDuringSelection) {
      m_artworkManager->updateViewportArtwork();
    }
  }
}

auto ScrollManager::getEffectiveHorizontalSpacing() const -> int {
  return m_context.config.horizontalSpacing;
}

auto ScrollManager::getFirstVisibleRow() const -> int {
  if (!m_mediaScrollArea) {
    return 0;
  }
  int scrollY = m_mediaScrollArea->verticalScrollBar()->value();
  int viewportHeight = m_mediaScrollArea->viewport()->height();
  auto [firstRow, lastRow] = GridLayoutCalculator::getVisibleRowRange(
      scrollY, viewportHeight, m_metrics, 0);
  return firstRow;
}

auto ScrollManager::getLastVisibleRow() const -> int {
  if (!m_mediaScrollArea) {
    return 0;
  }
  int scrollY = m_mediaScrollArea->verticalScrollBar()->value();
  int viewportHeight = m_mediaScrollArea->viewport()->height();
  auto [firstRow, lastRow] = GridLayoutCalculator::getVisibleRowRange(
      scrollY, viewportHeight, m_metrics, 0);
  return lastRow;
}

auto ScrollManager::getSubcollectionName(int subcollectionIndex) const
    -> QString {
  if ((!m_collections) || subcollectionIndex < 0 ||
      subcollectionIndex >= m_collections->size()) {
    return {};
  }
  return (*m_collections)[subcollectionIndex].name;
}

void ScrollManager::setDatabaseManager(DatabaseManager *manager) {
  m_databaseManager = manager;
  if (m_dataSource) {
    m_dataSource->setDatabaseManager(manager);
  }
}

void ScrollManager::setPendingSelectionRestoreByPath(const QString &filePath) {
  // Just store the path - we'll query the database after the collection reloads
  // (in setupVirtualScrolling) when the context and data are ready
  m_pendingRestoreFilePath = filePath;
}

void ScrollManager::onVisualIndexForPathLoaded(int visualIndex,
                                               const QString &filePath) {
  // Only process if this is the path we're waiting for
  if (filePath != m_pendingRestoreFilePath) {
    return;
  }

  m_pendingRestoreFilePath.clear();

  if (visualIndex >= 0) {
    // The database returns position among media items only. In the UI,
    // subcollections and virtual folders appear before media items,
    // so we need to offset the index accordingly.
    int prefixCount = m_dataManager ? (m_dataManager->subcollectionCount() +
                                       m_dataManager->virtualFolderCount())
                                    : 0;
    int adjustedIndex = visualIndex + prefixCount;
    emit selectItemByIndex(adjustedIndex);
  }
}

bool ScrollManager::isArtworkPreviewVisible() const {
  return m_selectionDisplay && m_selectionDisplay->isArtworkPreviewVisible();
}

bool ScrollManager::hideArtworkPreview() {
  return m_selectionDisplay && m_selectionDisplay->hideArtworkPreview();
}

void ScrollManager::recenterVirtualContainer() { positionVirtualContainer(); }

auto ScrollManager::getCurrentAlignment() const -> HorizontalAlignment {
  if ((!m_collections) || m_context.currentIndex < 0 ||
      m_context.currentIndex >= m_collections->size()) {
    return HorizontalAlignment::Center;
  }
  return (*m_collections)[m_context.currentIndex].horizontalAlignment;
}


auto ScrollManager::getScrollbarWidth() const -> int {
  if (!m_mediaScrollArea) {
    return 0;
  }
  QScrollBar *verticalScrollbar = m_mediaScrollArea->verticalScrollBar();
  if (!verticalScrollbar) {
    return 0;
  }
  if (verticalScrollbar->isVisible()) {
    return verticalScrollbar->width();
  }
  if (willNeedVerticalScrollbar()) {
    int barWidth = verticalScrollbar->sizeHint().width();
    static constexpr int DEFAULT_SCROLLBAR_WIDTH = 16;
    return barWidth > 0 ? barWidth : DEFAULT_SCROLLBAR_WIDTH;
  }
  return 0;
}

auto ScrollManager::willNeedVerticalScrollbar() const -> bool {
  if (!m_mediaScrollArea) {
    return false;
  }
  return m_metrics.totalHeight > m_mediaScrollArea->viewport()->height();
}

auto ScrollManager::getTotalItems() const -> int { return m_totalItems; }

void ScrollManager::enforceScrollContentConstraints() {
  if ((!m_gridContainer) || (!m_mediaScrollArea)) {
    return;
  }
  // Use totalHeight (clamped to Qt's QWIDGETSIZE_MAX) for container.
  // Scrollbar stays at clamped range - scroll scaling maps to logical
  // positions.
  m_gridContainer->setMinimumHeight(m_metrics.totalHeight);
  m_gridContainer->setMaximumHeight(m_metrics.totalHeight);
}

void ScrollManager::recreateLayout() {
  if (m_dataManager->filePaths().isEmpty() &&
      m_dataManager->subcollections().isEmpty()) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  bool isListMode = (m_context.config.viewType == ViewType::List);
  int fontSize =
      isListMode ? m_context.config.listFontSize : m_context.config.fontSize;
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (!widget) {
      continue;
    }
    widget->setHideTitles(m_context.config.hideTitles);
    widget->setHideSubcollectionTitles(
        m_context.config.hideSubcollectionTitles);
    widget->setFontSize(fontSize);
    widget->setCornerRadius(m_context.config.cornerRadius);
    widget->setItemDimensions(m_metrics.itemWidth, m_metrics.itemHeight);
    QPoint position = getItemPosition(it.key());
    widget->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                        m_metrics.itemHeight);
  }
  updateVirtualView();
}

void ScrollManager::centerHorizontalScrollbar(
    int /*currentCollectionIndex*/,
    const QList<CollectionConfig> & /*collections*/) {
  positionVirtualContainer();
}

void ScrollManager::handleLayoutChange() {
  if (m_destroying || QApplication::closingDown()) {
    return;
  }

  // Release all active widgets back to the pool - they need to be recreated
  // because layout changes (especially view type changes) require fresh widgets
  // with different configurations (e.g., list mode has no image label)
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (widget && m_widgetPool) {
      m_widgetPool->release(widget);
    }
  }
  m_activeWidgets.clear();

  calculateVirtualMetrics();

  // Update factory with new metrics before creating widgets
  // Critical for view type changes where item dimensions differ significantly
  if (m_widgetFactory) {
    m_widgetFactory->setMetrics(m_metrics.itemWidth, m_metrics.itemHeight);
  }

  positionVirtualContainer();
  updateVirtualView();

  // Clear user scroll state after layout change - scroll position changes
  // during resize should not block subsequent programmatic centering
  if (m_state) {
    m_state->scroll().userScrollActive = false;
  }
}

void ScrollManager::notifyUserActivity() {
  if (m_artworkManager) {
    m_artworkManager->updateUserActivity();
  }
}

auto ScrollManager::getCurrentGridWidth() const -> int {
  return m_context.config.gridWidth;
}

void ScrollManager::updateContextForSubcollection(int subcollectionIndex) {
  if ((!m_collections) || subcollectionIndex < 0 ||
      subcollectionIndex >= m_collections->size()) {
    return;
  }
  m_context.currentIndex = subcollectionIndex;
  m_context.config = (*m_collections)[subcollectionIndex];
  // Reinitialize subcollections for the new context
  m_dataManager->initializeSubcollections(m_context, m_collections,
                                          m_hierarchyCache);
  m_totalItems = m_dataManager->totalItemCount();
  calculateVirtualMetrics();
  // Update factory metrics after calculation - critical for list mode where
  // itemWidth depends on viewport width
  if (m_widgetFactory) {
    m_widgetFactory->setCollectionContext(m_context);
    m_widgetFactory->setMetrics(m_metrics.itemWidth, m_metrics.itemHeight);
  }
  positionVirtualContainer();
  updateVirtualView();
}

/* Apply subcollection filter showing only items belonging to the selected
 * subcollection (and its descendants) */
// Applies filtering to show only items belonging to specified subcollection
void ScrollManager::applySubcollectionFilter(int subcollectionIndex) {
  if ((!m_collections) || subcollectionIndex < 0 ||
      subcollectionIndex >= m_collections->size()) {
    return;
  }
  if (m_dataManager->filePaths().isEmpty() &&
      m_dataManager->subcollections().isEmpty()) {
    return;
  }

  if (m_context.currentIndex != subcollectionIndex) {
    updateContextForSubcollection(subcollectionIndex);
  }

  if (!m_filterManager) {
    return;
  }

  // Update FilterManager's source data and apply subcollection filter
  m_filterManager->setSourceData(
      m_dataManager->filePaths(), m_dataManager->fileNames(),
      m_dataManager->filePathToDisplayName(), m_dataManager->subcollections());
  m_filterManager->setContext(m_context);
  m_filterManager->applySubcollectionFilter(subcollectionIndex);

  rebuildFilteredView();
}

void ScrollManager::rebuildFilteredView() {
  m_totalItems = m_filterManager ? m_filterManager->filteredCount()
                                 : m_dataManager->subcollectionCount() +
                                       m_dataManager->fileCount();
  calculateVirtualMetrics();
  positionVirtualContainer();

  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (it.value()) {
      it.value()->hide();
      it.value()->deleteLater();
    }
  }
  m_activeWidgets.clear();

  if ((m_mediaScrollArea) && (m_mediaScrollArea->verticalScrollBar())) {
    m_mediaScrollArea->verticalScrollBar()->setValue(0);
  }

  // FilterManager emits filterChanged, no need to emit here
  updateVirtualView();
  enforceScrollContentConstraints();
}

auto ScrollManager::getEffectiveViewportWidth() const -> int {
  if (!m_mediaScrollArea) {
    return 0;
  }
  int viewportWidth = m_mediaScrollArea->viewport()->width();
  return qMax(UIConstants::Scroll::MIN_EFFECTIVE_VIEWPORT_WIDTH,
              viewportWidth);
}

void ScrollManager::recalculateContainerMetrics() {
  if (!m_virtualContainer) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  updateVirtualView();
}

void ScrollManager::forceVirtualViewUpdate() {
  calculateVirtualMetrics();
  positionVirtualContainer();
  updateVirtualView();
}

void ScrollManager::preCalculateLayout() {
  calculateVirtualMetrics();
  positionVirtualContainer();
}

// Create the virtual container without showing it immediately
void ScrollManager::createVirtualContainer() {
  if (!m_containerManager) {
    return;
  }

  m_containerManager->createContainer();
  m_virtualContainer = m_containerManager->container();

  connectScrollEvents();
  positionVirtualContainer();

  // Create or update list header for list view mode
  updateListHeader();
}

// Forwarder: list-header rendering lives on SelectionDisplayManager. Sync the
// virtual container pointer first since it changes across collection reloads.
void ScrollManager::updateListHeader() {
  if (!m_selectionDisplay) {
    return;
  }
  m_selectionDisplay->setVirtualContainer(m_virtualContainer);
  m_selectionDisplay->updateListHeader();
}

// Forwarder: artwork preview overlay lives on SelectionDisplayManager.
void ScrollManager::onArtworkPreviewRequested(const QString &filePath,
                                              const QString &artworkDir) {
  if (m_selectionDisplay) {
    m_selectionDisplay->showArtworkPreview(filePath, artworkDir);
  }
}

// Prime the container with target collection metrics before items are loaded
void ScrollManager::primeLayoutFor(const CollectionConfig &config) {
  if ((!m_gridContainer) || (!m_mediaScrollArea)) {
    return;
  }
  m_context.config = config;
  // Update factory context so new widgets get correct settings (corner radius,
  // etc.)
  if (m_widgetFactory) {
    m_widgetFactory->setCollectionContext(m_context);
  }
  int savedTotal = m_totalItems;
  m_totalItems = 0;
  calculateVirtualMetrics();
  // Update factory metrics after calculation - critical for list mode where
  // itemWidth depends on viewport width calculated in calculateVirtualMetrics()
  if (m_widgetFactory) {
    m_widgetFactory->setMetrics(m_metrics.itemWidth, m_metrics.itemHeight);
  }
  if (!m_virtualContainer) {
    createVirtualContainer();
  }
  positionVirtualContainer();
  m_totalItems = savedTotal;
}

// Positions virtual scrolling container with alignment and overflow handling
void ScrollManager::positionVirtualContainer() {
  if (!m_containerManager || !m_virtualContainer) {
    return;
  }

  bool isFiltered = m_filterManager && m_filterManager->isFiltered();

  ContainerPositionParams params;
  params.totalWidth = m_metrics.totalWidth;
  params.totalHeight = m_metrics.totalHeight;
  params.itemsPerRow = m_metrics.itemsPerRow;
  params.totalItems = m_totalItems;
  params.alignment = getCurrentAlignment();
  params.isFiltered = isFiltered;

  m_containerManager->positionContainer(params);

  // Update list header position after container is positioned to ensure
  // header x-position matches the container's final position
  updateListHeader();
}

// Cleans up the virtual container and persistent selection overlay resources
void ScrollManager::cleanupVirtualContainer() {
  debugLog("cleanupVirtualContainer called!");
  if (m_containerManager) {
    m_containerManager->cleanupContainer();
  }
  m_virtualContainer = nullptr;
  // Note: Don't delete the list header here - it persists across collection
  // reloads and is parented to the viewport (not the virtual container)
}

void ScrollManager::calculateVirtualMetrics() {
  // Use GridLayoutCalculator for metrics calculation
  m_metrics =
      GridLayoutCalculator::calculateMetrics(m_context.config, m_totalItems);

  // List mode: set item width to fill available viewport (minus scrollbar and
  // margins)
  if (m_context.config.viewType == ViewType::List && m_mediaScrollArea) {
    int viewportWidth = m_mediaScrollArea->viewport()->width();
    int scrollbarWidth = getScrollbarWidth();
    // Full width minus margins and scrollbar
    m_metrics.itemWidth =
        viewportWidth - (m_metrics.margins * 2) - scrollbarWidth;
    m_metrics.totalWidth = viewportWidth - scrollbarWidth;
    m_metrics.actualGridWidth = m_metrics.itemWidth + (m_metrics.margins * 2);
  }

  // Shrink the virtual container width when the entire grid fits in a
  // single partial row. Without this, totalWidth always reflects a full
  // itemsPerRow-wide row, so a centered container ends up looking left-
  // aligned because the partial widgets occupy slots 0..N-1 of a wider
  // block. Applies to both naturally small collections (e.g. a parent
  // showing only 3 subcollections at gridWidth=7) and search-filtered
  // result sets.
  if (m_totalItems > 0 && m_totalItems < m_metrics.itemsPerRow) {
    m_metrics = GridLayoutCalculator::adjustForFilter(m_metrics, m_totalItems);
  }

  // Note: updateListHeader() is called separately AFTER
  // positionVirtualContainer() to ensure header x-position matches the
  // container's final position
}

// Connects scrollbars to update logic and sets user scroll activity properties
void ScrollManager::connectScrollEvents() {
  if (m_scrollEventHandler) {
    m_scrollEventHandler->connectEvents();
  }
}

void ScrollManager::disconnectScrollEvents() {
  if (m_scrollEventHandler) {
    m_scrollEventHandler->disconnectEvents();
  }
}

// Ensures a widget exists for the visual index; orders click handling to emit
// first so InteractionManager controls selection and scrolling
// Creates and positions widget for given visual index, handling both
// subcollections and media items
void ScrollManager::ensureWidgetForIndex(int visualIndex) {
  if (visualIndex < 0 || visualIndex >= m_totalItems) {
    return;
  }
  if (!m_virtualContainer) {
    return;
  }

  ItemWidget *existing = m_activeWidgets.value(visualIndex, nullptr);
  if (existing) {
    if (!existing->isVisible()) {
      existing->show();
    }
    bool isListMode = (m_context.config.viewType == ViewType::List);
    int fontSize =
        isListMode ? m_context.config.listFontSize : m_context.config.fontSize;
    existing->setHideTitles(m_context.config.hideTitles);
    existing->setHideSubcollectionTitles(
        m_context.config.hideSubcollectionTitles);
    existing->setFontSize(fontSize);
    existing->setCornerRadius(m_context.config.cornerRadius);
    existing->setItemDimensions(m_metrics.itemWidth, m_metrics.itemHeight);
    QPoint position = getItemPosition(visualIndex);
    existing->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                          m_metrics.itemHeight);
    return;
  }

  int actualIndex = getFilteredIndex(visualIndex);
  if (actualIndex < 0) {
    return;
  }

  int subCount = m_dataManager->subcollectionCount();
  int folderCount = m_dataManager->virtualFolderCount();
  ItemWidget *itemWidget = nullptr;

  if (m_widgetFactory) {
    if (actualIndex < subCount) {
      // Subcollection item
      int subcollectionIndex =
          m_dataManager->subcollectionIndexFromActual(actualIndex);
      itemWidget =
          m_widgetFactory->createSubcollectionWidget(subcollectionIndex);
    } else if (actualIndex < subCount + folderCount) {
      // Virtual folder item
      QString folderPath = m_dataManager->virtualFolderFromActual(actualIndex);
      itemWidget = m_widgetFactory->createVirtualFolderWidget(folderPath);
    } else {
      // Media item
      int mediaIndex = m_dataManager->mediaIndexFromActual(actualIndex);
      int collectionIndex = m_context.currentIndex;
      itemWidget =
          m_widgetFactory->createMediaWidget(mediaIndex, collectionIndex);
    }
  }

  if (itemWidget) {
    QPoint position = getItemPosition(visualIndex);
    itemWidget->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                            m_metrics.itemHeight);
    // Set row index for alternating background colors in list mode
    int rowIndex =
        GridUtils::computeItemRow(visualIndex, m_metrics.itemsPerRow);
    itemWidget->setRowIndex(rowIndex);
    itemWidget->show();

    // Restore selection state if this widget corresponds to the currently
    // selected index
    if (visualIndex == m_selectionState->committedSelectedIndex()) {
      itemWidget->setSelected(true);
    }

    // Connect artwork preview signal for list mode (use UniqueConnection to
    // avoid duplicates on widget reuse)
    connect(itemWidget, &ItemWidget::artworkPreviewRequested, this,
            &ScrollManager::onArtworkPreviewRequested, Qt::UniqueConnection);

    m_activeWidgets.insert(visualIndex, itemWidget);
  }
}

auto ScrollManager::getItemPosition(int visualIndex) const -> QPoint {
  bool isFiltered = m_filterManager && m_filterManager->isFiltered();
  QPoint pos = GridLayoutCalculator::getItemPosition(visualIndex, m_metrics,
                                                     isFiltered, m_totalItems);

  // For very large grids that exceed Qt's size limit, position widgets
  // relative to the viewport. The scrollbar is in clamped (widget) space,
  // but we calculate which rows are visible using logical scroll position.
  // Place widgets so they appear at the correct viewport-relative position.
  if (m_metrics.isClipped && m_mediaScrollArea) {
    int widgetScrollY = m_mediaScrollArea->verticalScrollBar()->value();
    int viewportHeight = m_mediaScrollArea->viewport()->height();
    // Convert clamped scroll position to logical scroll position with viewport
    // for precise endpoint mapping (scrollbar max reaches end of collection)
    int logicalScrollY =
        m_metrics.toLogicalScrollY(widgetScrollY, viewportHeight);
    // Widget appears at (logicalY - logicalScrollY) pixels from viewport top,
    // which means placing it at widgetScrollY + (logicalY - logicalScrollY)
    // in container coordinates
    int relativeY = widgetScrollY + (pos.y() - logicalScrollY);
    return QPoint(pos.x(), relativeY);
  }

  return pos;
}

void ScrollManager::reconfigureArtworkForActiveWidgets() {
  if (!m_widgetFactory || !m_artworkManager) {
    return;
  }

  // Re-configure artwork for active widgets that may not have gotten
  // artwork paths on initial creation (directories weren't cached yet).
  // Skip widgets that already have artwork to avoid redundant work.
  // Use forceDirectLookup=true since prewarm has warmed the OS dentry cache.
  int reconfigured = 0;
  int skipped = 0;
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (!widget) {
      continue;
    }
    // Skip if widget already has artwork loaded or pending
    if (m_artworkManager->hasArtworkForWidget(widget)) {
      ++skipped;
      continue;
    }
    QString filePath = widget->getFilePath();
    if (filePath.isEmpty()) {
      continue;
    }
    // Force direct lookup - prewarm has warmed the OS filesystem cache
    m_widgetFactory->configureArtworkForWidget(widget, filePath,
                                               /*forceDirectLookup=*/true);
    ++reconfigured;
  }

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    qWarning()
        << "[PerfTrace] reconfigureArtworkForActiveWidgets: reconfigured="
        << reconfigured << "skipped=" << skipped;
  }

  // Trigger viewport update to load the newly-configured artwork
  m_artworkManager->scheduleViewportUpdate();
}
// Returns the virtual folder path for a visual index, or empty string if not a
// virtual folder
auto ScrollManager::virtualFolderPathForVisualIndex(int visualIndex) const
    -> QString {
  int actualIndex = getFilteredIndex(visualIndex);
  return m_dataManager->virtualFolderFromActual(actualIndex);
}

// Returns the underlying path for a visual index; delegates to DatabaseManager
// for path resolution
auto ScrollManager::filePathForVisualIndex(int visualIndex) const -> QString {
  int actualIndex = getFilteredIndex(visualIndex);
  int mediaIndex = m_dataManager->mediaIndexFromActual(actualIndex);
  if (mediaIndex < 0) {
    return {};
  }

  const QString rawEntry = m_dataManager->rawFilePath(mediaIndex);
  if (rawEntry.isEmpty()) {
    return {};
  }

  if (!m_databaseManager) {
    return {};
  }
  return m_databaseManager->resolveFilePath(rawEntry, m_context);
}

