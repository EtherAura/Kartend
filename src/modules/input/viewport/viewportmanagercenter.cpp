// Sibling TU: centering/scroll-to-visible logic for ViewportManager.
#include "animationmanager.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/validationhelpers.h"
#include "gridlayoutcalculator.h"
#include "iartworkmanager.h"
#include "igridlayoutscroll.h"
#include "interactionstateholder.h"
#include "iselectionmanager.h"
#include "iselectionoverlayscroll.h"
#include "viewporthelpers.h"
#include "viewportmanager.h"

#include "gridutils.h"
#include "uiconstants/detailspaneconstants.h"
#include "uiconstants/grid.h"

#include <QApplication>
#include <QDateTime>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcViewportManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcViewportManager().isDebugEnabled()) {                                                    \
      qCDebug(lcViewportManager) << msg;                                                           \
    }                                                                                              \
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

  const CollectionConfig &collection = (*m_collections)[*m_currentCollectionIndex];

  // in Horizontal view the scroll axis is X. Centering still
  // dispatches through centerItemVertically (kept as the canonical "center
  // selection along the scroll axis" entry point) but we drive the
  // horizontal scrollbar with a simpler animation path. Vertical centering
  // is unnecessary because the column always fits in the viewport.
  if (scrollGrid() && scrollGrid()->getMetrics().isHorizontal) {
    QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
    if (!hScrollBar) {
      return;
    }
    const auto &metrics = scrollGrid()->getMetrics();
    int viewportWidth = m_itemScrollArea->viewport()->width();
    if (viewportWidth <= 0 || metrics.itemsPerRow <= 0) {
      return;
    }
    int targetX = GridLayoutCalculator::calculateCenterScrollTarget(index, viewportWidth,
                                                                    hScrollBar->maximum(), metrics);
    targetX = qBound(0, targetX, hScrollBar->maximum());
    int curX = hScrollBar->value();
    bool forceImmediate = computeForceImmediate(immediate);
    if (shouldEarlyReturnUserScroll(forceImmediate)) {
      return;
    }
    int colIndex = index / metrics.itemsPerRow;
    if (forceImmediate || qAbs(targetX - curX) <= 1) {
      setProgrammaticScrollGuarded(true);
      hScrollBar->setValue(targetX);
      if (scrollGrid()) {
        if (m_isWrappingNavigation) {
          scrollGrid()->cleanupActiveWidgets();
        }
        scrollGrid()->updateVirtualView();
        int idxDyn = (state() && state()->isSelectionSuppressed())
                         ? state()->pendingSelectionIndex()
                         : index;
        if (idxDyn >= 0) {
          scrollOverlay()->updateSelectionForIndex(idxDyn);
        }
      }
      setProgrammaticScrollGuarded(false);
      finalizeImmediateCenteringState(index, colIndex);
      clearArtworkSuppressionViewportUpdateIfNeeded();
      clearArrowCenterSuppressionWhenDue();
      return;
    }
    // route clicks/arrow-centering through the same chained
    // wheel-style animation (configurable scrollAnimationDurationMs +
    // OutCubic). animateHorizontalSmooth's hardcoded 140ms felt jerky on
    // click, while wheel scrolling already used the longer chained variant.
    if (animMgr()) {
      auto onFinished = [this]() {
        if (state()) {
          state()->scroll().programmaticScroll = false;
        }
        if (scrollGrid()) {
          scrollOverlay()->refreshSelectionOverlayState();
          scrollGrid()->updateVirtualView();
        }
      };
      setProgrammaticScrollGuarded(true);
      animMgr()->startWheelScrollAnimationHorizontal(hScrollBar, curX, targetX, onFinished);
    }
    if (scrollGrid()) {
      scrollGrid()->updateVirtualView();
    }
    m_lastSelectedRow = colIndex;
    if (selectionMgr()) {
      selectionMgr()->setLastSelectedRow(colIndex);
    }
    return;
  }

  // Get metrics from ScrollManager for correct dimensions in both grid and list
  // modes
  int gridWidth = collection.gridLayout.gridWidth;
  int itemHeight = collection.gridLayout.itemHeight;
  int vSpacing = collection.gridLayout.verticalSpacing;

  if (scrollGrid()) {
    const auto &metrics = scrollGrid()->getMetrics();
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

  bool clickScroll = state() ? state()->scroll().clickScroll : false;
  bool clickHoldAdv = state() ? state()->scroll().clickHoldAdvancing : false;
  bool forceClickAnim = state() ? state()->click().clickForceAnim : false;

  // Get metrics from ScrollManager for very large collections
  int totalHeight = 0;
  int logicalHeight = 0;
  int headerOffset = 0;
  if (scrollGrid()) {
    const auto &metrics = scrollGrid()->getMetrics();
    totalHeight = metrics.totalHeight;
    logicalHeight = metrics.logicalHeight;
    headerOffset = metrics.headerOffset;
  }

  int targetY = AnimationManager::computeTargetYForIndex(
      index, gridWidth, itemHeight, vSpacing, viewportHeight, verticalScrollBar->maximum(),
      totalHeight, logicalHeight, headerOffset);

  bool forceImmediate = computeForceImmediate(immediate);
  if (shouldEarlyReturnUserScroll(forceImmediate)) {
    return;
  }

  int curY = verticalScrollBar->value();
  int distance = qAbs(targetY - curY);

  int currentRow = (gridWidth > 0 ? index / gridWidth : -1);
  int smallThreshold = computeSmallThreshold(currentRow);

  bool useSmooth = forceClickAnim ||
                   (m_continuousScrollActive && !m_instantPositioning && !m_wrapSequenceActive) ||
                   (state() && state()->scroll().clickContinuous) ||
                   (state() && state()->scroll().keyContinuous);

  if (!forceImmediate && distance <= smallThreshold) {
    if (handleSmallMovementEarlyReturn(distance, clickScroll, index, currentRow)) {
      return;
    }
  }

  if (maybeHandleImmediateCenter(distance <= 1, useSmooth, forceImmediate, forceClickAnim,
                                 verticalScrollBar, targetY, index, currentRow)) {
    return;
  }

  if (animMgr()) {
    animMgr()->ensureVAnimCreated(verticalScrollBar);

    if (animMgr()->handleExistingVerticalAnimIfRunning(verticalScrollBar, targetY, clickScroll,
                                                       clickHoldAdv, curY, distance)) {
      return;
    }
  }

  int duration = computeVerticalCenterDuration(distance, m_repeating);

  if (forceClickAnim && distance <= 1) {
    adjustForForceClickZeroDistance(verticalScrollBar, targetY, curY, distance, duration,
                                    forceClickAnim);
  }

  if (animMgr()) {
    animMgr()->configureAndStartVerticalAnimation(verticalScrollBar, curY, targetY, duration,
                                                  clickScroll, clickHoldAdv);
  }
}

