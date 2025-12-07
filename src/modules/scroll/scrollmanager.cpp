// Manages virtual scrolling, widget pooling, and grid layout for large item collections.
#include "scrollmanager.h"
#include "applicationcontext.h"
#include "arrowkeyscrollhelper.h"
#include "artworkmanager.h"
#include "databasemanager.h"
#include "filtermanager.h"
#include "gridlayoutcalculator.h"
#include "gridutils.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "itemwidgetfactory.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "scrolleventhandler.h"
#include "selectionstatetracker.h"
#include "selectioncoordinator.h"
#include "selectionoverlaymanager.h"
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
#include <QTimer>
#include <QWidget>
#include <algorithm>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcScrollManager, "kartend.scrollmanager")
#define debugLog(msg) qCDebug(lcScrollManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

// Initializes timers for throttle, arrow-key updates, and a short idle window
// to treat any scrollbar interaction as user-driven scrolling
ScrollManager::ScrollManager(QObject *parent) : QObject(parent) {
  // Widget pool for recycling ItemWidgets
  m_widgetPool = std::make_unique<WidgetPoolManager>(this);

  // Filter manager for search and subcollection filtering
  m_filterManager = std::make_unique<FilterManager>(this);
  connect(m_filterManager.get(), &FilterManager::filterChanged, this,
          &ScrollManager::filterChanged);

  // Selection overlay manager for glide animation
  m_overlayManager = std::make_unique<SelectionOverlayManager>(this);
  connect(m_overlayManager.get(), &SelectionOverlayManager::animationFinished, this,
          [this]() {
            // Update widget selection states when animation finishes
            if (m_selectionState->needsCommitUpdate(m_selectionState->lastSelectedIndex())) {
              if (auto *prevSel =
                      m_activeWidgets.value(m_selectionState->committedSelectedIndex(), nullptr)) {
                prevSel->setSelected(false);
              }
            }
            if (auto *newSel =
                    m_activeWidgets.value(m_selectionState->lastSelectedIndex(), nullptr)) {
              newSel->setSelected(true);
            }
            m_selectionState->commitSelection(m_selectionState->lastSelectedIndex());

            // Hide overlay if not in force-visible mode
            if (!m_overlayManager->shouldKeepVisible()) {
              m_overlayManager->hide();
            } else {
              m_overlayManager->raise();
            }
            updateVirtualView();
          });

  // Virtual container manager for container lifecycle
  m_containerManager = std::make_unique<VirtualContainerManager>(this);
  m_containerManager->setOverlayManager(m_overlayManager.get());

  // Selection coordinator for selection state and movement analysis
  m_selectionCoordinator = std::make_unique<SelectionCoordinator>(this);
  m_selectionCoordinator->setOverlayManager(m_overlayManager.get());

  // Scroll event handler for scroll event wiring
  m_scrollEventHandler = std::make_unique<ScrollEventHandler>(this);
  connect(m_scrollEventHandler.get(), &ScrollEventHandler::scrollChanged,
          this, &ScrollManager::onScrollChanged);
  connect(m_scrollEventHandler.get(), &ScrollEventHandler::userScrollEnded,
          this, &ScrollManager::updateVirtualView);

  // Item widget factory for creating and configuring widgets
  m_widgetFactory = std::make_unique<ItemWidgetFactory>(this);
  m_widgetFactory->setWidgetPool(m_widgetPool.get());
  connect(m_widgetFactory.get(), &ItemWidgetFactory::subcollectionDoubleClicked,
          this, &ScrollManager::onSubcollectionDoubleClicked);
  connect(m_widgetFactory.get(), &ItemWidgetFactory::virtualFolderDoubleClicked,
          this, &ScrollManager::onVirtualFolderDoubleClicked);
  connect(m_widgetFactory.get(), &ItemWidgetFactory::requestItemsRange,
          this, &ScrollManager::requestItemsRange);

  // Arrow key scroll helper for centering animation
  m_arrowKeyScrollHelper = std::make_unique<ArrowKeyScrollHelper>(this);
  connect(m_arrowKeyScrollHelper.get(), &ArrowKeyScrollHelper::requestViewUpdate,
          this, &ScrollManager::updateVirtualView);

  // Data manager for file paths, file names, subcollections, and virtual folders
  m_dataManager = std::make_unique<ScrollDataManager>(this);

  // Pre-search state manager for fast search result restoration
  m_preSearchStateManager = std::make_unique<PreSearchStateManager>(this);

  // Selection state tracker for selection indices, direction, and row
  m_selectionState = std::make_unique<SelectionStateTracker>();

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
  m_userScrollIdleTimer = new TimerUtils::DebouncedTimer(UIConstants::Mouse::USER_SCROLL_IDLE_TIMER_MS, this);
  connect(m_userScrollIdleTimer, &TimerUtils::DebouncedTimer::triggered, this,
          [this]() { 
            if (m_scrollEventHandler) {
              m_scrollEventHandler->setUserScrollActive(false);
            }
          });

  // Prewarm timer - replenishes widget pool after scroll activity settles
  m_prewarmIdleTimer = new TimerUtils::DebouncedTimer(UIConstants::Widget::Pool::PREWARM_IDLE_MS, this);
  connect(m_prewarmIdleTimer, &TimerUtils::DebouncedTimer::triggered, this,
          [this]() {
            if (m_widgetPool) {
              // Prune stale widgets that weren't reused during collection switch
              // This reclaims memory from the soft-cleared pool
              m_widgetPool->pruneStaleWidgets();
              
              int visibleRows = (getLastVisibleRow() - getFirstVisibleRow()) + 1;
              m_widgetPool->setVisibleMetrics(visibleRows, m_metrics.itemsPerRow);
              m_widgetPool->prewarm();
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
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, QWidget*, GridContainer, gridContainer)
SETUP_GETTER_DEF(ScrollManagerSetup, QScrollArea*, MediaScrollArea, mediaScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, ArtworkManager*, ArtworkManager, artworkManager)
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, const QList<CollectionConfig>*, Collections, collections)
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, const CollectionHierarchyCache*, HierarchyCache, hierarchyCache)
SETUP_GETTER_DEF_CTX_ONLY(ScrollManagerSetup, InteractionStateHolder*, InteractionState, interactionState)
SETUP_GETTER_DEF_SAME(ScrollManagerSetup, const GeneralSettings*, GeneralSettings, generalSettings)

void ScrollManager::setupReferences(const ScrollManagerSetup &setup) {
  m_generalSettings = setup.getGeneralSettings();
  m_state = setup.getInteractionState();
  m_gridContainer = setup.getGridContainer();
  m_mediaScrollArea = setup.getMediaScrollArea();
  m_artworkManager = setup.getArtworkManager();
  m_collections = setup.getCollections();
  m_hierarchyCache = setup.getHierarchyCache();

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
  
  initializeSubcollections();
  initializeVirtualFolders();
  
  if (!m_context.filePaths.isEmpty()) {
    // Preloaded data from context - copy to data manager
    m_dataManager->filePaths() = m_context.filePaths;
    m_dataManager->fileNames() = m_context.fileNames;
  } else {
    // On-demand loading - initialize storage with placeholder count
    int itemCount = totalCount - m_dataManager->subcollectionCount();
    if (itemCount < 0) {
      itemCount = 0;
    }
    m_dataManager->initializeStorage(itemCount);
  }

  m_totalItems = m_dataManager->totalItemCount();
  
  if (m_totalItems == 0) {
    setupEmptyVirtualScrolling();
    return;
  }

  setupNormalVirtualScrolling();
}

void ScrollManager::receiveItemsRange(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames) {
    if (offset < 0 || offset >= m_dataManager->fileCount()) return;
    
    // Store data and get visual indices that were updated
    QList<int> updatedIndices = m_dataManager->receiveItemsRange(offset, filePaths, fileNames);
    
    // Release placeholder widgets so they get re-created with actual data
    for (int visualIndex : updatedIndices) {
        if (ItemWidget *widget = m_activeWidgets.value(visualIndex, nullptr)) {
            releaseWidget(widget);
            m_activeWidgets.remove(visualIndex);
        }
    }
    
    // Trigger update of visible widgets
    updateVirtualView();
}

void ScrollManager::initializeSubcollections() {
  m_dataManager->initializeSubcollections(m_context, m_collections, m_hierarchyCache);
}

void ScrollManager::initializeVirtualFolders() {
  m_dataManager->initializeVirtualFolders(m_context);
}

void ScrollManager::setupFilePathMappings() {
  m_dataManager->setupFilePathMappings(m_context);
}

// processRelativeFilePaths is no longer needed - DatabaseManager now handles
// relative path resolution via resolveFilePath() and resolveRelativeFilePath()

void ScrollManager::setupEmptyVirtualScrolling() {
  calculateVirtualMetrics();
  createVirtualContainer();
  if (m_virtualContainer) {
    m_virtualContainer->setVisible(true);
  }
  emit virtualScrollSetupComplete();
}

void ScrollManager::setupNormalVirtualScrolling() {
  calculateVirtualMetrics();
  createVirtualContainer();

  // Configure widget factory with current context and metrics
  if (m_widgetFactory) {
    m_widgetFactory->setDatabaseManager(m_databaseManager);
    m_widgetFactory->setParentWidget(m_virtualContainer);
    m_widgetFactory->setCollectionContext(m_context);
    m_widgetFactory->setMetrics(m_metrics.itemWidth, m_metrics.itemHeight);
    m_widgetFactory->setFileData(&m_dataManager->filePaths(), &m_dataManager->fileNames());
    m_widgetFactory->setSubcollectionNameResolver(
        [this](int idx) { return getSubcollectionName(idx); });
  }

  // Pre-position scrollbar BEFORE creating widgets to prevent visual jump
  // where list briefly shows at position 0 then jumps to remembered position
  if (m_initialScrollIndex >= 0 && m_mediaScrollArea) {
    if (QScrollBar *scrollbar = m_mediaScrollArea->verticalScrollBar()) {
      int viewportHeight = m_mediaScrollArea->viewport()->height();
      // Calculate scroll max from metrics rather than scrollbar->maximum()
      // because Qt may not have processed the container resize yet
      int scrollMax = qMax(0, m_metrics.totalHeight - viewportHeight);
      int itemY = GridUtils::computeItemY(m_initialScrollIndex, m_metrics.itemsPerRow,
                                          m_metrics.itemHeight, m_metrics.verticalSpacing,
                                          UIConstants::Grid::MARGINS);
      int targetY = itemY + (m_metrics.itemHeight / 2) - (viewportHeight / 2);
      targetY = qBound(0, targetY, scrollMax);
      // Set scrollbar range explicitly before setting value to ensure it takes effect
      scrollbar->setRange(0, scrollMax);
      scrollbar->setValue(targetY);
    }
    m_initialScrollIndex = -1;  // Reset after use
  }

  updateVirtualView();
  positionVirtualContainer();
  if (m_virtualContainer) {
    m_virtualContainer->setVisible(true);
  }
  
  // Pre-warm widget pool during initial setup for smoother scrolling
  if (m_widgetPool) {
    int visibleRows = (getLastVisibleRow() - getFirstVisibleRow()) + 1;
    m_widgetPool->setVisibleMetrics(visibleRows, m_metrics.itemsPerRow);
    m_widgetPool->prewarm();
  }
  
  emit virtualScrollSetupComplete();
}

void ScrollManager::cleanup() {
  if (m_destroying) {
    return;
  }
  bool hasPreSearch = m_preSearchStateManager && m_preSearchStateManager->hasSavedState();
  if (m_activeWidgets.isEmpty() && (!m_virtualContainer) &&
      m_dataManager->filePaths().isEmpty() && m_dataManager->subcollections().isEmpty() &&
      !hasPreSearch) {
    return;
  }

  disconnectScrollEvents();

  // Clear artwork widget references FIRST - prevents stale widget pointers
  // from causing incorrect artwork or crashes when widgets are destroyed
  if (m_artworkManager) {
    m_artworkManager->clearWidgetReferences();
  }

  // Explicitly delete active widgets before clearing the pool
  // This ensures proper cleanup order and prevents use-after-free
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      delete widget;
    }
  }
  m_activeWidgets.clear();

  // DO NOT delete pre-search widgets here - they are reparented to m_gridContainer
  // and will be restored when search is cleared. Only delete them on full cleanup
  // (when m_destroying is true, which returns early above)

  // Clear the widget pool - if we have pre-search widgets, use soft clear
  // to allow potential reuse when search is cleared
  // Otherwise use clearAndDelete for explicit cleanup
  if (m_widgetPool) {
    if (hasPreSearch) {
      // Pre-search widgets exist - soft clear allows widget reuse
      m_widgetPool->softClear();
    } else {
      // Full cleanup - use soft clear to allow reuse during next collection load
      // The prewarm timer will prune unused stale widgets during idle
      m_widgetPool->softClear();
    }
  }

  cleanupVirtualContainer();
  if (m_filterManager) {
    m_filterManager->clearFilter();
  }
  m_dataManager->clear();
  m_totalItems = 0;
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
    return;
  }
  if (m_metrics.itemsPerRow <= 0) {
    return;
  }

  QSet<int> needed = calculateNeededIndices();

  for (int visualIndex : needed) {
    ensureWidgetForIndex(visualIndex);
  }

  removeUnneededWidgets(needed);
  updateArtworkIfAllowed();

  if (m_overlayManager) {
    if (m_overlayManager->isForceVisible() && m_selectionState->hasSelection()) {
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

// Performs arrow key recentering unless suppressed by user scroll or timing
// properties
// Handles arrow key scroll animation to center selected item
void ScrollManager::onArrowKeyViewUpdate() {
  if (!m_arrowKeyScrollHelper || !m_virtualContainer ||
      !m_selectionState->hasSelection() || 
      m_selectionState->lastSelectedIndex() >= m_totalItems) {
    return;
  }

  // Update metrics for the helper
  m_arrowKeyScrollHelper->setItemMetrics(
      m_metrics.itemHeight, m_metrics.verticalSpacing, m_metrics.margins);

  // Delegate to helper with position callback
  m_arrowKeyScrollHelper->performUpdate(
      m_selectionState->lastSelectedIndex(), m_totalItems, m_metrics.itemsPerRow,
      [this](int idx) { return getItemPosition(idx).y(); });
}

auto ScrollManager::selectionOverlayRectForIndex(int visualIndex) const
    -> QRect {
  // Delegate to coordinator if available (shares the same calculation)
  if (m_selectionCoordinator) {
    return m_selectionCoordinator->rectForIndex(visualIndex, m_totalItems);
  }
  
  // Fallback for when coordinator is not configured
  if (visualIndex < 0 || visualIndex >= m_totalItems) {
    return {};
  }
  QPoint pos = getItemPosition(visualIndex);
  return SelectionOverlayManager::overlayRectForPosition(
      pos, m_metrics.itemWidth, m_metrics.itemHeight);
}

void ScrollManager::refreshSelectionOverlayState() {
  if (!m_overlayManager) {
    return;
  }
  
  debugLog(QString("refreshSelectionOverlayState: lastSelectedIndex=%1 forceVisible=%2")
           .arg(m_selectionState->lastSelectedIndex()).arg(m_overlayManager->isForceVisible()));
  
  if (!m_overlayManager->shouldKeepVisible()) {
    debugLog("  HIDING overlay (not force visible)");
    m_overlayManager->hide();

    bool glideWasActive = m_state && m_state->glideAnimating();
    if (m_state) {
      m_state->setGlideAnimating(false);
    }

    if (glideWasActive && m_selectionState->hasSelection()) {
      ensureWidgetForIndex(m_selectionState->lastSelectedIndex());
      if (auto *selectedWidget =
              m_activeWidgets.value(m_selectionState->lastSelectedIndex(), nullptr)) {
        debugLog("  requesting widget repaint for selection border");
        selectedWidget->update();
      }
    }
    return;
  }

  // Don't interfere with an ongoing selection overlay animation
  if (m_overlayManager->isAnimating()) {
    debugLog("  animation running, just show/raise");
    m_overlayManager->raise();
    return;
  }

  // During click-hold, we respect the current visibility state (managed by move handlers)
  // If visible, keep it on top. If hidden (e.g. wrapped row), keep it hidden.
  if (m_overlayManager->isForceVisible()) {
    if (m_overlayManager->isVisible()) {
      debugLog("  click-hold mode, raising visible overlay");
      m_overlayManager->raise();
    } else {
      debugLog("  click-hold mode, overlay hidden (respecting state)");
    }
    return;
  }

  if (!m_selectionState->hasSelection()) {
    return;
  }

  // Position overlay directly (non-click-hold case or initial positioning)
  QRect rect = selectionOverlayRectForIndex(m_selectionState->lastSelectedIndex());
  debugLog(QString("  setting geometry to rect: x=%1 y=%2 w=%3 h=%4")
           .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()));
  if (rect.isValid()) {
    m_overlayManager->showAtRect(rect);
  }
}

void ScrollManager::setForceSelectionOverlayVisible(bool force) {
  debugLog(QString("setForceSelectionOverlayVisible: %1").arg(force));
  if (!m_overlayManager) {
    return;
  }
  if (m_overlayManager->isForceVisible() == force) {
    return;
  }
  m_overlayManager->setForceVisible(force);
  refreshSelectionOverlayState();
}

void ScrollManager::handleHorizontalMoveAnimation(int selectedIndex,
                                                  int prevIndex) {
  if (!m_overlayManager) {
    return;
  }
  
  debugLog(QString("handleHorizontalMoveAnimation: prev=%1 sel=%2 forceVisible=%3")
           .arg(prevIndex).arg(selectedIndex).arg(m_overlayManager->isForceVisible()));

  // Calculate target rect for the new selection
  QRect targetRect = selectionOverlayRectForIndex(selectedIndex);
  if (!targetRect.isValid()) {
    ensureWidgetForIndex(selectedIndex);
    if (auto *w = m_activeWidgets.value(selectedIndex, nullptr)) {
      targetRect = SelectionOverlayManager::overlayRectForWidget(w);
    }
  }
  if (!targetRect.isValid()) {
    debugLog("  targetRect invalid, returning");
    return;
  }

  debugLog(QString("  targetRect: x=%1 y=%2").arg(targetRect.x()).arg(targetRect.y()));

  // Get start rect from previous selection if overlay not visible
  QRect startRect;
  if (!m_overlayManager->isVisible()) {
    startRect = selectionOverlayRectForIndex(prevIndex);
    if (!startRect.isValid()) {
      ensureWidgetForIndex(prevIndex);
      if (auto *w = m_activeWidgets.value(prevIndex, nullptr)) {
        startRect = SelectionOverlayManager::overlayRectForWidget(w);
      }
    }
  }

  // Update widget selection states
  if (m_selectionState->needsCommitUpdate(selectedIndex)) {
    if (auto *prevWidget = m_activeWidgets.value(m_selectionState->committedSelectedIndex(), nullptr)) {
      prevWidget->setSelected(false);
    }
  }
  ensureWidgetForIndex(selectedIndex);
  if (auto *currWidget = m_activeWidgets.value(selectedIndex, nullptr)) {
    currWidget->setSelected(true);
  }
  m_selectionState->commitSelection(selectedIndex);

  // Animate to target
  m_overlayManager->animateTo(targetRect, startRect);
  
  debugLog(QString("  animation started to (%1,%2)")
           .arg(targetRect.x()).arg(targetRect.y()));
}

void ScrollManager::handleDirectSelectionUpdate(int selectedIndex) {
  bool keepOverlay = m_overlayManager && m_overlayManager->shouldKeepVisible();
  
  if (m_overlayManager && m_overlayManager->isVisible()) {
    m_overlayManager->stopAnimation();
    if (!keepOverlay) {
      m_overlayManager->hide();
      if (m_state) {
        m_state->setGlideAnimating(false);
      }
    }
  }

  if (m_selectionState->needsCommitUpdate(selectedIndex)) {
    if (auto *prevSel =
            m_activeWidgets.value(m_selectionState->committedSelectedIndex(), nullptr)) {
      prevSel->setSelected(false);
    }
  }
  ensureWidgetForIndex(selectedIndex);
  auto *currSel = m_activeWidgets.value(selectedIndex, nullptr);
  if (currSel) {
    currSel->setSelected(true);
    // Force update to ensure border is drawn if we are in widget-border mode
    currSel->update();
  }
  
  if (keepOverlay) {
    // For direct updates (non-gliding) during click-hold (e.g. row wrap),
    // use widget border instead of overlay to avoid visual glitches.
    // This ensures we always have a visible selection indicator.
    if (m_state) {
      m_state->setGlideAnimating(false);
    }
    if (m_overlayManager) {
      m_overlayManager->hide();
      debugLog("  handleDirectSelectionUpdate: hiding overlay, using widget border");
    }
  }
  m_selectionState->commitSelection(selectedIndex);
}

void ScrollManager::prewarmSurroundingWidgets(int selectedIndex) {
  const int itemsPerRow =
      (m_metrics.itemsPerRow > 0 ? m_metrics.itemsPerRow : 1);
  const int prewarmRows =
      (UIConstants::Grid::BUFFER_ROWS > 0 ? UIConstants::Grid::BUFFER_ROWS : 2);
  const int halfWindow = prewarmRows * itemsPerRow;
  int start = std::max(0, selectedIndex - halfWindow);
  int end = std::min(m_totalItems - 1, selectedIndex + halfWindow);
  for (int visualIndex = start; visualIndex <= end; ++visualIndex) {
    ensureWidgetForIndex(visualIndex);
  }
}

void ScrollManager::scheduleArrowKeyUpdate(int selectedIndex) {
  if (!m_arrowKeyScrollHelper || !m_arrowKeyViewUpdateTimer) {
    return;
  }

  // Check if update should be skipped due to suppression
  if (m_arrowKeyScrollHelper->shouldSkipUpdate()) {
    return;
  }

  bool extendedHold = m_state && m_state->arrow().arrowKeyScrolling;
  static constexpr int ARROW_KEY_UPDATE_DELAY_EXTENDED_MS = 16;
  static constexpr int ARROW_KEY_UPDATE_DELAY_NORMAL_MS = 8;
  const int delayMs = extendedHold ? ARROW_KEY_UPDATE_DELAY_EXTENDED_MS
                                   : ARROW_KEY_UPDATE_DELAY_NORMAL_MS;
  m_arrowKeyViewUpdateTimer->start(delayMs);
  
  Q_UNUSED(selectedIndex)
}

// Updates selection visuals and manages prewarming, overlay animation, and
// arrow-centering properties
void ScrollManager::updateSelectionForIndex(int selectedIndex) {
#ifdef KARTEND_DEBUG_LOGGING
  bool forceVisible = m_overlayManager && m_overlayManager->isForceVisible();
  debugLog(QString("updateSelectionForIndex: sel=%1 lastSel=%2 forceVisible=%3")
           .arg(selectedIndex).arg(m_selectionState->lastSelectedIndex()).arg(forceVisible));
#endif
  
  if (m_destroying || (!m_mediaScrollArea) || selectedIndex < 0 ||
      selectedIndex >= m_totalItems) {
    debugLog("  early return due to invalid state");
    return;
  }

  int prevIndex = m_selectionState->lastSelectedIndex();
  const bool sameSelection = (prevIndex == selectedIndex);

  if (!sameSelection) {
    updateSelectionDirection(selectedIndex, prevIndex);
  }

  prewarmSurroundingWidgets(selectedIndex);

  // Ensure widget exists and get reference
  ensureWidgetForIndex(selectedIndex);
  ItemWidget *currentWidget = m_activeWidgets.value(selectedIndex, nullptr);
  
  const bool keepOverlay = m_overlayManager && m_overlayManager->shouldKeepVisible();
  
  if (!currentWidget) {
    handleMissingWidgetSelection(selectedIndex, keepOverlay);
    return;
  }

  if (sameSelection) {
    handleSameSelectionUpdate(selectedIndex, currentWidget, keepOverlay);
    return;
  }

  handleNewSelectionUpdate(selectedIndex, prevIndex, currentWidget);
  scheduleArrowKeyUpdate(selectedIndex);
}

void ScrollManager::updateSelectionDirection(int selectedIndex, int prevIndex) {
  m_selectionState->updateForNewSelection(selectedIndex, prevIndex, m_metrics.itemsPerRow);
  debugLog(QString("  updated lastSelectedIndex to %1").arg(selectedIndex));
}

void ScrollManager::handleMissingWidgetSelection(int selectedIndex, bool keepOverlay) {
  debugLog(QString("  currentWidget is null for index %1").arg(selectedIndex));
  // Widget not available, but during click-hold we still update overlay position
  if (keepOverlay && m_overlayManager) {
    QRect rect = selectionOverlayRectForIndex(selectedIndex);
    if (rect.isValid()) {
      m_overlayManager->showAtRect(rect);
      debugLog(QString("  positioned overlay directly at (%1,%2)").arg(rect.x()).arg(rect.y()));
    }
  }
}

void ScrollManager::handleSameSelectionUpdate(int selectedIndex, ItemWidget *currentWidget, bool keepOverlay) {
  debugLog("  same selection, returning");
  
  // If an animation is running, we MUST let it finish to achieve the "glide" effect.
  // Interrupting it here would cause the overlay to disappear and the widget border 
  // to snap back immediately, defeating the purpose of the animation.
  if (m_overlayManager && m_overlayManager->isAnimating()) {
    debugLog("  animation running during same-selection update - letting it continue");
    if (!m_overlayManager->isVisible()) {
      m_overlayManager->raise();
    }
    return;
  }

  if (keepOverlay && m_overlayManager) {
    m_overlayManager->showAtWidget(currentWidget);
    if (!m_overlayManager->isVisible()) {
      // Fallback to index-based positioning if widget-based failed
      QRect rect = selectionOverlayRectForIndex(selectedIndex);
      if (rect.isValid()) {
        m_overlayManager->showAtRect(rect);
      }
    }
  } else if (m_overlayManager && m_overlayManager->isVisible()) {
    m_overlayManager->hide();
  }
  
  if (m_selectionState->needsCommitUpdate(selectedIndex)) {
    if (auto *prevSel = m_activeWidgets.value(m_selectionState->committedSelectedIndex(), nullptr)) {
      prevSel->setSelected(false);
    }
  }
  currentWidget->setSelected(true);
  m_selectionState->commitSelection(selectedIndex);
  
  if (m_state && !keepOverlay) {
    m_state->setGlideAnimating(false);
  }
  scheduleArrowKeyUpdate(selectedIndex);
}

void ScrollManager::handleNewSelectionUpdate(int selectedIndex, int prevIndex, ItemWidget *currentWidget) {
  // Use coordinator for movement analysis
  bool isHorizontalMove = false;
  if (m_selectionCoordinator) {
    auto movement = m_selectionCoordinator->analyzeMovement(
        selectedIndex, prevIndex, m_metrics.itemsPerRow);
    isHorizontalMove = movement.isHorizontal;
  } else {
    calculateMovementDirection(selectedIndex, prevIndex, m_metrics.itemsPerRow,
                               isHorizontalMove);
  }
  
  debugLog(QString("  isHorizontalMove=%1").arg(isHorizontalMove));

  if (isHorizontalMove) {
    handleHorizontalMoveAnimation(selectedIndex, prevIndex);
  } else {
    handleDirectSelectionUpdate(selectedIndex);
  }
  
  Q_UNUSED(currentWidget)
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
  if (m_filterManager) {
    m_filterManager->setDatabaseManager(manager);
  }
}

void ScrollManager::recenterVirtualContainer() { positionVirtualContainer(); }

auto ScrollManager::getCurrentAlignment() const -> HorizontalAlignment {
  if ((!m_collections) || m_context.currentIndex < 0 ||
      m_context.currentIndex >= m_collections->size()) {
    return HorizontalAlignment::Center;
  }
  return (*m_collections)[m_context.currentIndex].horizontalAlignment;
}

void ScrollManager::applyFilter(const QString &searchText) {
  if (!m_filterManager) {
    return;
  }

  // Update FilterManager's source data before applying filter
  m_filterManager->setSourceData(m_dataManager->filePaths(), m_dataManager->fileNames(),
                                  m_dataManager->filePathToDisplayName(), m_dataManager->subcollections());
  m_filterManager->setContext(m_context);
  m_filterManager->applyFilter(searchText);

  // Update local state from FilterManager
  m_totalItems = m_filterManager->isFiltered()
                     ? m_filterManager->filteredCount()
                     : m_dataManager->subcollectionCount() + m_dataManager->fileCount();

  calculateVirtualMetrics();
  positionVirtualContainer();

  // Release widgets back to pool for reuse instead of just hiding
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      releaseWidget(widget);
    }
  }
  m_activeWidgets.clear();

  if (m_mediaScrollArea && m_mediaScrollArea->verticalScrollBar()) {
    m_mediaScrollArea->verticalScrollBar()->setValue(0);
  }

  updateVirtualView();
}

