// VirtualScrollEngine: drives virtual-scrolling layout, container lifecycle,
// and widget materialization for ScrollManager. Promoted from
// scrollmanagervirtual.cpp (Kartend-158).
#include "virtualscrollengine.h"

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

  QSet<int> needed = calculateNeededIndices();

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

  for (int visualIndex : needed) {
    ensureWidgetForIndex(visualIndex);
  }

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

  removeUnneededWidgets(needed);
  updateArtworkIfAllowed();

  if (m_owner->m_overlayManager) {
    if (m_owner->m_overlayManager->isForceVisible() && m_owner->m_selectionState->hasSelection()) {
      m_owner->refreshSelectionOverlayState();
    }
    m_owner->m_overlayManager->raise();
  }
}

auto VirtualScrollEngine::calculateNeededIndices() const -> QSet<int> {
  int firstVisible = m_owner->getFirstVisibleRow();
  int lastVisible = m_owner->getLastVisibleRow();
  int startRow = qMax(0, firstVisible - 1);
  int endRow = lastVisible + 1;

  int maxRow = ((m_owner->m_totalItems + m_owner->m_metrics.itemsPerRow - 1) /
                m_owner->m_metrics.itemsPerRow) -
               1;
  if (maxRow < 0) {
    return {};
  }
  endRow = std::min(endRow, maxRow);

  QSet<int> needed;
  for (int rowIndex = startRow; rowIndex <= endRow; ++rowIndex) {
    for (int columnIndex = 0; columnIndex < m_owner->m_metrics.itemsPerRow; ++columnIndex) {
      int visualIndex = (rowIndex * m_owner->m_metrics.itemsPerRow) + columnIndex;
      if (visualIndex < m_owner->m_totalItems) {
        needed.insert(visualIndex);
      }
    }
  }
  return needed;
}

void VirtualScrollEngine::removeUnneededWidgets(const QSet<int> &needed) {
  QList<int> existing = m_owner->m_activeWidgets.keys();
  for (int visualIndex : existing) {
    if (!needed.contains(visualIndex)) {
      if (ItemWidget *widget = m_owner->m_activeWidgets.value(visualIndex)) {
        m_owner->releaseWidget(widget);
      }
      m_owner->m_activeWidgets.remove(visualIndex);
    }
  }
}

void VirtualScrollEngine::updateArtworkIfAllowed() {
  if (!QApplication::closingDown() && m_owner->m_artworkManager) {
    const bool suppressArtwork = m_owner->m_state && m_owner->m_state->artwork().suppressArtwork;
    const bool allowDuringSelection =
        m_owner->m_state && m_owner->m_state->artwork().allowDuringSelection;
    if (!suppressArtwork || allowDuringSelection) {
      m_owner->m_artworkManager->updateViewportArtwork();
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
}

void VirtualScrollEngine::recreateLayout() {
  if (m_owner->m_dataManager->filePaths().isEmpty() &&
      m_owner->m_dataManager->subcollections().isEmpty()) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  bool isListMode = (m_owner->m_context.config.viewType == ViewType::List);
  int fontSize =
      isListMode ? m_owner->m_context.config.listFontSize : m_owner->m_context.config.fontSize;
  for (auto it = m_owner->m_activeWidgets.begin(); it != m_owner->m_activeWidgets.end(); ++it) {
    ItemWidget *widget = it.value();
    if (!widget) {
      continue;
    }
    widget->setHideTitles(m_owner->m_context.config.hideTitles);
    widget->setHideSubcollectionTitles(m_owner->m_context.config.hideSubcollectionTitles);
    widget->setFontSize(fontSize);
    widget->setCornerRadius(m_owner->m_context.config.cornerRadius);
    widget->setItemDimensions(m_owner->m_metrics.itemWidth, m_owner->m_metrics.itemHeight);
    QPoint position = m_owner->getItemPosition(it.key());
    widget->setGeometry(position.x(), position.y(), m_owner->m_metrics.itemWidth,
                        m_owner->m_metrics.itemHeight);
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
  if (m_owner->m_state) {
    m_owner->m_state->scroll().userScrollActive = false;
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
  // Kartend-ks4n: settings save calls primeLayoutFor with the updated config.
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
  m_owner->m_metrics =
      GridLayoutCalculator::calculateMetrics(m_owner->m_context.config, m_owner->m_totalItems);

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
    bool isListMode = (m_owner->m_context.config.viewType == ViewType::List);
    int fontSize =
        isListMode ? m_owner->m_context.config.listFontSize : m_owner->m_context.config.fontSize;
    existing->setHideTitles(m_owner->m_context.config.hideTitles);
    existing->setHideSubcollectionTitles(m_owner->m_context.config.hideSubcollectionTitles);
    existing->setFontSize(fontSize);
    existing->setCornerRadius(m_owner->m_context.config.cornerRadius);
    existing->setItemDimensions(m_owner->m_metrics.itemWidth, m_owner->m_metrics.itemHeight);
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
  if (!m_owner->m_widgetFactory || !m_owner->m_artworkManager) {
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
    if (m_owner->m_artworkManager->hasArtworkForWidget(widget)) {
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
  m_owner->m_artworkManager->scheduleViewportUpdate();
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

  if (!m_owner->m_databaseManager) {
    return {};
  }
  return m_owner->m_databaseManager->resolveFilePath(rawEntry, m_owner->m_context);
}