bool ViewportManager::computeForceImmediate(bool immediate) const {
  const bool restoringSelection = selectionMgr() && selectionMgr()->isRestoringSelection();
  return ViewportHelpers::computeForceImmediate(immediate, m_forceImmediateCenter,
                                                m_isWrappingNavigation, restoringSelection,
                                                m_instantPositioning, m_wrapSequenceActive);
}

int ViewportManager::computeSmallThreshold(int currentRow) const {
  return ViewportHelpers::smallMovementThreshold(m_lastSelectedRow, currentRow);
}

bool ViewportManager::handleSmallMovementEarlyReturn(int /*distance*/, bool clickScroll, int index,
                                                     int currentRow) {
  if (scrollGrid()) {
    scrollGrid()->updateVirtualView();
    int idxDyn =
        (state() && state()->isSelectionSuppressed()) ? state()->pendingSelectionIndex() : index;
    if (idxDyn >= 0) {
      scrollOverlay()->updateSelectionForIndex(idxDyn);
    }
  }
  if (clickScroll && state()) {
    state()->scroll().clickScroll = false;
  }
  if (state()) {
    state()->arrow().suppressArrowCenter = false;
  }
  m_instantPositioning = false;
  m_lastSelectedRow = currentRow;
  if (selectionMgr()) {
    selectionMgr()->setLastSelectedRow(currentRow);
  }
  return true;
}

bool ViewportManager::shouldDeferCenterNow(bool immediate, int index) const {
  if (immediate) {
    return false;
  }
  if (!state() || !state()->click().deferCenterOnClick) {
    return false;
  }
  int defIdx = state()->click().deferredCenterIndex;
  return (defIdx < 0 || defIdx == index);
}

bool ViewportManager::shouldEarlyReturnUserScroll(bool forceImmediate) const {
  // A continuous wheel scroll OWNS the viewport for as long as it runs, and
  // that outranks forceImmediate. Without this, centring fires while the
  // wheel's own glide is still in flight and animates the view back toward
  // the previous selection's centre — the reported "scrolling reverts".
  //
  // Caught in the act by the VANIM trace: every revert is a wheelScroll
  // immediately followed by a configureAndStart whose target is the wheel
  // glide's own START value, i.e. exactly where the user just scrolled away
  // from, and the drawn selection follows it back:
  //
  //   VANIM wheelScroll      from=83800 to=84085 delta=+285
  //   VANIM configureAndStart from=83887 to=83800 delta=-87
  //   SELDRAW 1482 -> 1476
  //
  // The existing guard could not catch it: it consults userScrollActive only,
  // and every centring on this path passes immediate=true, which sets
  // forceImmediate and bypasses the check outright.
  //
  // Scoped deliberately to the continuous-scroll window rather than
  // suppressing centring generally — outside it, immediate centring still
  // wins as before, which is what keyboard navigation and restore rely on.
  if (m_continuousScrollActive) {
    return true;
  }
  return (state() && state()->scroll().userScrollActive) && !forceImmediate;
}

