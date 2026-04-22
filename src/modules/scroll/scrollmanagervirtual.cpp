// Sibling translation unit for ScrollManager.
// Extracted from scrollmanager.cpp during LOC-reduction refactor.
// These remain ScrollManager members; this is a translation-unit split.
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
#include <QtGlobal>
#include <algorithm>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcScrollManager().isDebugEnabled()) {                                  \
      qCDebug(lcScrollManager) << msg;                                         \
    }                                                                          \
  } while (0)

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

    qCDebug(lcPerfTrace) << "reconfigureArtworkForActiveWidgets: reconfigured="
        << reconfigured << "skipped=" << skipped;
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