void ScrollManager::cleanupActiveWidgets() {
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      releaseWidget(widget);
    }
  }
  m_activeWidgets.clear();
}

void ScrollManager::savePreSearchState() {
  if (m_preSearchStateManager) {
    m_preSearchStateManager->saveState(m_activeWidgets);
  }
}

void ScrollManager::restorePreSearchState() {
  if (!m_preSearchStateManager || !m_preSearchStateManager->hasSavedState()) {
    return;
  }
  
  // Create position callback for widget repositioning
  auto getPositionFunc = [this](int index) -> QPoint {
    return getItemPosition(index);
  };
  
  m_preSearchStateManager->restoreState(
      m_activeWidgets,
      m_virtualContainer,
      m_widgetPool.get(),
      m_artworkManager,
      getPositionFunc,
      m_metrics.itemWidth,
      m_metrics.itemHeight);
}

auto ScrollManager::hasPreSearchState() const -> bool {
  return m_preSearchStateManager && m_preSearchStateManager->hasSavedState();
}

void ScrollManager::clearFilter() {
  if (!m_filterManager) {
    emit filterChanged(m_dataManager->fileCount(), m_dataManager->fileCount());
    return;
  }

  bool hasPreSearch = hasPreSearchState();
  
  if (!m_filterManager->isFiltered()) {
    // Filter not active, but we may still have pre-search widgets to restore
    if (hasPreSearch) {
      restorePreSearchState();
    }
    emit filterChanged(m_dataManager->fileCount(), m_dataManager->fileCount());
    return;
  }

  m_filterManager->clearFilter();
  m_totalItems = m_dataManager->totalItemCount();

  calculateVirtualMetrics();
  
  // Pre-set scroll position BEFORE positionVirtualContainer to prevent visual jump
  // where list briefly shows at position 0 then jumps to remembered position
  if (hasPreSearch && m_mediaScrollArea && m_preSearchStateManager) {
    if (QScrollBar *scrollbar = m_mediaScrollArea->verticalScrollBar()) {
      int viewportHeight = m_mediaScrollArea->viewport()->height();
      int scrollMax = qMax(0, m_metrics.totalHeight - viewportHeight);
      scrollbar->setRange(0, scrollMax);
      scrollbar->setValue(m_preSearchStateManager->savedScrollPosition());
    }
  }
  
  positionVirtualContainer();

  // Try to restore saved pre-search state for instant recovery
  if (hasPreSearch) {
    restorePreSearchState();
  } else {
    // Fallback: release widgets and rebuild view
    for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
      if (ItemWidget *widget = it.value()) {
        releaseWidget(widget);
      }
    }
    m_activeWidgets.clear();
    updateVirtualView();
  }
}