bool ViewportManager::handlePendingInitialCenterIfNeeded(QScrollBar *verticalScrollBar, int index,
                                                         int targetYUnbounded, bool immediate) {
  Q_UNUSED(targetYUnbounded);
  if (verticalScrollBar->maximum() == 0 && !immediate) {
    if (state() && !state()->click().pendingInitialCenter) {
      state()->click().pendingInitialCenter = true;
      // Defer centering until scrollbar has a valid range - happens during
      // initial layout when content height isn't calculated yet
      QTimer::singleShot(UIConstants::DetailsPane::INITIAL_CENTER_SCROLL_DELAY_MS, this,
                         [this, index]() {
                           if (state()) {
                             state()->click().pendingInitialCenter = false;
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

void ViewportManager::adjustForForceClickZeroDistance(QScrollBar *verticalScrollBar, int targetY,
                                                      int &curY, int &distance, int &duration,
                                                      bool /*forceClickAnim*/) {
  if (targetY == curY) {
    int adjust = (targetY > 0 ? -1 : 1);
    if (state()) {
      state()->scroll().programmaticScroll = true;
    }
    if (scrollOverlay()) {
      scrollOverlay()->refreshSelectionOverlayState();
    }
    int startVal = qBound(0, targetY + adjust, verticalScrollBar->maximum());
    verticalScrollBar->setValue(startVal);
    // Defer clearing ProgrammaticScroll flag until after Qt processes the
    // setValue() - ensures the scroll event handler sees the flag is set
    QTimer::singleShot(0, this, [this]() {
      if (state()) {
        state()->scroll().programmaticScroll = false;
      }
      if (scrollOverlay()) {
        scrollOverlay()->refreshSelectionOverlayState();
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
  const CollectionConfig &collection = (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if ((!vScrollBar) || (!hScrollBar)) {
    return false;
  }
  QRect viewport = m_itemScrollArea->viewport()->rect();
  int viewportWidth = viewport.width();
  int viewportHeight = viewport.height();
  int gridWidth = collection.gridLayout.gridWidth;
  if (gridWidth <= 0 || viewportHeight <= 0) {
    return false;
  }
  int hSpacing = (scrollGrid()) ? scrollGrid()->getEffectiveHorizontalSpacing()
                                : collection.gridLayout.horizontalSpacing;
  int margins = UIConstants::Grid::MARGINS;
  int itemX =
      GridUtils::computeItemX(index, gridWidth, collection.gridLayout.itemWidth, hSpacing, margins);
  int itemY = GridUtils::computeItemY(index, gridWidth, collection.gridLayout.itemHeight,
                                      collection.gridLayout.verticalSpacing, margins);

  // Calculate target scroll position in logical space (center the item)
  int logicalTargetY = itemY + (collection.gridLayout.itemHeight / 2) - (viewportHeight / 2);
  logicalTargetY = qMax(0, logicalTargetY);

  // Convert logical scroll target to widget scroll position for clipped grids
  int targetY = toWidgetScrollY(logicalTargetY);
  targetY = qBound(0, targetY, vScrollBar->maximum());

  int targetX = GridUtils::computeCenterTarget(itemX, collection.gridLayout.itemWidth,
                                               viewportWidth, hScrollBar->maximum());
  vScrollBar->setValue(targetY);
  hScrollBar->setValue(targetX);
  if (scrollGrid()) {
    // When wrapping, clear all widgets to prevent stale artwork from showing
    // at wrong positions after the large scroll jump
    if (m_isWrappingNavigation) {
      scrollGrid()->cleanupActiveWidgets();
    }
    scrollGrid()->updateVirtualView();
  }
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
  if (selectionMgr()) {
    selectionMgr()->setLastSelectedRow(m_lastSelectedRow);
  }
  m_forceImmediateCenter = false;
  m_deferredCenterPending = false;
  return true;
}

bool ViewportManager::maybeHandleImmediateCenter(bool distanceSmall, bool useSmooth,
                                                 bool forceImmediate, bool forceClickAnim,
                                                 QScrollBar *verticalScrollBar, int targetY,
                                                 int index, int currentRow) {
  if (((distanceSmall && !useSmooth) || forceImmediate) && !forceClickAnim) {
    if (handleImmediateCenterPath(verticalScrollBar, targetY, index, currentRow)) {
      return true;
    }
  }
  return false;
}

bool ViewportManager::handleImmediateCenterPath(QScrollBar *verticalScrollBar, int targetY,
                                                int index, int currentRow) {
  if (animMgr()) {
    animMgr()->stopActiveVerticalAnims(verticalScrollBar);
  }
  setProgrammaticScrollGuarded(true);
  setScrollValueAndUpdateSelection(verticalScrollBar, targetY, index);
  setProgrammaticScrollGuarded(false);
  finalizeImmediateCenteringState(index, currentRow);
  clearArtworkSuppressionViewportUpdateIfNeeded();
  clearArrowCenterSuppressionWhenDue();
  return true;
}
