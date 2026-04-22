// Sibling TU: click-selection + row-hopping + restore for SelectionManager.
#include "selectionmanager.h"

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWidget>

#include "animationmanager.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "metadatasidebar.h"
#include "mousemanager.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcSelectionManager)
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcSelectionManager().isDebugEnabled()) {                               \
      qCDebug(lcSelectionManager) << msg;                                      \
    }                                                                          \
  } while (0)

bool SelectionManager::checkAndFinalizeRestore(int index) {
  if (m_restoringSelection && index == m_targetRestoreIndex) {
    m_restoringSelection = false;
    m_targetRestoreIndex = -1;
    return true;
  }
  return false;
}

void SelectionManager::prepareForRestore(int targetIndex) {
  clearSelection();

  m_restoringSelection = true;
  m_targetRestoreIndex = targetIndex;
  m_forceImmediateCenter = true;

  // Request InteractionManager to configure scroll area properties
  if (m_state) {
    qint64 until =
        QDateTime::currentMSecsSinceEpoch() +
        UIConstants::Keyboard::ANIMATION_SETTLE_MS +
        UIConstants::Keyboard::ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS;
    m_state->arrow().suppressArrowCenter = true;
    m_state->arrow().suppressArrowCenterUntilMs = until;
  }

  emit requestStopScrollAnimations();
}

void SelectionManager::finalizeRestore() {
  m_restoringSelection = false;
  m_targetRestoreIndex = -1;
  m_forceImmediateCenter = false;

  if (m_state) {
    m_state->click().selectionSuppressed = false;
    m_state->click().pendingSelectionIndex = -1;
  }
}

void SelectionManager::cancelPendingSelectionRestore() {
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
  if (m_state) {
    m_state->selectionRestore().restoreToken++;
    m_state->selectionRestore().restorePending = false;
    m_state->selectionRestore().userSelectionMade = true;
  }
}

void SelectionManager::resetSelectionRestoreState() {
  // Reset state for new navigation - clears pending restores and
  // userSelectionMade so that automatic restore can proceed for the new
  // collection
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
  if (m_state) {
    m_state->selectionRestore().restoreToken++;
    m_state->selectionRestore().restorePending = false;
    m_state->selectionRestore().userSelectionMade = false;
  }
}

void SelectionManager::processSingleClickSelection(int visualIndex,
                                                   const QString &filePath) {
  if ((!m_scrollManager) || (!m_collections) || (!m_currentCollectionIndex)) {
    return;
  }
  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return;
  }

  // Clear user scroll state so centering isn't blocked
  if (m_state) {
    m_state->scroll().userScrollActive = false;
  }

  qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  int dcInterval = QApplication::doubleClickInterval();

  if (m_state) {
    m_state->setHorizAnimActive(false);
    m_state->nextHorizAnimGen();
  }

  if (filePath.isEmpty()) {
    m_selectedFilePath.clear();
  } else {
    m_selectedFilePath = filePath;
  }
  if (m_viewportManager) {
    m_viewportManager->setPhysicalKeyDown(false);
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
  emit requestStopRepeat();

  const int pendingIndex =
      m_state ? m_state->click().rowChangeFirstClickIndex : -1;
  const qint64 pendingMs = m_state ? m_state->click().rowChangeFirstClickMs : 0;
  const bool pendingValid =
      (pendingIndex >= 0 && (nowMs - pendingMs) <= dcInterval);

  const int fromIndex = m_selectedItemIndex;
  const bool canAnimateHoriz =
      shouldAnimateHorizontalHop(fromIndex, visualIndex, gridWidth);

  if (canAnimateHoriz) {
    runHorizontalHopAnimation(fromIndex, visualIndex, nowMs);
    return;
  }

  const bool treatAsNewRow = shouldTreatAsNewRow(visualIndex, gridWidth);
  if (treatAsNewRow) {
    handleNewRowClickSelection(visualIndex, nowMs);
  } else {
    const bool skipCenter = (pendingValid && pendingIndex == visualIndex);
    handleSameRowClickSelection(visualIndex, skipCenter, nowMs);
  }

  if (m_state) {
    m_state->setClickSeriesLastMs(nowMs);
  }
  emit requestFocusItemsPage();
}