auto ScrollManager::getFilteredIndex(int visualIndex) const -> int {
  if (!m_filterManager || !m_filterManager->isFiltered()) {
    return visualIndex;
  }
  return m_filterManager->getActualIndex(visualIndex);
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
  m_gridContainer->setMinimumHeight(m_metrics.totalHeight);
  m_gridContainer->setMaximumHeight(m_metrics.totalHeight);
}

void ScrollManager::recreateLayout() {
  if (m_dataManager->filePaths().isEmpty() && m_dataManager->subcollections().isEmpty()) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (!widget) {
      continue;
    }
    widget->setHideTitles(m_context.config.hideTitles);
    widget->setHideSubcollectionTitles(
        m_context.config.hideSubcollectionTitles);
    widget->setFontSize(m_context.config.fontSize);
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
  calculateVirtualMetrics();
  positionVirtualContainer();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (!widget) {
      continue;
    }
    widget->setCornerRadius(m_context.config.cornerRadius);
    widget->setItemDimensions(m_metrics.itemWidth, m_metrics.itemHeight);
    QPoint position = getItemPosition(it.key());
    widget->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                        m_metrics.itemHeight);
  }
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
  m_dataManager->initializeSubcollections(m_context, m_collections, m_hierarchyCache);
  m_totalItems = m_dataManager->totalItemCount();
  calculateVirtualMetrics();
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
  if (m_dataManager->filePaths().isEmpty() && m_dataManager->subcollections().isEmpty()) {
    return;
  }

  if (m_context.currentIndex != subcollectionIndex) {
    updateContextForSubcollection(subcollectionIndex);
  }

  if (!m_filterManager) {
    return;
  }

  // Update FilterManager's source data and apply subcollection filter
  m_filterManager->setSourceData(m_dataManager->filePaths(), m_dataManager->fileNames(),
                                  m_dataManager->filePathToDisplayName(), m_dataManager->subcollections());
  m_filterManager->setContext(m_context);
  m_filterManager->applySubcollectionFilter(subcollectionIndex);

  rebuildFilteredView();
}

