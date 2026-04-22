// Sibling TU: centering/scroll-to-visible logic for ViewportManager.
#include "viewportmanager.h"
#include "animationmanager.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "gridlayoutcalculator.h"
#include "interactionstateholder.h"
#include "scrollmanager.h"
#include "selectionmanager.h"

#include "gridutils.h"
#include "uiconstants.h"

#include <QApplication>
#include <QDateTime>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcViewportManager)
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcViewportManager().isDebugEnabled()) {                                \
      qCDebug(lcViewportManager) << msg;                                       \
    }                                                                          \
  } while (0)

void ViewportManager::centerItemVertically(int index, bool immediate) {
  if (!m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }
  if (index < 0) {
    return;
  }
  if (shouldDeferCenterNow(immediate, index)) {
    return;
  }

  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];

  // Get metrics from ScrollManager for correct dimensions in both grid and list
  // modes
  int gridWidth = collection.gridWidth;
  int itemHeight = collection.itemHeight;
  int vSpacing = collection.verticalSpacing;

  if (m_scrollManager) {
    const auto &metrics = m_scrollManager->getMetrics();
    gridWidth = metrics.itemsPerRow;
    itemHeight = metrics.itemHeight;
    vSpacing = metrics.verticalSpacing;
  }

  if (gridWidth <= 0) {
    return;
  }

  QScrollBar *verticalScrollBar = m_itemScrollArea->verticalScrollBar();
  if (!verticalScrollBar) {
    return;
  }
  int viewportHeight = m_itemScrollArea->viewport()->height();
  if (viewportHeight <= 0) {
    return;
  }

  bool clickScroll = m_state ? m_state->scroll().clickScroll : false;
  bool clickHoldAdv = m_state ? m_state->scroll().clickHoldAdvancing : false;
  bool forceClickAnim = m_state ? m_state->click().clickForceAnim : false;

  // Get metrics from ScrollManager for very large collections
  int totalHeight = 0;
  int logicalHeight = 0;
  int headerOffset = 0;
  if (m_scrollManager) {
    const auto &metrics = m_scrollManager->getMetrics();
    totalHeight = metrics.totalHeight;
    logicalHeight = metrics.logicalHeight;
    headerOffset = metrics.headerOffset;
  }

  int targetY = AnimationManager::computeTargetYForIndex(
      index, gridWidth, itemHeight, vSpacing, viewportHeight,
      verticalScrollBar->maximum(), totalHeight, logicalHeight, headerOffset);

  bool forceImmediate = computeForceImmediate(immediate);
  if (shouldEarlyReturnUserScroll(forceImmediate)) {
    return;
  }

  int curY = verticalScrollBar->value();
  int distance = qAbs(targetY - curY);

  int currentRow = (gridWidth > 0 ? index / gridWidth : -1);
  int smallThreshold = computeSmallThreshold(currentRow);

  bool useSmooth = forceClickAnim ||
                   (m_continuousScrollActive && !m_instantPositioning &&
                    !m_wrapSequenceActive) ||
                   (m_state && m_state->scroll().clickContinuous) ||
                   (m_state && m_state->scroll().keyContinuous);

  if (!forceImmediate && distance <= smallThreshold) {
    if (handleSmallMovementEarlyReturn(distance, clickScroll, index,
                                       currentRow)) {
      return;
    }
  }

  if (maybeHandleImmediateCenter(distance <= 1, useSmooth, forceImmediate,
                                 forceClickAnim, verticalScrollBar, targetY,
                                 index, currentRow)) {
    return;
  }

  if (m_animationManager) {
    m_animationManager->ensureVAnimCreated(verticalScrollBar);

    if (m_animationManager->handleExistingVerticalAnimIfRunning(
            verticalScrollBar, targetY, clickScroll, clickHoldAdv, curY,
            distance)) {
      return;
    }
  }

  int duration = computeVerticalCenterDuration(distance, m_repeating);

  if (forceClickAnim && distance <= 1) {
    adjustForForceClickZeroDistance(verticalScrollBar, targetY, curY, distance,
                                    duration, forceClickAnim);
  }

  if (m_animationManager) {
    m_animationManager->configureAndStartVerticalAnimation(
        verticalScrollBar, curY, targetY, duration, clickScroll, clickHoldAdv);
  }
}

