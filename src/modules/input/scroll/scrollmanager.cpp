// Manages virtual scrolling, widget pooling, and grid layout for large item
// collections.
#include "scrollmanager.h"
#include "applicationcontext.h"
#include "arrowkeyscrollhelper.h"
#include "artworkpreviewoverlay.h"
#include "artworkutils.h"
#include "coverflowcontroller.h"
#include "datasourcemanager.h"
#include "filtermanager.h"
#include "gridlayoutcalculator.h"
#include "gridutils.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "itemwidgetfactory.h"
#include "loggingcategories.h"
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
#include "virtualscrollengine.h"
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
Q_LOGGING_CATEGORY(lcScrollManager, "kartend.scrollmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcScrollManager().isDebugEnabled()) {                                                      \
      qCDebug(lcScrollManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

// Initializes timers for throttle, arrow-key updates, and a short idle window
// to treat any scrollbar interaction as user-driven scrolling
ScrollManager::ScrollManager(QObject *parent) : IScrollManager(parent) {
  // Widget pool for recycling ItemWidgets
  m_widgetPool = std::make_unique<WidgetPoolManager>(this);

  // Virtual scrolling engine: owns layout / container / materialization
  // algorithms. Borrows ScrollManager state via friendship.
  m_engine = std::make_unique<VirtualScrollEngine>(this);

  // Data source manager: owns FilterManager + ScrollDataManager +
  // PreSearchStateManager + SearchLoadingOverlay.
  m_dataSource = std::make_unique<DataSourceManager>(this);
  m_filterManager = m_dataSource->filterManager();
  m_dataManager = m_dataSource->dataManager();
  m_preSearchStateManager = m_dataSource->preSearchStateManager();
  m_searchLoadingOverlay = m_dataSource->searchLoadingOverlay();
  connect(m_dataSource.get(), &DataSourceManager::filterChanged, this,
          &ScrollManager::filterChanged);

  // Selection display manager: owns overlay + state tracker + list header +
  // artwork preview overlay.
  m_selectionDisplay = std::make_unique<SelectionDisplayManager>(this);
  m_overlayManager = m_selectionDisplay->overlay();
  m_selectionState = m_selectionDisplay->state();

  // Forward list-mode signals from display manager out through ScrollManager.
  connect(m_selectionDisplay.get(), &SelectionDisplayManager::sortModeChangeRequested, this,
          &ScrollManager::sortModeChangeRequested);
  connect(m_selectionDisplay.get(), &SelectionDisplayManager::listColumnWidthChanged, this,
          &ScrollManager::listColumnWidthChanged);
  connect(m_selectionDisplay.get(), &SelectionDisplayManager::listArtworkColumnWidthChanged, this,
          &ScrollManager::listArtworkColumnWidthChanged);
  connect(m_selectionDisplay.get(), &SelectionDisplayManager::artworkPreviewLaunchRequested, this,
          &ScrollManager::artworkPreviewLaunchRequested);
  connect(m_selectionDisplay.get(), &SelectionDisplayManager::artworkPreviewVisibilityChanged, this,
          &ScrollManager::artworkPreviewVisibilityChanged);

  connect(m_overlayManager, &SelectionOverlayManager::animationFinished, this, [this]() {
    // Update widget selection states when animation finishes
    if (m_selectionState->needsCommitUpdate(m_selectionState->lastSelectedIndex())) {
      if (auto *prevSel =
              m_activeWidgets.value(m_selectionState->committedSelectedIndex(), nullptr)) {
        prevSel->setSelected(false);
      }
    }
    if (auto *newSel = m_activeWidgets.value(m_selectionState->lastSelectedIndex(), nullptr)) {
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

  // SearchLoadingOverlay is now owned by m_dataSource.

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
  connect(m_scrollEventHandler.get(), &ScrollEventHandler::userScrollEnded, this,
          &ScrollManager::updateVirtualView);
  connect(m_scrollEventHandler.get(), &ScrollEventHandler::sliderMoved, this,
          &ScrollManager::onSliderMoved);

  // Item widget factory for creating and configuring widgets
  m_widgetFactory = std::make_unique<ItemWidgetFactory>(this);
  m_widgetFactory->setWidgetPool(m_widgetPool.get());
  connect(m_widgetFactory.get(), &ItemWidgetFactory::subcollectionDoubleClicked, this,
          &ScrollManager::onSubcollectionDoubleClicked);
  connect(m_widgetFactory.get(), &ItemWidgetFactory::virtualFolderDoubleClicked, this,
          &ScrollManager::onVirtualFolderDoubleClicked);
  connect(m_widgetFactory.get(), &ItemWidgetFactory::requestItemsRange, this,
          [this](int offset, int limit) {
            debugLog("requestItemsRange: offset="
                     << offset << "limit=" << limit << "totalItems=" << m_totalItems
                     << "mediaFileCount=" << (m_dataManager ? m_dataManager->fileCount() : -1));
            emit requestItemsRange(offset, limit);
          });

  // Arrow key scroll helper for centering animation
  m_arrowKeyScrollHelper = std::make_unique<ArrowKeyScrollHelper>(this);
  connect(m_arrowKeyScrollHelper.get(), &ArrowKeyScrollHelper::requestViewUpdate, this,
          &ScrollManager::updateVirtualView);

  // ScrollDataManager and PreSearchStateManager are now owned by m_dataSource
  // Raw aliases (m_dataManager, m_preSearchStateManager) were
  // set up at construction.

  // Note: SelectionStateTracker is now owned by m_selectionDisplay; the
  // m_selectionState raw alias was set up above.

  // Throttle timer - only fires once per interval, ignores subsequent triggers
  m_scrollTimer = new QTimer(this);
  m_scrollTimer->setSingleShot(true);
  m_scrollTimer->setInterval(UIConstants::Timing::SCROLL_THROTTLE_DELAY_MS);
  connect(m_scrollTimer, &QTimer::timeout, this, &ScrollManager::onThrottledUpdate);

  // Arrow key view update timer - delegates to helper
  m_arrowKeyViewUpdateTimer = new QTimer(this);
  m_arrowKeyViewUpdateTimer->setSingleShot(true);
  m_arrowKeyViewUpdateTimer->setInterval(UIConstants::Keyboard::VIEW_UPDATE_INTERVAL_MS);
  connect(m_arrowKeyViewUpdateTimer, &QTimer::timeout, this, &ScrollManager::onArrowKeyViewUpdate);

  // Debounce timer - restarts on each trigger, fires after inactivity
  m_userScrollIdleTimer =
      new TimerUtils::DebouncedTimer(UIConstants::Mouse::USER_SCROLL_IDLE_TIMER_MS, this);
  connect(m_userScrollIdleTimer, &TimerUtils::DebouncedTimer::triggered, this, [this]() {
    if (m_scrollEventHandler) {
      m_scrollEventHandler->setUserScrollActive(false);
    }
  });

  // Prewarm timer - replenishes widget pool after scroll activity settles
  m_prewarmIdleTimer =
      new TimerUtils::DebouncedTimer(UIConstants::Widget::Pool::PREWARM_IDLE_MS, this);
  connect(m_prewarmIdleTimer, &TimerUtils::DebouncedTimer::triggered, this, [this]() {
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

  // Cover-flow controller — owns the CoverFlowWidget. Its carousel signals
  // are forwarded straight through ScrollManager's own signals so external
  // wiring (MainWindow) keeps connecting to ScrollManager.
  m_coverFlow = std::make_unique<CoverFlowController>(this);
  connect(m_coverFlow.get(), &CoverFlowController::selectItemByIndex, this,
          &ScrollManager::selectItemByIndex);
  connect(m_coverFlow.get(), &CoverFlowController::subcollectionEntered, this,
          &ScrollManager::subcollectionEntered);
  connect(m_coverFlow.get(), &CoverFlowController::virtualFolderEntered, this,
          &ScrollManager::virtualFolderEntered);
  connect(m_coverFlow.get(), &CoverFlowController::itemActivated, this,
          &ScrollManager::coverFlowItemActivated);
  connect(m_coverFlow.get(), &CoverFlowController::activeChanged, this,
          &ScrollManager::coverFlowActiveChanged);
}

// Destructor disconnects scroll events, clears timers, deletes widgets and
// container
ScrollManager::~ScrollManager() {
  m_destroying = true;
  disconnectScrollEvents();

  TimerUtils::stopAndDisconnectTimers({m_scrollTimer, m_arrowKeyViewUpdateTimer});

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
  if (auto *art = m_ctx ? m_ctx->artworkManager() : nullptr) {
    art->clearPendingArtworkForWidget(widget);
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

// ScrollManagerSetup getter definitions (all resolve through ApplicationContext)
SETUP_GETTER_DEF_UI_CTX_ONLY(ScrollManagerSetup, QWidget *, GridContainer, gridContainer)
SETUP_GETTER_DEF_UI_CTX_ONLY(ScrollManagerSetup, QScrollArea *, MediaScrollArea, itemScrollArea)
SETUP_GETTER_DEF_MGR_CTX_ONLY(ScrollManagerSetup, IArtworkManager *, ArtworkManager, artworkManager)
SETUP_GETTER_DEF_COL_CTX_ONLY(ScrollManagerSetup, const QList<CollectionConfig> *, Collections,
                              collections)
SETUP_GETTER_DEF_COL_CTX_ONLY(ScrollManagerSetup, const CollectionHierarchyCache *, HierarchyCache,
                              hierarchyCache)
SETUP_GETTER_DEF_MGR_CTX_ONLY(ScrollManagerSetup, InteractionStateHolder *, InteractionState,
                              interactionState)
SETUP_GETTER_DEF_COL_CTX_ONLY(ScrollManagerSetup, const GeneralSettings *, GeneralSettings,
                              generalSettings)

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
  // different row heights, so the old scroll position is meaningless. Reset
  // both scrollbars because Horizontal mode drives the
  // horizontal axis instead of the vertical one.
  if (m_mediaScrollArea) {
    if (m_mediaScrollArea->verticalScrollBar()) {
      m_mediaScrollArea->verticalScrollBar()->setValue(0);
    }
    if (m_mediaScrollArea->horizontalScrollBar()) {
      m_mediaScrollArea->horizontalScrollBar()->setValue(0);
    }
    // in Horizontal mode the items area needs a horizontal
    // scrollbar (modulo the user's hideHorizontalScrollbar preference).
    // Other modes leave the policy under the per-collection setting too —
    // applyHorizontalScrollbarSetting is the canonical path for that — but
    // configureHorizontalScrollbar in VirtualContainerManager will pin it
    // off again on overflow. Force it back on here when entering Horizontal.
    if (viewType == ViewType::Horizontal) {
      m_mediaScrollArea->setHorizontalScrollBarPolicy(m_context.config.gridLayout.hideHorizontalScrollbar
                                                          ? Qt::ScrollBarAlwaysOff
                                                          : Qt::ScrollBarAsNeeded);
    }
  }

  handleLayoutChange();

  // cover-flow uses a parallel widget tree; keep its config, card list, and
  // visibility in sync with the grid's.
  m_coverFlow->refreshForViewTypeChange();
}

void ScrollManager::updateGridWidth(int newGridWidth) {
  // route the write to whichever field is currently active, so
  // calculateMetrics (which reads via the effective-value helpers) picks it up
  // on the next recompute. When the alt field is 0 (= "inherit primary") and
  // we're shrinking, treat the alt as "newly configured" and store there
  // instead of overwriting the primary — that way Ctrl+/- while sidebar is
  // hidden specializes the alt the moment the user diverges from the primary.
  int &target = (m_sidebarShrinkingActive && m_context.config.gridLayout.gridWidthSidebarHidden > 0)
                    ? m_context.config.gridLayout.gridWidthSidebarHidden
                    : m_context.config.gridLayout.gridWidth;
  if (target == newGridWidth) {
    return;
  }
  target = newGridWidth;
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
    widget->setGeometry(position.x(), position.y(), m_metrics.itemWidth, m_metrics.itemHeight);
  }
  updateVirtualView();
}

void ScrollManager::updateHorizontalGridHeight(int newHorizontalGridHeight) {
  // same active-field routing as updateGridWidth.
  int &target = (m_sidebarShrinkingActive && m_context.config.gridLayout.horizontalGridHeightSidebarHidden > 0)
                    ? m_context.config.gridLayout.horizontalGridHeightSidebarHidden
                    : m_context.config.gridLayout.horizontalGridHeight;
  if (target == newHorizontalGridHeight) {
    return;
  }
  target = newHorizontalGridHeight;
  // Only Horizontal view consults this field — other modes can absorb the
  // setting change silently and pick it up the next time they switch in.
  if (m_context.config.viewType != ViewType::Horizontal || !m_virtualContainer) {
    return;
  }
  // Re-flow the existing widgets against the new fixed-axis count. Same
  // pattern as updateGridWidth — the metric recompute drops the totalRows
  // (= column count) along the long axis, then we reposition active widgets.
  calculateVirtualMetrics();
  positionVirtualContainer();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (!widget) {
      continue;
    }
    QPoint position = getItemPosition(it.key());
    widget->setGeometry(position.x(), position.y(), m_metrics.itemWidth, m_metrics.itemHeight);
  }
  updateVirtualView();
}

// Updates active widgets for current viewport and triggers artwork updates
// unless suppression is enforced without selection allowance
auto ScrollManager::getEffectiveHorizontalSpacing() const -> int {
  return m_context.config.gridLayout.horizontalSpacing;
}

auto ScrollManager::getFirstVisibleRow() const -> int {
  if (!m_mediaScrollArea) {
    return 0;
  }
  // in Horizontal mode the long axis is X, so we read the
  // horizontal scrollbar and the viewport width instead.
  int scrollPos;
  int viewportSize;
  if (m_metrics.isHorizontal) {
    scrollPos = m_mediaScrollArea->horizontalScrollBar()->value();
    viewportSize = m_mediaScrollArea->viewport()->width();
  } else {
    scrollPos = m_mediaScrollArea->verticalScrollBar()->value();
    viewportSize = m_mediaScrollArea->viewport()->height();
  }
  auto [firstRow, lastRow] =
      GridLayoutCalculator::getVisibleRowRange(scrollPos, viewportSize, m_metrics, 0);
  return firstRow;
}

auto ScrollManager::getLastVisibleRow() const -> int {
  if (!m_mediaScrollArea) {
    return 0;
  }
  int scrollPos;
  int viewportSize;
  if (m_metrics.isHorizontal) {
    scrollPos = m_mediaScrollArea->horizontalScrollBar()->value();
    viewportSize = m_mediaScrollArea->viewport()->width();
  } else {
    scrollPos = m_mediaScrollArea->verticalScrollBar()->value();
    viewportSize = m_mediaScrollArea->viewport()->height();
  }
  auto [firstRow, lastRow] =
      GridLayoutCalculator::getVisibleRowRange(scrollPos, viewportSize, m_metrics, 0);
  return lastRow;
}

auto ScrollManager::getSubcollectionName(int subcollectionIndex) const -> QString {
  if ((!m_collections) || subcollectionIndex < 0 || subcollectionIndex >= m_collections->size()) {
    return {};
  }
  return (*m_collections)[subcollectionIndex].name;
}

void ScrollManager::setPendingSelectionRestoreByPath(const QString &filePath) {
  // Just store the path - we'll query the database after the collection reloads
  // (in setupVirtualScrolling) when the context and data are ready
  m_pendingRestoreFilePath = filePath;
}

void ScrollManager::onVisualIndexForPathLoaded(int visualIndex, const QString &filePath) {
  // Only process if this is the path we're waiting for
  if (filePath != m_pendingRestoreFilePath) {
    return;
  }

  m_pendingRestoreFilePath.clear();

  if (visualIndex >= 0) {
    // The database returns position among media items only. In the UI,
    // subcollections and virtual folders appear before media items,
    // so we need to offset the index accordingly.
    int prefixCount =
        m_dataManager ? (m_dataManager->subcollectionCount() + m_dataManager->virtualFolderCount())
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

int ScrollManager::subcollectionIndexFromActual(int actualIndex) const {
  return m_dataManager ? m_dataManager->subcollectionIndexFromActual(actualIndex) : -1;
}

void ScrollManager::recenterVirtualContainer() {
  positionVirtualContainer();
}

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
  // Horizontal mode shows a horizontal scrollbar instead, so
  // the vertical scrollbar prediction is always false there.
  if (m_metrics.isHorizontal) {
    return false;
  }
  return m_metrics.totalHeight > m_mediaScrollArea->viewport()->height();
}

auto ScrollManager::getTotalItems() const -> int {
  return m_totalItems;
}

void ScrollManager::notifyUserActivity() {
  if (auto *art = m_ctx ? m_ctx->artworkManager() : nullptr) {
    art->updateUserActivity();
  }
}

auto ScrollManager::getCurrentGridWidth() const -> int {
  // returns the items-per-row currently driving the layout —
  // matches what calculateMetrics() picks (alt when sidebar is hidden in
  // Expand mode and the alt is configured, otherwise primary). Callers that
  // navigate by visual rows (arrow keys, mouse wheel selection) must see the
  // active value, not the persisted primary.
  return CollectionUtils::effectiveGridWidth(m_context.config, m_sidebarShrinkingActive);
}

void ScrollManager::setSidebarShrinkingActive(bool active) {
  if (m_sidebarShrinkingActive == active) {
    return;
  }
  m_sidebarShrinkingActive = active;
  // The actual relayout is driven by the existing recalculateContainerMetrics
  // path that MainWindow already runs after sidebar-visibility changes — we
  // just need the flag in place before that runs. No need to trigger here.
}

auto ScrollManager::getEffectiveViewportWidth() const -> int {
  if (!m_mediaScrollArea) {
    return 0;
  }
  int viewportWidth = m_mediaScrollArea->viewport()->width();
  return qMax(UIConstants::Scroll::MIN_EFFECTIVE_VIEWPORT_WIDTH, viewportWidth);
}

void ScrollManager::updateListHeader() {
  if (!m_selectionDisplay) {
    return;
  }
  m_selectionDisplay->setVirtualContainer(m_virtualContainer);
  m_selectionDisplay->updateListHeader();
}

// Forwarder: artwork preview overlay lives on SelectionDisplayManager.
void ScrollManager::onArtworkPreviewRequested(const QString &filePath, const QString &artworkDir) {
  if (m_selectionDisplay) {
    m_selectionDisplay->showArtworkPreview(filePath, artworkDir);
  }
}

// Public entry point used by InteractionManager for expand-mode activation.
// Identical behavior to onArtworkPreviewRequested but exposed as a public
// API for callers that don't go through the widget signal chain.
void ScrollManager::showArtworkPreview(const QString &filePath, const QString &artworkDir) {
  if (m_selectionDisplay) {
    m_selectionDisplay->showArtworkPreview(filePath, artworkDir);
  }
}

// Video-first preview entry point. Used by expand-mode and
// middle-click; falls back to artwork when no video matches.
bool ScrollManager::showMediaPreview(const QString &filePath, const QString &artworkDir,
                                     const QString &videoDir) {
  return m_selectionDisplay && m_selectionDisplay->showMediaPreview(filePath, artworkDir, videoDir);
}

void ScrollManager::setArtworkPreviewGallery(
    const QList<ArtworkPreviewOverlay::GalleryEntry> &entries) {
  if (m_selectionDisplay) {
    m_selectionDisplay->setArtworkPreviewGallery(entries);
  }
}

// Prime the container with target collection metrics before items are loaded
// Creates and positions widget for given visual index, handling both
auto ScrollManager::getItemPosition(int visualIndex) const -> QPoint {
  bool isFiltered = m_filterManager && m_filterManager->isFiltered();
  QPoint pos =
      GridLayoutCalculator::getItemPosition(visualIndex, m_metrics, isFiltered, m_totalItems);

  // For very large grids that exceed Qt's size limit, position widgets
  // relative to the viewport. The scrollbar is in clamped (widget) space,
  // but we calculate which rows are visible using logical scroll position.
  // Place widgets so they appear at the correct viewport-relative position.
  if (m_metrics.isClipped && m_mediaScrollArea) {
    if (m_metrics.isHorizontal) {
      int widgetScrollX = m_mediaScrollArea->horizontalScrollBar()->value();
      int viewportWidth = m_mediaScrollArea->viewport()->width();
      int logicalScrollX = m_metrics.toLogicalScrollY(widgetScrollX, viewportWidth);
      int relativeX = widgetScrollX + (pos.x() - logicalScrollX);
      return QPoint(relativeX, pos.y());
    }
    int widgetScrollY = m_mediaScrollArea->verticalScrollBar()->value();
    int viewportHeight = m_mediaScrollArea->viewport()->height();
    // Convert clamped scroll position to logical scroll position with viewport
    // for precise endpoint mapping (scrollbar max reaches end of collection)
    int logicalScrollY = m_metrics.toLogicalScrollY(widgetScrollY, viewportHeight);
    // Widget appears at (logicalY - logicalScrollY) pixels from viewport top,
    // which means placing it at widgetScrollY + (logicalY - logicalScrollY)
    // in container coordinates
    int relativeY = widgetScrollY + (pos.y() - logicalScrollY);
    return QPoint(pos.x(), relativeY);
  }

  return pos;
}