void ScrollManager::rebuildFilteredView() {
  m_totalItems = m_filterManager ? m_filterManager->filteredCount()
                                 : m_dataManager->subcollectionCount() + m_dataManager->fileCount();
  calculateVirtualMetrics();
  positionVirtualContainer();

  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (it.value()) {
      it.value()->hide();
      it.value()->deleteLater();
    }
  }
  m_activeWidgets.clear();

  if ((m_mediaScrollArea) &&
      (m_mediaScrollArea->verticalScrollBar())) {
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
  static constexpr int MIN_EFFECTIVE_VIEWPORT_WIDTH = 200;
  return qMax(MIN_EFFECTIVE_VIEWPORT_WIDTH, viewportWidth);
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
}

// Prime the container with target collection metrics before items are loaded
void ScrollManager::primeLayoutFor(const CollectionConfig &config) {
  if ((!m_gridContainer) || (!m_mediaScrollArea)) {
    return;
  }
  m_context.config = config;
  // Update factory context so new widgets get correct settings (corner radius, etc.)
  if (m_widgetFactory) {
    m_widgetFactory->setCollectionContext(m_context);
  }
  int savedTotal = m_totalItems;
  m_totalItems = 0;
  calculateVirtualMetrics();
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
}

// Cleans up the virtual container and persistent selection overlay resources
void ScrollManager::cleanupVirtualContainer() {
  debugLog("cleanupVirtualContainer called!");
  if (m_containerManager) {
    m_containerManager->cleanupContainer();
  }
  m_virtualContainer = nullptr;
}