void SelectionManager::runHorizontalHopAnimation(int start, int target,
                                                 qint64 nowMs) {
  if (!m_state) {
    return;
  }
  const int gen = m_state->nextHorizAnimGen();
  m_state->setHorizAnimActive(true);
  const int step = (target > start) ? 1 : -1;
  const int steps = qAbs(target - start);
  constexpr int kPerHopMs = 12;
  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(start);
  }
  // Animate horizontal selection by stepping through intermediate indices -
  // each hop scheduled at fixed intervals creates smooth left/right movement
  for (int i = 1; i <= steps; ++i) {
    QTimer::singleShot(i * kPerHopMs, this,
                       [this, gen, i, step, start, target]() {
                         if (!m_state || m_state->horizAnimGen() != gen) {
                           return;
                         }
                         if (!m_scrollManager) {
                           return;
                         }
                         int nextIdx = start + (i * step);
                         if (nextIdx != target) {
                           m_selectedItemIndex = nextIdx;
                           m_scrollManager->updateSelectionForIndex(nextIdx);
                         } else {
                           if (m_state) {
                             m_state->setHorizAnimActive(false);
                           }
                           m_selectedItemIndex = target;
                           selectItemByIndex(target, true);
                           emit requestCenterVertically(target, false);
                         }
                       });
  }
  if (m_state) {
    m_state->setClickSeriesLastMs(nowMs);
    m_state->click().rowChangeFirstClickIndex = -1;
    m_state->click().rowChangeFirstClickMs = 0;
  }
  emit requestFocusItemsPage();
}

void SelectionManager::handleNewRowClickSelection(int visualIndex,
                                                  qint64 nowMs) {
  if (m_state) {
    m_state->click().selectionSuppressed = true;
    m_state->click().pendingSelectionIndex = visualIndex;
    m_state->click().deferCenterOnClick = false;
    m_state->click().deferredCenterIndex = -1;
  }
  m_selectedItemIndex = visualIndex;
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(visualIndex, subs);
  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(visualIndex);
  }
  selectItemByIndex(visualIndex, true);
  emit requestCenterVertically(visualIndex, false);
  if (m_state) {
    m_state->click().rowChangeFirstClickIndex = visualIndex;
    m_state->click().rowChangeFirstClickMs = nowMs;
  }
}

void SelectionManager::handleSameRowClickSelection(int visualIndex,
                                                   bool skipCenter,
                                                   qint64 /*nowMs*/) {
  if (m_state) {
    m_state->click().deferCenterOnClick = false;
    m_state->click().deferredCenterIndex = -1;
    // Clear any stale row-change suppression state to prevent ViewportManager
    // from using the old pendingSelectionIndex when centering
    m_state->click().selectionSuppressed = false;
    m_state->click().pendingSelectionIndex = -1;
  }
  selectItemByIndex(visualIndex, true);
  if (!skipCenter) {
    emit requestCenterVertically(visualIndex, false);
  }
  if (m_state) {
    m_state->click().rowChangeFirstClickIndex = -1;
    m_state->click().rowChangeFirstClickMs = 0;
  }
}

int SelectionManager::handleWidgetSelectionByIndex(int visualIndex,
                                                   const QPoint &clickPos,
                                                   QMouseEvent *originalEvent) {
  Q_UNUSED(clickPos);
  Q_UNUSED(originalEvent);

  if (visualIndex < 0 || !m_scrollManager) {
    return -1;
  }

  // User initiated selection - cancel any pending automatic restore
  // to prevent it from overriding this explicit user choice
  cancelPendingSelectionRestore();

  // Get the file path for this index
  QString filePath = m_scrollManager->filePathForVisualIndex(visualIndex);

  processSingleClickSelection(visualIndex, filePath);
  return visualIndex;
}

