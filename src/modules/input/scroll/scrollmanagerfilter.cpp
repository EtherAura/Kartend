// Search/filter/pre-search-state methods extracted from scrollmanager.cpp.
// Operate on raw aliases (m_filterManager, m_dataManager,
// m_preSearchStateManager) into m_dataSource.
#include "applicationcontext.h"
#include "datasourcemanager.h"
#include "filtermanager.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "scrollmanager.h"
#include "widgetpoolmanager.h"
#include <QScrollArea>
#include <QScrollBar>

void ScrollManager::applyFilter(const QString &searchText) {
  if (!m_filterManager) {
    return;
  }

  // Update FilterManager's source data before applying filter
  m_filterManager->setSourceData(m_dataManager->filePaths(), m_dataManager->fileNames(),
                                 m_dataManager->filePathToDisplayName(),
                                 m_dataManager->subcollections(), m_dataManager->virtualFolders(),
                                 m_dataManager->unifiedConcatToActualMap());
  m_filterManager->setContext(m_context);
  m_filterManager->applyFilter(searchText);

  // Update local state from FilterManager. The unfiltered fallback is the
  // store's full count — virtual folders included.
  m_totalItems = m_filterManager->isFiltered() ? m_filterManager->filteredCount()
                                               : m_dataManager->totalItemCount();

  calculateVirtualMetrics();
  positionVirtualContainer();

  // Release widgets back to pool for reuse instead of just hiding
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      releaseWidget(widget);
    }
  }
  clearActiveWidgets();

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
  clearActiveWidgets();
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
  auto getPositionFunc = [this](int index) -> QPoint { return getItemPosition(index); };

  m_preSearchStateManager->restoreState(m_activeWidgets, m_virtualContainer, m_widgetPool.get(),
                                        m_ctx ? m_ctx->artworkManager() : nullptr, getPositionFunc,
                                        m_metrics.itemWidth, m_metrics.itemHeight);
}

auto ScrollManager::hasPreSearchState() const -> bool {
  return m_dataSource && m_dataSource->hasPreSearchState();
}

void ScrollManager::showSearchLoadingOverlay() {
  if (m_dataSource) {
    m_dataSource->showSearchLoadingOverlay();
  }
}

void ScrollManager::hideSearchLoadingOverlay() {
  if (m_dataSource) {
    m_dataSource->hideSearchLoadingOverlay();
  }
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

  // Pre-set scroll position BEFORE positionVirtualContainer to prevent visual
  // jump where list briefly shows at position 0 then jumps to remembered
  // position
  if (hasPreSearch && m_mediaScrollArea && m_preSearchStateManager) {
    if (QScrollBar *scrollbar = m_mediaScrollArea->verticalScrollBar()) {
      int viewportHeight = m_mediaScrollArea->viewport()->height();
      // Use clamped totalHeight for scrollbar range
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
    clearActiveWidgets();
    updateVirtualView();
  }
}

auto ScrollManager::getFilteredIndex(int visualIndex) const -> int {
  return m_dataSource ? m_dataSource->getFilteredIndex(visualIndex) : visualIndex;
}