void ScrollManager::calculateVirtualMetrics() {
  // Use GridLayoutCalculator for metrics calculation
  m_metrics = GridLayoutCalculator::calculateMetrics(m_context.config, m_totalItems);

  // Adjust for filtered view if needed
  bool isFiltered = m_filterManager && m_filterManager->isFiltered();
  if (isFiltered && m_totalItems > 0 && m_totalItems < m_metrics.itemsPerRow) {
    m_metrics = GridLayoutCalculator::adjustForFilter(m_metrics, m_totalItems);
  }
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
    existing->setHideTitles(m_context.config.hideTitles);
    existing->setHideSubcollectionTitles(
        m_context.config.hideSubcollectionTitles);
    existing->setFontSize(m_context.config.fontSize);
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
      int subcollectionIndex = m_dataManager->subcollectionIndexFromActual(actualIndex);
      itemWidget = m_widgetFactory->createSubcollectionWidget(subcollectionIndex);
    } else if (actualIndex < subCount + folderCount) {
      // Virtual folder item
      QString folderPath = m_dataManager->virtualFolderFromActual(actualIndex);
      itemWidget = m_widgetFactory->createVirtualFolderWidget(folderPath);
    } else {
      // Media item
      int mediaIndex = m_dataManager->mediaIndexFromActual(actualIndex);
      int collectionIndex = m_context.currentIndex;
      itemWidget = m_widgetFactory->createMediaWidget(mediaIndex, collectionIndex);
    }
  }

  if (itemWidget) {
    QPoint position = getItemPosition(visualIndex);
    itemWidget->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                            m_metrics.itemHeight);
    itemWidget->show();

    // Restore selection state if this widget corresponds to the currently selected index
    if (visualIndex == m_selectionState->committedSelectedIndex()) {
      itemWidget->setSelected(true);
    }

    m_activeWidgets.insert(visualIndex, itemWidget);
  }
}