int SelectionManager::handleWidgetSelection(ItemWidget *widget,
                                            const QPoint &clickPos,
                                            QMouseEvent *originalEvent) {
  Q_UNUSED(clickPos);
  Q_UNUSED(originalEvent);

  if (!widget || !m_scrollManager) {
    return -1;
  }

  // User initiated selection - cancel any pending automatic restore
  // to prevent it from overriding this explicit user choice
  cancelPendingSelectionRestore();

  // Visual state is handled by ScrollManager::updateSelectionForIndex
  // which is called from selectItemByIndex during processSingleClickSelection

  int visualIndex = -1;
  const auto &activeWidgets = m_scrollManager->getActiveWidgets();
  for (auto it = activeWidgets.constBegin(); it != activeWidgets.constEnd();
       ++it) {
    if (it.value() == widget) {
      visualIndex = it.key();
      break;
    }
  }
  if (visualIndex < 0) {
    return -1;
  }

  QString filePath = widget->getFilePath();
  processSingleClickSelection(visualIndex, filePath);
  return visualIndex;
}

void SelectionManager::beginFullSelectionRestore(int targetIndex) {
  if (targetIndex < 0) {
    return;
  }

  prepareForRestore(targetIndex);

  // Stop any running scroll animations
  emit requestStopScrollAnimations();

  applySelectionStateForIndex(targetIndex);
  if (m_viewportManager) {
    m_viewportManager->applyImmediateViewportPositioningForSelection(
        targetIndex);
  }
  selectItemByIndex(targetIndex, false);

  if (m_selectedItemIndex == targetIndex) {
    finalizeRestoreFlagsAndFocus();
    emit selectionChanged(targetIndex);
  }

  // Finalize restore state
  finalizeRestore();

  if ((m_sidebarManager) && m_sidebarManager->isSidebarVisible()) {
    ItemWidget *widget = widgetForIndex(targetIndex);
    if (widget) {
      m_sidebarManager->updateSidebarMetadata(widget);
    }
    scheduleSidebarMetadataUpdateIfVisible(
        targetIndex, 0,
        UIConstants::Selection::METADATA_SIDEBAR_UPDATE_DELAY_MS);
  }
}

void SelectionManager::applySelectionStateForIndex(int idx) {
  m_selectedItemIndex = idx;
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(idx, subs);
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
    m_scrollManager->updateSelectionForIndex(idx);
  }
}

void SelectionManager::finalizeRestoreFlagsAndFocus() {
  if (m_viewportManager) {
    m_viewportManager->setPhysicalKeyDown(false);
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
  // Only set focus to items page if search bar doesn't currently have focus
  bool searchBarHasFocus = (m_searchBar) && m_searchBar->hasFocus();
  if ((m_itemsPage) && !m_itemsPage->hasFocus() && !searchBarHasFocus) {
    emit requestFocusItemsPage();
  }
  // Clear arrow center suppression after restore completes - ensures the
  // selection is fully visible before allowing subsequent centering operations
  QTimer::singleShot(UIConstants::Keyboard::ARROW_CENTER_CLEAR_AFTER_RESTORE_MS,
                     this, [this]() {
                       if (m_state) {
                         m_state->clearArrowCenterSuppression();
                       }
                     });
}

void SelectionManager::scheduleSidebarMetadataUpdateIfVisible(
    int targetIndex, int initialDelayMs, int secondaryDelayMs) {
  if (!m_sidebarManager || !m_sidebarManager->isSidebarVisible()) {
    return;
  }

  auto updateSidebar = [this, targetIndex]() {
    if (!m_sidebarManager || !m_sidebarManager->isSidebarVisible()) {
      return;
    }
    ItemWidget *widget = widgetForIndex(targetIndex);
    if (widget) {
      m_sidebarManager->updateSidebarMetadata(widget);
    }
  };

  // Schedule sidebar updates at multiple delays to handle asynchronous
  // widget materialization and metadata loading race conditions
  if (initialDelayMs > 0) {
    QTimer::singleShot(initialDelayMs, this, updateSidebar);
  } else {
    updateSidebar();
  }

  if (secondaryDelayMs > 0) {
    // Second pass after the layout has settled, in case the first attempt
    // ran before the widget completed asynchronous metadata loading.
    QTimer::singleShot(secondaryDelayMs, this, updateSidebar);
  }
}
