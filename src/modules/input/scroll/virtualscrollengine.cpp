// VirtualScrollEngine: drives virtual-scrolling layout, container lifecycle,
// and widget materialization for ScrollManager. Promoted from
// scrollmanagervirtual.cpp.
#include "virtualscrollengine.h"

#include "applicationcontext.h"
#include "arrowkeyscrollhelper.h"
#include "artworkutils.h"
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
#include "scrollmanager.h"
#include "searchloadingoverlay.h"
#include "selectioncoordinator.h"
#include "selectiondisplaymanager.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "textzoom.h"
#include "timerutils.h"
#include "virtualcontainermanager.h"
#include "virtualscrollmath.h"
#include "widgetpoolmanager.h"

#include <algorithm>
#include <QApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QtGlobal>
#include <QWidget>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcScrollManager().isDebugEnabled()) {                                                      \
      qCDebug(lcScrollManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

VirtualScrollEngine::VirtualScrollEngine(ScrollManager *owner) : QObject(owner), m_owner(owner) {}

void VirtualScrollEngine::updateVirtualView() {
  if (m_owner->m_destroying || QApplication::closingDown()) {
    return;
  }
  if ((!m_owner->m_virtualContainer) || (!m_owner->m_mediaScrollArea)) {
    if (m_owner->m_emptyViewDebugBudget > 0) {
      --m_owner->m_emptyViewDebugBudget;
      debugLog("updateVirtualView: early return - missing container/scrollArea "
               "(virtualContainer="
               << static_cast<bool>(m_owner->m_virtualContainer)
               << " scrollArea=" << static_cast<bool>(m_owner->m_mediaScrollArea) << ")");
    }
    return;
  }
  if (m_owner->m_metrics.itemsPerRow <= 0) {
    if (m_owner->m_emptyViewDebugBudget > 0) {
      --m_owner->m_emptyViewDebugBudget;
      debugLog("updateVirtualView: early return - itemsPerRow<=0 (itemsPerRow="
               << m_owner->m_metrics.itemsPerRow << ")");
    }
    return;
  }

  // Perf trace — fires once per QPropertyAnimation::valueChanged step
  // during smooth scroll (~60fps), so cumulative cost matters here even
  // when each call is cheap. Drives the diagnosis for vertical-grid
  // animation jerkiness reported during Kartend-jxp5 follow-up.
  const bool perfTrace = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE");
  QElapsedTimer perfWall;
  qint64 perfCalcMs = 0, perfEnsureMs = 0, perfRemoveMs = 0, perfArtMs = 0;
  if (perfTrace) {
    perfWall.start();
  }

  QElapsedTimer perfCalcTimer;
  if (perfTrace) perfCalcTimer.start();
  const NeededRange needed = calculateNeededIndices();
  if (perfTrace) perfCalcMs = perfCalcTimer.elapsed();

  if (needed.isEmpty()) {
    if (m_owner->m_emptyViewDebugBudget > 0) {
      --m_owner->m_emptyViewDebugBudget;
      debugLog("updateVirtualView: needed is EMPTY (totalItems="
               << m_owner->m_totalItems << "itemsPerRow=" << m_owner->m_metrics.itemsPerRow
               << "itemWxH=" << m_owner->m_metrics.itemWidth << "x" << m_owner->m_metrics.itemHeight
               << ")");
    }
    return;
  }

  // Detect whether the per-widget visual config changed since the last pass.
  // When it hasn't (the common smooth-scroll case), ensureWidgetForIndex skips
  // re-pushing title/font/corner/dimension setters to every visible widget and
  // only repositions them (Kartend-8ucd).
  const WidgetVisualConfig currentConfig = currentWidgetVisualConfig();
  m_widgetVisualConfigDirty = (currentConfig != m_lastWidgetVisualConfig);

  QElapsedTimer perfEnsureTimer;
  if (perfTrace) perfEnsureTimer.start();
  for (int visualIndex = needed.firstIndex; visualIndex <= needed.lastIndex; ++visualIndex) {
    ensureWidgetForIndex(visualIndex);
  }
  if (perfTrace) perfEnsureMs = perfEnsureTimer.elapsed();
  m_lastWidgetVisualConfig = currentConfig;

  if (m_owner->m_activeWidgets.isEmpty() && m_owner->m_emptyViewDebugBudget > 0) {
    --m_owner->m_emptyViewDebugBudget;
    debugLog("updateVirtualView: NO widgets materialized (needed="
             << needed.size() << "firstRow=" << m_owner->getFirstVisibleRow()
             << "lastRow=" << m_owner->getLastVisibleRow() << "totalItems=" << m_owner->m_totalItems
             << "itemsPerRow=" << m_owner->m_metrics.itemsPerRow
             << "itemWxH=" << m_owner->m_metrics.itemWidth << "x" << m_owner->m_metrics.itemHeight
             << "virtualContainer=" << static_cast<bool>(m_owner->m_virtualContainer)
             << "scrollArea=" << static_cast<bool>(m_owner->m_mediaScrollArea) << ")");
  }

  {
    QElapsedTimer t;
    if (perfTrace) t.start();
    removeUnneededWidgets(needed);
    if (perfTrace) perfRemoveMs = t.elapsed();
  }
  {
    QElapsedTimer t;
    if (perfTrace) t.start();
    updateArtworkIfAllowed();
    if (perfTrace) perfArtMs = t.elapsed();
  }

  if (m_owner->m_overlayManager) {
    if (m_owner->m_overlayManager->isForceVisible() && m_owner->m_selectionState->hasSelection()) {
      m_owner->refreshSelectionOverlayState();
    }
    // QPropertyAnimation::valueChanged drives updateVirtualView at ~60 Hz
    // during smooth scroll. QWidget::raise() does a full re-stacking and
    // emits a paint on the overlay + siblings — skip it on animation
    // frames where the selection hasn't moved (Kartend-eukc). Whenever
    // the committed index actually changes, the overlay is re-raised and
    // its sibling order is published again.
    const int committedIndex = m_owner->m_selectionState->committedSelectedIndex();
    if (committedIndex != m_lastRaisedSelectionIndex) {
      m_owner->m_overlayManager->raise();
      m_lastRaisedSelectionIndex = committedIndex;
    }
  }

  if (perfTrace) {
    // Log every call (no >Nms filter) — we need to count frequency, not just
    // tail values. Round-13 trace produced only 5 lines here vs 49 artwork
    // batches; need to confirm whether updateVirtualView really runs 5x or
    // we're filtering out many sub-2ms calls.
    const qint64 total = perfWall.elapsed();
    // Inter-call interval — diagnoses Qt animation timer drift on Wayland
    // (Kartend-9q8d round 4). If updateVirtualView fires from
    // QPropertyAnimation::valueChanged at vsync intervals (~16.67ms on
    // 60Hz), drift will show up here as variance — values jumping 12 / 18 /
    // 14 / 21 ms instead of a clean ~16ms cadence. Even cadence + still
    // jerky == paint pipeline; uneven cadence == animation timer drift.
    static thread_local QElapsedTimer s_interval;
    static thread_local bool s_intervalStarted = false;
    qint64 intervalMs = -1;
    if (s_intervalStarted) {
      intervalMs = s_interval.elapsed();
    } else {
      s_intervalStarted = true;
    }
    s_interval.restart();
    qCDebug(lcPerfTrace).nospace()
        << "VirtualScrollEngine::updateVirtualView: total=" << total << " interval=" << intervalMs
        << " (calc=" << perfCalcMs << " ensure=" << perfEnsureMs << " remove=" << perfRemoveMs
        << " art=" << perfArtMs << ") needed=" << needed.size()
        << " active=" << m_owner->m_activeWidgets.size();
  }
}

auto VirtualScrollEngine::calculateNeededIndices() const -> NeededRange {
  // Kartend-gro2: rows are contiguous and item indices are monotonic, so the
  // needed widgets form a single half-open band (no per-index QSet).
  // Kartend-tacgk: the overscan + clamp arithmetic lives in the pure,
  // unit-tested VirtualScrollMath::neededIndexBand; here we just feed it the
  // current viewport rows and collection size from the owning ScrollManager.
  const VirtualScrollMath::IndexBand band = VirtualScrollMath::neededIndexBand(
      m_owner->getFirstVisibleRow(), m_owner->getLastVisibleRow(), m_owner->m_metrics.itemsPerRow,
      m_owner->m_totalItems);
  return NeededRange{band.firstIndex, band.lastIndex};
}

void VirtualScrollEngine::removeUnneededWidgets(const NeededRange &needed) {
  for (auto it = m_owner->m_activeWidgets.begin(); it != m_owner->m_activeWidgets.end();
       /* advance inside */) {
    if (!needed.contains(it.key())) {
      if (ItemWidget *widget = it.value()) {
        m_owner->releaseWidget(widget);
      }
      it = m_owner->m_activeWidgets.erase(it);
    } else {
      ++it;
    }
  }
}

void VirtualScrollEngine::updateArtworkIfAllowed() {
  IArtworkManager *art = m_owner->m_ctx ? m_owner->m_ctx->artworkManager() : nullptr;
  InteractionStateHolder *state = m_owner->m_ctx ? m_owner->m_ctx->interactionState() : nullptr;
  if (!QApplication::closingDown() && art) {
    const bool suppressArtwork = state && state->artwork().suppressArtwork;
    const bool allowDuringSelection = state && state->artwork().allowDuringSelection;
    if (!suppressArtwork || allowDuringSelection) {
      // Route through the debounced TimerCoordinator (100ms) instead of
      // calling updateViewportArtwork() directly (Kartend-9q8d). This is
      // called from updateVirtualView, which fires on every animation
      // step (~60Hz during a smooth scroll) — the previous direct call
      // accumulated to 312ms of GUI-thread time per scroll session, with
      // 4 outlier frames at 45-65ms each that the user perceived as
      // jerkiness. Coalescing multiple per-frame triggers into one
      // trailing-edge update flattens those spikes. Trade-off: during
      // continuous scroll (longer than the debounce window) art doesn't
      // load until the user stops; that's the same UX as the existing
      // async load (which was already 'popping in' after worker decode).
      art->scheduleViewportUpdate();
    }
  }
}

void VirtualScrollEngine::enforceScrollContentConstraints() {
  if ((!m_owner->m_gridContainer) || (!m_owner->m_mediaScrollArea)) {
    return;
  }
  // Use totalHeight (clamped to Qt's QWIDGETSIZE_MAX) for container.
  // Scrollbar stays at clamped range - scroll scaling maps to logical
  // positions.
  m_owner->m_gridContainer->setMinimumHeight(m_owner->m_metrics.totalHeight);
  m_owner->m_gridContainer->setMaximumHeight(m_owner->m_metrics.totalHeight);
  // in Horizontal mode the long axis is X, so pin the container's
  // width too so the scroll area's horizontal scrollbar reflects the full
  // collection extent. Other modes leave width up to layout / VirtualContainerManager.
  if (m_owner->m_metrics.isHorizontal) {
    m_owner->m_gridContainer->setMinimumWidth(m_owner->m_metrics.totalWidth);
    m_owner->m_gridContainer->setMaximumWidth(m_owner->m_metrics.totalWidth);
  }
}

void VirtualScrollEngine::recreateLayout() {
  // m_owner is a QPointer (Kartend-kovt); cache to a raw alias so the
  // clang-analyzer flow analysis doesn't re-assume null after each
  // intermediate member-function call (which it can't prove leaves the
  // back-pointer intact even though Qt's parent ownership guarantees it).
  ScrollManager *owner = m_owner.data();
  if (!owner) {
    return;
  }
  if (owner->m_dataManager->filePaths().isEmpty() &&
      owner->m_dataManager->subcollections().isEmpty()) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  bool isListMode = (owner->m_context.config.viewType == ViewType::List);
  int fontSize = isListMode ? owner->m_context.config.listView.listFontSize
                            : owner->m_context.config.gridLayout.fontSize;
  // scale before push so item titles match the active zoom.
  fontSize = TextZoom::zoomedFontSize(fontSize);
  for (auto it = owner->m_activeWidgets.begin(); it != owner->m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (!widget) {
      continue;
    }
    widget->setHideTitles(owner->m_context.config.hideTitles);
    widget->setHideSubcollectionTitles(owner->m_context.config.hideSubcollectionTitles);
    widget->setFontSize(fontSize);
    widget->setCornerRadius(owner->m_context.config.gridLayout.cornerRadius);
    widget->setItemDimensions(owner->m_metrics.itemWidth, owner->m_metrics.itemHeight);
    QPoint position = owner->getItemPosition(it.key());
    widget->setGeometry(position.x(), position.y(), owner->m_metrics.itemWidth,
                        owner->m_metrics.itemHeight);
  }
  updateVirtualView();
}

void VirtualScrollEngine::centerHorizontalScrollbar() {
  positionVirtualContainer();
}

void VirtualScrollEngine::handleLayoutChange() {
  if (m_owner->m_destroying || QApplication::closingDown()) {
    return;
  }

  // Release all active widgets back to the pool - they need to be recreated
  // because layout changes (especially view type changes) require fresh widgets
  // with different configurations (e.g., list mode has no image label)
  for (auto it = m_owner->m_activeWidgets.begin(); it != m_owner->m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (widget && m_owner->m_widgetPool) {
      m_owner->m_widgetPool->release(widget);
    }
  }
  m_owner->m_activeWidgets.clear();

  calculateVirtualMetrics();

  // Update factory with new metrics before creating widgets
  // Critical for view type changes where item dimensions differ significantly
  if (m_owner->m_widgetFactory) {
    m_owner->m_widgetFactory->setMetrics(m_owner->m_metrics.itemWidth,
                                         m_owner->m_metrics.itemHeight);
  }

  positionVirtualContainer();
  updateVirtualView();

  // Clear user scroll state after layout change - scroll position changes
  // during resize should not block subsequent programmatic centering
  if (auto *state = m_owner->m_ctx ? m_owner->m_ctx->interactionState() : nullptr) {
    state->scroll().userScrollActive = false;
  }
}

void VirtualScrollEngine::recalculateContainerMetrics() {
  if (!m_owner->m_virtualContainer) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  updateVirtualView();
}

void VirtualScrollEngine::forceVirtualViewUpdate() {
  calculateVirtualMetrics();
  positionVirtualContainer();
  updateVirtualView();
}

void VirtualScrollEngine::preCalculateLayout() {
  calculateVirtualMetrics();
  positionVirtualContainer();
}

// Create the virtual container without showing it immediately
void VirtualScrollEngine::createVirtualContainer() {
  if (!m_owner->m_containerManager) {
    return;
  }

  m_owner->m_containerManager->createContainer();
  m_owner->m_virtualContainer = m_owner->m_containerManager->container();

  connectScrollEvents();
  positionVirtualContainer();

  // Create or update list header for list view mode
  m_owner->updateListHeader();
}

// Forwarder: list-header rendering lives on SelectionDisplayManager. Sync the
// virtual container pointer first since it changes across collection reloads.
void VirtualScrollEngine::primeLayoutFor(const CollectionConfig &config) {
  if ((!m_owner->m_gridContainer) || (!m_owner->m_mediaScrollArea)) {
    return;
  }
  m_owner->m_context.config = config;
  // Update factory context so new widgets get correct settings (corner radius,
  // etc.)
  if (m_owner->m_widgetFactory) {
    m_owner->m_widgetFactory->setCollectionContext(m_owner->m_context);
  }
  // settings save calls primeLayoutFor with the updated config.
  // Re-push the source data + context so the FilterManager's hideMissingArtwork
  // baseline reflects the new toggle, then either rebuild the artwork-only
  // baseline (when no other filter is active) or re-run the active search /
  // subcollection filter so the new predicate composes with it.
  if (m_owner->m_filterManager && m_owner->m_dataManager) {
    m_owner->m_filterManager->setSourceData(
        m_owner->m_dataManager->filePaths(), m_owner->m_dataManager->fileNames(),
        m_owner->m_dataManager->filePathToDisplayName(), m_owner->m_dataManager->subcollections());
    m_owner->m_filterManager->setContext(m_owner->m_context);
    if (m_owner->m_filterManager->isFiltered() &&
        !m_owner->m_filterManager->currentFilter().isEmpty()) {
      m_owner->m_filterManager->applyFilter(m_owner->m_filterManager->currentFilter());
    } else {
      m_owner->m_filterManager->clearFilter();
    }
    m_owner->m_totalItems =
        m_owner->m_filterManager->isFiltered()
            ? m_owner->m_filterManager->filteredCount()
            : m_owner->m_dataManager->subcollectionCount() + m_owner->m_dataManager->fileCount();
  }
  int savedTotal = m_owner->m_totalItems;
  m_owner->m_totalItems = 0;
  calculateVirtualMetrics();
  // Update factory metrics after calculation - critical for list mode where
  // itemWidth depends on viewport width calculated in calculateVirtualMetrics()
  if (m_owner->m_widgetFactory) {
    m_owner->m_widgetFactory->setMetrics(m_owner->m_metrics.itemWidth,
                                         m_owner->m_metrics.itemHeight);
  }
  if (!m_owner->m_virtualContainer) {
    createVirtualContainer();
  }
  positionVirtualContainer();
  m_owner->m_totalItems = savedTotal;
}

// Positions virtual scrolling container with alignment and overflow handling
void VirtualScrollEngine::positionVirtualContainer() {
  if (!m_owner->m_containerManager || !m_owner->m_virtualContainer) {
    return;
  }

  bool isFiltered = m_owner->m_filterManager && m_owner->m_filterManager->isFiltered();

  ContainerPositionParams params;
  params.totalWidth = m_owner->m_metrics.totalWidth;
  params.totalHeight = m_owner->m_metrics.totalHeight;
  params.itemsPerRow = m_owner->m_metrics.itemsPerRow;
  params.totalItems = m_owner->m_totalItems;
  params.alignment = m_owner->getCurrentAlignment();
  params.isFiltered = isFiltered;
  params.isHorizontal = m_owner->m_metrics.isHorizontal;

  m_owner->m_containerManager->positionContainer(params);

  // Update list header position after container is positioned to ensure
  // header x-position matches the container's final position
  m_owner->updateListHeader();
}

// Cleans up the virtual container and persistent selection overlay resources
void VirtualScrollEngine::cleanupVirtualContainer() {
  debugLog("cleanupVirtualContainer called!");
  if (m_owner->m_containerManager) {
    m_owner->m_containerManager->cleanupContainer();
  }
  m_owner->m_virtualContainer = nullptr;
  // Note: Don't delete the list header here - it persists across collection
  // reloads and is parented to the viewport (not the virtual container)
}

void VirtualScrollEngine::calculateVirtualMetrics() {
  // Use GridLayoutCalculator for metrics calculation
  m_owner->m_metrics = GridLayoutCalculator::calculateMetrics(
      m_owner->m_context.config, m_owner->m_totalItems, m_owner->m_sidebarShrinkingActive);

  // List mode: set item width to fill available viewport (minus scrollbar and
  // margins)
  if (m_owner->m_context.config.viewType == ViewType::List && m_owner->m_mediaScrollArea) {
    int viewportWidth = m_owner->m_mediaScrollArea->viewport()->width();
    int scrollbarWidth = m_owner->getScrollbarWidth();
    // Full width minus margins and scrollbar
    m_owner->m_metrics.itemWidth =
        viewportWidth - (m_owner->m_metrics.margins * 2) - scrollbarWidth;
    m_owner->m_metrics.totalWidth = viewportWidth - scrollbarWidth;
    m_owner->m_metrics.actualGridWidth =
        m_owner->m_metrics.itemWidth + (m_owner->m_metrics.margins * 2);
  }

  // Shrink the virtual container width when the entire grid fits in a
  // single partial row. Without this, totalWidth always reflects a full
  // itemsPerRow-wide row, so a centered container ends up looking left-
  // aligned because the partial widgets occupy slots 0..N-1 of a wider
  // block. Applies to both naturally small collections (e.g. a parent
  // showing only 3 subcollections at gridWidth=7) and search-filtered
  // result sets.
  if (m_owner->m_totalItems > 0 && m_owner->m_totalItems < m_owner->m_metrics.itemsPerRow) {
    m_owner->m_metrics =
        GridLayoutCalculator::adjustForFilter(m_owner->m_metrics, m_owner->m_totalItems);
  }

  // Note: updateListHeader() is called separately AFTER
  // positionVirtualContainer() to ensure header x-position matches the
  // container's final position
}

// Connects scrollbars to update logic and sets user scroll activity properties
void VirtualScrollEngine::connectScrollEvents() {
  if (m_owner->m_scrollEventHandler) {
    m_owner->m_scrollEventHandler->connectEvents();
  }
}

void VirtualScrollEngine::disconnectScrollEvents() {
  if (m_owner->m_scrollEventHandler) {
    m_owner->m_scrollEventHandler->disconnectEvents();
  }
}

// Snapshots the visual config currently applied to materialized widgets. Must
// stay in lockstep with the setters in ensureWidgetForIndex's existing-widget
// branch so a real config change is detected as a signature change.
VirtualScrollEngine::WidgetVisualConfig VirtualScrollEngine::currentWidgetVisualConfig() const {
  WidgetVisualConfig cfg;
  if (!m_owner) {
    return cfg;
  }
  const bool isListMode = (m_owner->m_context.config.viewType == ViewType::List);
  const int rawFont = isListMode ? m_owner->m_context.config.listView.listFontSize
                                 : m_owner->m_context.config.gridLayout.fontSize;
  cfg.hideTitles = m_owner->m_context.config.hideTitles;
  cfg.hideSubcollectionTitles = m_owner->m_context.config.hideSubcollectionTitles;
  cfg.fontSize = TextZoom::zoomedFontSize(rawFont);
  cfg.cornerRadius = m_owner->m_context.config.gridLayout.cornerRadius;
  cfg.itemWidth = m_owner->m_metrics.itemWidth;
  cfg.itemHeight = m_owner->m_metrics.itemHeight;
  return cfg;
}

// Ensures a widget exists for the visual index; orders click handling to emit
// first so InteractionManager controls selection and scrolling
// subcollections and media items
void VirtualScrollEngine::ensureWidgetForIndex(int visualIndex) {
  if (visualIndex < 0 || visualIndex >= m_owner->m_totalItems) {
    return;
  }
  if (!m_owner->m_virtualContainer) {
    return;
  }

  ItemWidget *existing = m_owner->m_activeWidgets.value(visualIndex, nullptr);
  if (existing) {
    if (!existing->isVisible()) {
      existing->show();
    }
    // On smooth-scroll frames the visual config is unchanged, so only the
    // geometry below needs updating; skip the (otherwise-redundant, though
    // individually early-out-guarded) title/font/corner/dimension setters
    // (Kartend-8ucd). m_widgetVisualConfigDirty is recomputed once per
    // updateVirtualView pass.
    if (m_widgetVisualConfigDirty) {
      bool isListMode = (m_owner->m_context.config.viewType == ViewType::List);
      int fontSize = isListMode ? m_owner->m_context.config.listView.listFontSize
                                : m_owner->m_context.config.gridLayout.fontSize;
      // same as the bulk-update branch above.
      fontSize = TextZoom::zoomedFontSize(fontSize);
      existing->setHideTitles(m_owner->m_context.config.hideTitles);
      existing->setHideSubcollectionTitles(m_owner->m_context.config.hideSubcollectionTitles);
      existing->setFontSize(fontSize);
      existing->setCornerRadius(m_owner->m_context.config.gridLayout.cornerRadius);
      existing->setItemDimensions(m_owner->m_metrics.itemWidth, m_owner->m_metrics.itemHeight);
    }
    QPoint position = m_owner->getItemPosition(visualIndex);
    existing->setGeometry(position.x(), position.y(), m_owner->m_metrics.itemWidth,
                          m_owner->m_metrics.itemHeight);
    return;
  }

  int actualIndex = m_owner->getFilteredIndex(visualIndex);
  if (actualIndex < 0) {
    return;
  }

  int subCount = m_owner->m_dataManager->subcollectionCount();
  int folderCount = m_owner->m_dataManager->virtualFolderCount();
  ItemWidget *itemWidget = nullptr;

  if (m_owner->m_widgetFactory) {
    if (actualIndex < subCount) {
      // Subcollection item
      int subcollectionIndex = m_owner->m_dataManager->subcollectionIndexFromActual(actualIndex);
      itemWidget = m_owner->m_widgetFactory->createSubcollectionWidget(subcollectionIndex);
    } else if (actualIndex < subCount + folderCount) {
      // Virtual folder item
      QString folderPath = m_owner->m_dataManager->virtualFolderFromActual(actualIndex);
      itemWidget = m_owner->m_widgetFactory->createVirtualFolderWidget(folderPath);
    } else {
      // Media item
      int mediaIndex = m_owner->m_dataManager->mediaIndexFromActual(actualIndex);
      int collectionIndex = m_owner->m_context.currentIndex;
      itemWidget = m_owner->m_widgetFactory->createMediaWidget(mediaIndex, collectionIndex);
    }
  }

  if (itemWidget) {
    QPoint position = m_owner->getItemPosition(visualIndex);
    itemWidget->setGeometry(position.x(), position.y(), m_owner->m_metrics.itemWidth,
                            m_owner->m_metrics.itemHeight);
    // Set row index for alternating background colors in list mode
    int rowIndex = GridUtils::computeItemRow(visualIndex, m_owner->m_metrics.itemsPerRow);
    itemWidget->setRowIndex(rowIndex);
    itemWidget->show();

    // Restore selection state if this widget corresponds to the currently
    // selected index
    if (visualIndex == m_owner->m_selectionState->committedSelectedIndex()) {
      itemWidget->setSelected(true);
    }

    // Connect artwork preview signal for list mode (use UniqueConnection to
    // avoid duplicates on widget reuse)
    connect(itemWidget, &ItemWidget::artworkPreviewRequested, m_owner,
            &ScrollManager::onArtworkPreviewRequested, Qt::UniqueConnection);

    m_owner->m_activeWidgets.insert(visualIndex, itemWidget);
  }
}

void VirtualScrollEngine::reconfigureArtworkForActiveWidgets() {
  IArtworkManager *art = m_owner->m_ctx ? m_owner->m_ctx->artworkManager() : nullptr;
  if (!m_owner->m_widgetFactory || !art) {
    return;
  }

  // Re-configure artwork for active widgets that may not have gotten
  // artwork paths on initial creation (directories weren't cached yet).
  // Skip widgets that already have artwork to avoid redundant work.
  // Use forceDirectLookup=true since prewarm has warmed the OS dentry cache.
  int reconfigured = 0;
  int skipped = 0;
  for (auto it = m_owner->m_activeWidgets.begin(); it != m_owner->m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (!widget) {
      continue;
    }
    // Skip if widget already has artwork loaded or pending
    if (art->hasArtworkForWidget(widget)) {
      ++skipped;
      continue;
    }
    QString filePath = widget->getFilePath();
    if (filePath.isEmpty()) {
      continue;
    }
    // Force direct lookup - prewarm has warmed the OS filesystem cache
    m_owner->m_widgetFactory->configureArtworkForWidget(widget, filePath,
                                                        /*forceDirectLookup=*/true);
    ++reconfigured;
  }

  qCDebug(lcPerfTrace) << "reconfigureArtworkForActiveWidgets: reconfigured=" << reconfigured
                       << "skipped=" << skipped;
  // Trigger viewport update to load the newly-configured artwork
  art->scheduleViewportUpdate();
}

// Returns the virtual folder path for a visual index, or empty string if not a
// virtual folder
auto VirtualScrollEngine::virtualFolderPathForVisualIndex(int visualIndex) const -> QString {
  int actualIndex = m_owner->getFilteredIndex(visualIndex);
  return m_owner->m_dataManager->virtualFolderFromActual(actualIndex);
}

// Returns the underlying path for a visual index; delegates to DatabaseManager
// for path resolution
auto VirtualScrollEngine::filePathForVisualIndex(int visualIndex) const -> QString {
  int actualIndex = m_owner->getFilteredIndex(visualIndex);
  int mediaIndex = m_owner->m_dataManager->mediaIndexFromActual(actualIndex);
  if (mediaIndex < 0) {
    return {};
  }

  const QString rawEntry = m_owner->m_dataManager->rawFilePath(mediaIndex);
  if (rawEntry.isEmpty()) {
    return {};
  }

  IDatabaseManager *db = m_owner->m_ctx ? m_owner->m_ctx->databaseManager() : nullptr;
  if (!db) {
    return {};
  }
  return db->resolveFilePath(rawEntry, m_owner->m_context);
}