auto ScrollManager::getItemPosition(int visualIndex) const -> QPoint {
  bool isFiltered = m_filterManager && m_filterManager->isFiltered();
  return GridLayoutCalculator::getItemPosition(
      visualIndex, m_metrics, isFiltered, m_totalItems);
}

// Handles scroll changes throttling artwork updates and arrow-centering
// suppression
void ScrollManager::onScrollChanged() {
  if (m_destroying) {
    return;
  }
  if (!m_scrollTimer) {
    return;
  }

  if (m_state && m_state->scroll().programmaticScroll) {
    handleProgrammaticScroll();
    return;
  }

  handleUserScroll();
  setupScrollSuppression();
  finalizeScrollChanges();
}

void ScrollManager::handleProgrammaticScroll() {
  notifyUserActivity();
  if (!m_scrollTimer->isActive()) {
    m_scrollTimer->start();
  }
}

void ScrollManager::handleUserScroll() {
  if (m_scrollEventHandler) {
    m_scrollEventHandler->setUserScrollActive(true);
  }
  if (m_userScrollIdleTimer) {
    m_userScrollIdleTimer->trigger();
  }

  // Schedule pool prewarm for when scroll activity settles
  if (m_prewarmIdleTimer) {
    m_prewarmIdleTimer->trigger();
  }

  if (m_state) {
    m_state->scroll().userScrollActive = true;
  }

  // Stop any running arrow key scroll animation
  if (m_arrowKeyScrollHelper) {
    m_arrowKeyScrollHelper->stopAnimation();
  }
}