bool ViewportManager::computeForceImmediate(bool immediate) const {
  // Query SelectionManager as the source of truth for restore state
  bool restoringSelection =
      m_selectionManager && m_selectionManager->isRestoringSelection();
  return immediate || m_forceImmediateCenter || m_isWrappingNavigation ||
         restoringSelection || m_instantPositioning || m_wrapSequenceActive;
}

int ViewportManager::computeSmallThreshold(int currentRow) const {
  constexpr int kSmallThresholdSameRow = 8;
  constexpr int kSmallThresholdOtherRow = 2;
  return (m_lastSelectedRow >= 0 && m_lastSelectedRow == currentRow)
             ? kSmallThresholdSameRow
             : kSmallThresholdOtherRow;
}

bool ViewportManager::handleSmallMovementEarlyReturn(int /*distance*/,
                                                     bool clickScroll,
                                                     int index,
                                                     int currentRow) {
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
    int idxDyn = (m_state && m_state->isSelectionSuppressed())
                     ? m_state->pendingSelectionIndex()
                     : index;
    if (idxDyn >= 0) {
      m_scrollManager->updateSelectionForIndex(idxDyn);
    }
  }
  if (clickScroll && m_state) {
    m_state->scroll().clickScroll = false;
  }
  if (m_state) {
    m_state->arrow().suppressArrowCenter = false;
  }
  m_instantPositioning = false;
  m_lastSelectedRow = currentRow;
  if (m_selectionManager) {
    m_selectionManager->setLastSelectedRow(currentRow);
  }
  return true;
}

bool ViewportManager::shouldDeferCenterNow(bool immediate, int index) const {
  if (immediate) {
    return false;
  }
  if (!m_state || !m_state->click().deferCenterOnClick) {
    return false;
  }
  int defIdx = m_state->click().deferredCenterIndex;
  return (defIdx < 0 || defIdx == index);
}

bool ViewportManager::shouldEarlyReturnUserScroll(bool forceImmediate) const {
  return (m_state && m_state->scroll().userScrollActive) && !forceImmediate;
}

bool ViewportManager::handlePendingInitialCenterIfNeeded(
    QScrollBar *verticalScrollBar, int index, int targetYUnbounded,
    bool immediate) {
  Q_UNUSED(targetYUnbounded);
  if (verticalScrollBar->maximum() == 0 && !immediate) {
    if (m_state && !m_state->click().pendingInitialCenter) {
      m_state->click().pendingInitialCenter = true;
      // Defer centering until scrollbar has a valid range - happens during
      // initial layout when content height isn't calculated yet
      QTimer::singleShot(UIConstants::Sidebar::INITIAL_CENTER_SCROLL_DELAY_MS,
                         this, [this, index]() {
                           if (m_state) {
                             m_state->click().pendingInitialCenter = false;
                           }
                           if (!QApplication::closingDown()) {
                             centerItemVertically(index, false);
                           }
                         });
    }
    return true;
  }
  return false;
}

