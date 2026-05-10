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
#include "detailspane.h"
#include "detailspanemanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "mousemanager.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "selectionhelpers.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcSelectionManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcSelectionManager().isDebugEnabled()) {                                                   \
      qCDebug(lcSelectionManager) << msg;                                                          \
    }                                                                                              \
  } while (0)

bool SelectionManager::checkAndFinalizeRestore(int index) {
  auto &restoreState = state()->selectionRestore();
  if (restoreState.restoring && index == restoreState.targetIndex) {
    restoreState.restoring = false;
    restoreState.targetIndex = -1;
    return true;
  }
  return false;
}

void SelectionManager::prepareForRestore(int targetIndex) {
  clearSelection();

  auto &restoreState = state()->selectionRestore();
  restoreState.restoring = true;
  restoreState.targetIndex = targetIndex;
  restoreState.forceImmediateCenter = true;

  qint64 until = QDateTime::currentMSecsSinceEpoch() + UIConstants::Keyboard::ANIMATION_SETTLE_MS +
                 UIConstants::Keyboard::ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS;
  state()->arrow().suppressArrowCenter = true;
  state()->arrow().suppressArrowCenterUntilMs = until;

  emit requestStopScrollAnimations();
}

void SelectionManager::finalizeRestore() {
  auto &restoreState = state()->selectionRestore();
  restoreState.restoring = false;
  restoreState.targetIndex = -1;
  restoreState.forceImmediateCenter = false;

  state()->click().selectionSuppressed = false;
  state()->click().pendingSelectionIndex = -1;
}

void SelectionManager::cancelPendingSelectionRestore() {
  auto &restoreState = state()->selectionRestore();
  ++restoreState.restoreToken;
  restoreState.restorePending = false;
  restoreState.userSelectionMade = true;
}

void SelectionManager::resetSelectionRestoreState() {
  // Reset state for new navigation - clears pending restores and
  // userSelectionMade so that automatic restore can proceed for the new
  // collection
  auto &restoreState = state()->selectionRestore();
  ++restoreState.restoreToken;
  restoreState.restorePending = false;
  restoreState.userSelectionMade = false;
}

void SelectionManager::processSingleClickSelection(int visualIndex, const QString &filePath) {
  if ((!scrollMgr()) || (!m_collections) || (!m_currentCollectionIndex)) {
    return;
  }
  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return;
  }

  // Clear user scroll state so centering isn't blocked
  if (state()) {
    state()->scroll().userScrollActive = false;
  }

  qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  int dcInterval = QApplication::doubleClickInterval();

  if (state()) {
    state()->setHorizAnimActive(false);
    state()->nextHorizAnimGen();
  }

  if (filePath.isEmpty()) {
    m_selectedFilePath.clear();
  } else {
    m_selectedFilePath = filePath;
  }
  if (viewportMgr()) {
    viewportMgr()->setPhysicalKeyDown(false);
    viewportMgr()->setRepeating(false);
    viewportMgr()->setWrapSequenceActive(false);
  }
  emit requestStopRepeat();

  const int pendingIndex = state() ? state()->click().rowChangeFirstClickIndex : -1;
  const qint64 pendingMs = state() ? state()->click().rowChangeFirstClickMs : 0;
  const bool pendingValid =
      SelectionHelpers::isRowChangePendingValid(pendingIndex, pendingMs, nowMs, dcInterval);

  const int fromIndex = m_selectedItemIndex;
  const bool canAnimateHoriz = shouldAnimateHorizontalHop(fromIndex, visualIndex, gridWidth);

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

  if (state()) {
    state()->setClickSeriesLastMs(nowMs);
  }
  emit requestFocusItemsPage();
}

void SelectionManager::runHorizontalHopAnimation(int start, int target, qint64 nowMs) {
  if (!state()) {
    return;
  }
  const int gen = state()->nextHorizAnimGen();
  state()->setHorizAnimActive(true);
  const int steps = SelectionHelpers::hopStepCount(start, target);
  constexpr int kPerHopMs = 12;
  if (scrollMgr()) {
    scrollMgr()->updateSelectionForIndex(start);
  }
  // Animate horizontal selection by stepping through intermediate indices -
  // each hop scheduled at fixed intervals creates smooth left/right movement
  for (int i = 1; i <= steps; ++i) {
    QTimer::singleShot(i * kPerHopMs, this, [this, gen, i, start, target]() {
      if (!state() || state()->horizAnimGen() != gen) {
        return;
      }
      if (!scrollMgr()) {
        return;
      }
      const int nextIdx = SelectionHelpers::hopIntermediateIndex(start, target, i);
      if (nextIdx != target) {
        m_selectedItemIndex = nextIdx;
        scrollMgr()->updateSelectionForIndex(nextIdx);
      } else {
        if (state()) {
          state()->setHorizAnimActive(false);
        }
        m_selectedItemIndex = target;
        selectItemByIndex(target, true);
        emit requestCenterVertically(target, false);
      }
    });
  }
  if (state()) {
    state()->setClickSeriesLastMs(nowMs);
    state()->click().rowChangeFirstClickIndex = -1;
    state()->click().rowChangeFirstClickMs = 0;
  }
  emit requestFocusItemsPage();
}