void ScrollManager::setupScrollSuppression() {
  if (!m_state) {
    return;
  }

  m_state->arrow().suppressArrowCenter = true;
  qint64 until = QDateTime::currentMSecsSinceEpoch() +
                 UIConstants::Mouse::WHEEL_SUPPRESS_ARROW_CENTER_MS;
  m_state->arrow().suppressArrowCenterUntilMs = until;

  InteractionStateHolder *statePtr = m_state;
  // Clear arrow center suppression after the suppression window expires -
  // checks timestamp to avoid clearing if another suppress was scheduled
  QTimer::singleShot(
      UIConstants::Keyboard::ARROW_CENTER_CLEAR_CHECK_DELAY_MS, this, [statePtr]() {
        if (!statePtr) {
          return;
        }
        qint64 suppressUntilMs = statePtr->arrow().suppressArrowCenterUntilMs;
        if (suppressUntilMs > 0 &&
            QDateTime::currentMSecsSinceEpoch() < suppressUntilMs) {
          return;
        }
        statePtr->arrow().suppressArrowCenter = false;
      });
}

void ScrollManager::finalizeScrollChanges() {
  // Delay clearing UserScrollActive to allow any pending scroll events
  // to be processed with the flag still set
  QTimer::singleShot(
      UIConstants::Mouse::USER_SCROLL_ACTIVE_CLEAR_DELAY_MS, this, [this]() {
        if (m_state) {
          m_state->scroll().userScrollActive = false;
        }
      });

  notifyUserActivity();
  if (!m_scrollTimer->isActive()) {
    m_scrollTimer->start();
  }
}

