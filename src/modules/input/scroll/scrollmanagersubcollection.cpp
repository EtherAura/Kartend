// Subcollection-context + filter cluster split out from scrollmanager.cpp.
#include "coverflowcontroller.h"
#include "datasourcemanager.h"
#include "filtermanager.h"
#include "itemwidget.h"
#include "itemwidgetfactory.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "scrollmanager.h"

#include <QScrollArea>
#include <QScrollBar>

void ScrollManager::updateContextForSubcollection(int subcollectionIndex) {
  if ((!m_collections) || subcollectionIndex < 0 || subcollectionIndex >= m_collections->size()) {
    return;
  }
  m_context.currentIndex = subcollectionIndex;
  m_context.config = (*m_collections)[subcollectionIndex];
  // Reinitialize subcollections for the new context
  m_dataManager->initializeSubcollections(m_context, m_collections, m_hierarchyCache);
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
  if ((!m_collections) || subcollectionIndex < 0 || subcollectionIndex >= m_collections->size()) {
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
                                 m_dataManager->filePathToDisplayName(),
                                 m_dataManager->subcollections());
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

  if ((m_mediaScrollArea) && (m_mediaScrollArea->verticalScrollBar())) {
    m_mediaScrollArea->verticalScrollBar()->setValue(0);
  }

  // FilterManager emits filterChanged, no need to emit here
  updateVirtualView();
  enforceScrollContentConstraints();

  // cover-flow operates over the same filtered card list, so any filter
  // rebuild that re-sources the grid also re-sources the carousel. The
  // controller gates on the carousel being active so grid-mode users don't
  // pay the per-item descriptor build for no visible result.
  m_coverFlow->rebuildCardsIfActive();
}