void SelectionManager::handleNewRowClickSelection(int visualIndex, qint64 nowMs) {
  if (state()) {
    state()->click().selectionSuppressed = true;
    state()->click().pendingSelectionIndex = visualIndex;
    state()->click().deferCenterOnClick = false;
    state()->click().deferredCenterIndex = -1;
  }
  m_selectedItemIndex = visualIndex;
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(visualIndex, subs);
  if (scrollMgr()) {
    scrollMgr()->updateSelectionForIndex(visualIndex);
  }
  selectItemByIndex(visualIndex, true);
  emit requestCenterVertically(visualIndex, false);
  if (state()) {
    state()->click().rowChangeFirstClickIndex = visualIndex;
    state()->click().rowChangeFirstClickMs = nowMs;
  }
}

void SelectionManager::handleSameRowClickSelection(int visualIndex, bool skipCenter,
                                                   qint64 /*nowMs*/) {
  if (state()) {
    state()->click().deferCenterOnClick = false;
    state()->click().deferredCenterIndex = -1;
    // Clear any stale row-change suppression state to prevent ViewportManager
    // from using the old pendingSelectionIndex when centering
    state()->click().selectionSuppressed = false;
    state()->click().pendingSelectionIndex = -1;
  }
  selectItemByIndex(visualIndex, true);
  if (!skipCenter) {
    emit requestCenterVertically(visualIndex, false);
  }
  if (state()) {
    state()->click().rowChangeFirstClickIndex = -1;
    state()->click().rowChangeFirstClickMs = 0;
  }
}

int SelectionManager::handleWidgetSelectionByIndex(int visualIndex, const QPoint &clickPos,
                                                   QMouseEvent *originalEvent) {
  Q_UNUSED(clickPos);
  Q_UNUSED(originalEvent);

  if (visualIndex < 0 || !scrollMgr()) {
    return -1;
  }

  // User initiated selection - cancel any pending automatic restore
  // to prevent it from overriding this explicit user choice
  cancelPendingSelectionRestore();

  // Get the file path for this index
  QString filePath = scrollMgr()->filePathForVisualIndex(visualIndex);

  processSingleClickSelection(visualIndex, filePath);
  return visualIndex;
}

int SelectionManager::handleWidgetSelection(ItemWidget *widget, const QPoint &clickPos,
                                            QMouseEvent *originalEvent) {
  Q_UNUSED(clickPos);
  Q_UNUSED(originalEvent);

  if (!widget || !scrollMgr()) {
    return -1;
  }

  // User initiated selection - cancel any pending automatic restore
  // to prevent it from overriding this explicit user choice
  cancelPendingSelectionRestore();

  // Visual state is handled by ScrollManager::updateSelectionForIndex
  // which is called from selectItemByIndex during processSingleClickSelection

  int visualIndex = -1;
  const auto &activeWidgets = scrollMgr()->getActiveWidgets();
  for (auto it = activeWidgets.constBegin(); it != activeWidgets.constEnd(); ++it) {
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
  if (viewportMgr()) {
    viewportMgr()->applyImmediateViewportPositioningForSelection(targetIndex);
  }
  selectItemByIndex(targetIndex, false);

  if (m_selectedItemIndex == targetIndex) {
    finalizeRestoreFlagsAndFocus();
    emit selectionChanged(targetIndex);
  }

  // Finalize restore state
  finalizeRestore();

  if ((detailsPaneMgr()) && detailsPaneMgr()->isSidebarVisible()) {
    ItemWidget *widget = widgetForIndex(targetIndex);
    if (widget) {
      detailsPaneMgr()->updateSidebarMetadata(widget);
    }
    scheduleSidebarMetadataUpdateIfVisible(
        targetIndex, 0, UIConstants::Selection::METADATA_SIDEBAR_UPDATE_DELAY_MS);
  }
}

void SelectionManager::applySelectionStateForIndex(int idx) {
  m_selectedItemIndex = idx;
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(idx, subs);
  if (scrollMgr()) {
    scrollMgr()->updateVirtualView();
    scrollMgr()->updateSelectionForIndex(idx);
  }
}

void SelectionManager::finalizeRestoreFlagsAndFocus() {
  if (viewportMgr()) {
    viewportMgr()->setPhysicalKeyDown(false);
    viewportMgr()->setRepeating(false);
    viewportMgr()->setWrapSequenceActive(false);
  }
  // Only set focus to items page if search bar doesn't currently have focus
  bool searchBarHasFocus = (m_searchBar) && m_searchBar->hasFocus();
  if ((m_itemsPage) && !m_itemsPage->hasFocus() && !searchBarHasFocus) {
    emit requestFocusItemsPage();
  }
  // Clear arrow center suppression after restore completes - ensures the
  // selection is fully visible before allowing subsequent centering operations
  QTimer::singleShot(UIConstants::Keyboard::ARROW_CENTER_CLEAR_AFTER_RESTORE_MS, this, [this]() {
    if (state()) {
      state()->clearArrowCenterSuppression();
    }
  });
}

void SelectionManager::scheduleSidebarMetadataUpdateIfVisible(int targetIndex, int initialDelayMs,
                                                              int secondaryDelayMs) {
  if (!detailsPaneMgr() || !detailsPaneMgr()->isSidebarVisible()) {
    return;
  }

  auto updateSidebar = [this, targetIndex]() {
    if (!detailsPaneMgr() || !detailsPaneMgr()->isSidebarVisible()) {
      return;
    }
    ItemWidget *widget = widgetForIndex(targetIndex);
    if (widget) {
      detailsPaneMgr()->updateSidebarMetadata(widget);
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