void ScrollManager::onThrottledUpdate() { updateVirtualView(); }

void ScrollManager::onSubcollectionDoubleClicked(int subcollectionIndex) {
  emit subcollectionEntered(subcollectionIndex);
}

void ScrollManager::onVirtualFolderDoubleClicked(const QString &folderPath) {
  emit virtualFolderEntered(folderPath);
}

// Returns the virtual folder path for a visual index, or empty string if not a virtual folder
auto ScrollManager::virtualFolderPathForVisualIndex(int visualIndex) const -> QString {
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

void ScrollManager::calculateMovementDirection(int selectedIndex, int prevIndex,
                                               int itemsPerRow,
                                               bool &isHorizontalMove) {
  if (prevIndex < 0) {
    isHorizontalMove = false;
    return;
  }

  // Allow jumps > 1 if on the same row (for rapid click-hold advancing)
  int prevRow = GridUtils::computeItemRow(prevIndex, itemsPerRow);
  int currRow = GridUtils::computeItemRow(selectedIndex, itemsPerRow);
  
  if (prevRow == currRow) {
    isHorizontalMove = true;
    return;
  }

  int diff = std::abs(selectedIndex - prevIndex);
  if (diff != 1) {
    isHorizontalMove = false;
    return;
  }

  if (itemsPerRow <= 1) {
    isHorizontalMove = false;
    return;
  }

  int prevCol = prevIndex % itemsPerRow;
  if (prevCol < 0) {
    prevCol += itemsPerRow;
  }
  int currCol = selectedIndex % itemsPerRow;
  if (currCol < 0) {
    currCol += itemsPerRow;
  }

  bool wrappedForward = (prevCol == itemsPerRow - 1) && (currCol == 0);
  bool wrappedBackward = (prevCol == 0) && (currCol == itemsPerRow - 1);
  isHorizontalMove = wrappedForward || wrappedBackward;
}