void ViewportManager::adjustForForceClickZeroDistance(
    QScrollBar *verticalScrollBar, int targetY, int &curY, int &distance,
    int &duration, bool /*forceClickAnim*/) {
  if (targetY == curY) {
    int adjust = (targetY > 0 ? -1 : 1);
    if (m_state) {
      m_state->scroll().programmaticScroll = true;
    }
    if (m_scrollManager) {
      m_scrollManager->refreshSelectionOverlayState();
    }
    int startVal = qBound(0, targetY + adjust, verticalScrollBar->maximum());
    verticalScrollBar->setValue(startVal);
    // Defer clearing ProgrammaticScroll flag until after Qt processes the
    // setValue() - ensures the scroll event handler sees the flag is set
    QTimer::singleShot(0, this, [this]() {
      if (m_state) {
        m_state->scroll().programmaticScroll = false;
      }
      if (m_scrollManager) {
        m_scrollManager->refreshSelectionOverlayState();
      }
    });
    curY = startVal;
    distance = qAbs(targetY - curY);
    duration = computeVerticalCenterDuration(distance, m_repeating);
  }
}

bool ViewportManager::handleImmediateCenterForEnsureVisible(int index) {
  if (!m_forceImmediateCenter) {
    return false;
  }
  if (!m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return false;
  }
  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if ((!vScrollBar) || (!hScrollBar)) {
    return false;
  }
  QRect viewport = m_itemScrollArea->viewport()->rect();
  int viewportWidth = viewport.width();
  int viewportHeight = viewport.height();
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0 || viewportHeight <= 0) {
    return false;
  }
  int hSpacing = (m_scrollManager)
                     ? m_scrollManager->getEffectiveHorizontalSpacing()
                     : collection.horizontalSpacing;
  int margins = UIConstants::Grid::MARGINS;
  int itemX = GridUtils::computeItemX(index, gridWidth, collection.itemWidth,
                                      hSpacing, margins);
  int itemY = GridUtils::computeItemY(index, gridWidth, collection.itemHeight,
                                      collection.verticalSpacing, margins);

  // Calculate target scroll position in logical space (center the item)
  int logicalTargetY =
      itemY + (collection.itemHeight / 2) - (viewportHeight / 2);
  logicalTargetY = qMax(0, logicalTargetY);

  // Convert logical scroll target to widget scroll position for clipped grids
  int targetY = toWidgetScrollY(logicalTargetY);
  targetY = qBound(0, targetY, vScrollBar->maximum());

  int targetX = GridUtils::computeCenterTarget(
      itemX, collection.itemWidth, viewportWidth, hScrollBar->maximum());
  vScrollBar->setValue(targetY);
  hScrollBar->setValue(targetX);
  if (m_scrollManager) {
    // When wrapping, clear all widgets to prevent stale artwork from showing
    // at wrong positions after the large scroll jump
    if (m_isWrappingNavigation) {
      m_scrollManager->cleanupActiveWidgets();
    }
    m_scrollManager->updateVirtualView();
  }
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
  if (m_selectionManager) {
    m_selectionManager->setLastSelectedRow(m_lastSelectedRow);
  }
  m_forceImmediateCenter = false;
  m_deferredCenterPending = false;
  return true;
}

bool ViewportManager::maybeHandleImmediateCenter(
    bool distanceSmall, bool useSmooth, bool forceImmediate,
    bool forceClickAnim, QScrollBar *verticalScrollBar, int targetY, int index,
    int currentRow) {
  if (((distanceSmall && !useSmooth) || forceImmediate) && !forceClickAnim) {
    if (handleImmediateCenterPath(verticalScrollBar, targetY, index,
                                  currentRow)) {
      return true;
    }
  }
  return false;
}

bool ViewportManager::handleImmediateCenterPath(QScrollBar *verticalScrollBar,
                                                int targetY, int index,
                                                int currentRow) {
  if (m_animationManager) {
    m_animationManager->stopActiveVerticalAnims(verticalScrollBar);
  }
  setProgrammaticScrollGuarded(true);
  setScrollValueAndUpdateSelection(verticalScrollBar, targetY, index);
  setProgrammaticScrollGuarded(false);
  finalizeImmediateCenteringState(index, currentRow);
  clearArtworkSuppressionViewportUpdateIfNeeded();
  clearArrowCenterSuppressionWhenDue();
  return true;
}

