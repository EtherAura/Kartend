// Manages virtual scrolling, widget pooling, and grid layout for large item
// collections.
#include "scrollmanager.h"
#include "loggingcategories.h"
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

// ScrollManagerSetup getter definitions (all resolve through ApplicationContext)
SETUP_GETTER_DEF_CTX_ONLY(ScrollManagerSetup, QWidget *, GridContainer,
                          gridContainer)
SETUP_GETTER_DEF_CTX_ONLY(ScrollManagerSetup, QScrollArea *, MediaScrollArea,
                          itemScrollArea)
SETUP_GETTER_DEF_CTX_ONLY(ScrollManagerSetup, ArtworkManager *, ArtworkManager,
                          artworkManager)
SETUP_GETTER_DEF_CTX_ONLY(ScrollManagerSetup, const QList<CollectionConfig> *,
                          Collections, collections)
SETUP_GETTER_DEF_CTX_ONLY(ScrollManagerSetup,
                          const CollectionHierarchyCache *, HierarchyCache,
                          hierarchyCache)
SETUP_GETTER_DEF_CTX_ONLY(ScrollManagerSetup, InteractionStateHolder *,
                          InteractionState, interactionState)
SETUP_GETTER_DEF_CTX_ONLY(ScrollManagerSetup, const GeneralSettings *,
                          GeneralSettings, generalSettings)

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

void ScrollManager::notifyUserActivity() {
  if (m_artworkManager) {
    m_artworkManager->updateUserActivity();
  }
}

auto ScrollManager::getCurrentGridWidth() const -> int {
  return m_context.config.gridWidth;
}

auto ScrollManager::getEffectiveViewportWidth() const -> int {
  if (!m_mediaScrollArea) {
    return 0;
  }
  int viewportWidth = m_mediaScrollArea->viewport()->width();
  return qMax(UIConstants::Scroll::MIN_EFFECTIVE_VIEWPORT_WIDTH,
              viewportWidth);
}

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
// Creates and positions widget for given visual index, handling both
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

