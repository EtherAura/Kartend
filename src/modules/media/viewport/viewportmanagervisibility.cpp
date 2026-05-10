// Visibility / "ensure visible" methods extracted from viewportmanager.cpp:
//   - ensureHorizontallyVisible
//   - ensureItemVisible
//   - updateViewAndRowAfterVisibility
//   - shouldExitEnsureItemVisible
//   - startEnsureVisibleVAnim
//   - ensureVerticalScrollbarPolicy
//   - applyImmediateViewportPositioningForSelection
//   - applyImmediateCenterSuppression
// All operate on existing ViewportManager state.
#include "animationmanager.h"
#include "collectionutils.h"
#include "gridlayoutcalculator.h"
#include "gridutils.h"
#include "interactionstateholder.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"
#include <QApplication>
#include <QDateTime>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

void ViewportManager::ensureHorizontallyVisible(int index) {
  if (!m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }
  if (index < 0) {
    return;
  }

  // in Horizontal view "ensure horizontally visible" reduces to
  // a no-op — the item's column always fits in the viewport's height, and the
  // long-axis scrolling is handled by centerItemVertically (which dispatches
  // to the horizontal scrollbar in this mode).
  if (scrollMgr() && scrollMgr()->getMetrics().isHorizontal) {
    if (scrollMgr()) {
      scrollMgr()->updateVirtualView();
    }
    return;
  }

  const CollectionConfig &collection = (*m_collections)[*m_currentCollectionIndex];
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0) {
    return;
  }

  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if (!hScrollBar) {
    return;
  }

  int hSpacing = (scrollMgr()) ? scrollMgr()->getEffectiveHorizontalSpacing()
                                   : collection.horizontalSpacing;
  int margins = UIConstants::Grid::MARGINS;
  int itemX = GridUtils::computeItemX(index, gridWidth, collection.itemWidth, hSpacing, margins);

  QRect viewport = m_itemScrollArea->viewport()->rect();
  int curX = hScrollBar->value();
  int viewportWidth = viewport.width();
  int targetX = curX;

  if (itemX < curX + margins) {
    targetX = qMax(0, itemX - margins);
  } else if (itemX + collection.itemWidth > curX + viewportWidth - margins) {
    targetX = qMax(
        0, qMin(itemX + collection.itemWidth - viewportWidth + margins, hScrollBar->maximum()));
  }

  if (targetX == curX) {
    if (scrollMgr()) {
      scrollMgr()->updateVirtualView();
    }
    return;
  }

  bool hold = state() ? state()->scroll().horizHoldActive : false;

  if (animMgr()) {
    animMgr()->initHorizontalAnimIfNeeded(hScrollBar);

    if (hold) {
      int startX = hScrollBar->value();
      animMgr()->animateHorizontalHold(hScrollBar, startX, targetX);
    } else {
      animMgr()->animateHorizontalSmooth(hScrollBar, curX, targetX);
    }
  }

  if (scrollMgr()) {
    scrollMgr()->updateVirtualView();
  }
}

void ViewportManager::ensureItemVisible(int index, bool allowHorizontalScroll) {
  if (shouldExitEnsureItemVisible(index)) {
    return;
  }

  if (state() && state()->click().deferCenterOnClick && !m_physicalKeyDown) {
    return;
  }

  // in Horizontal view, "ensure visible" defers to the
  // horizontal-axis centering path — the only scroll axis there.
  if (scrollMgr() && scrollMgr()->getMetrics().isHorizontal) {
    Q_UNUSED(allowHorizontalScroll);
    centerItemVertically(index, /*immediate=*/true);
    return;
  }

  const CollectionConfig &collection = (*m_collections)[*m_currentCollectionIndex];

  // Get metrics from ScrollManager for correct dimensions in both grid and list
  // modes
  int gridWidth = collection.gridWidth;
  int itemHeight = collection.itemHeight;
  int itemWidth = collection.itemWidth;
  int vSpacing = collection.verticalSpacing;
  int hSpacing = collection.horizontalSpacing;

  if (scrollMgr()) {
    const auto &metrics = scrollMgr()->getMetrics();
    gridWidth = metrics.itemsPerRow;
    itemHeight = metrics.itemHeight;
    itemWidth = metrics.itemWidth;
    vSpacing = metrics.verticalSpacing;
    hSpacing = metrics.horizontalSpacing;
  }

  if (gridWidth <= 0) {
    return;
  }

  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if ((!vScrollBar) || (!hScrollBar)) {
    return;
  }

  int margins = UIConstants::Grid::MARGINS;

  int itemX = GridUtils::computeItemX(index, gridWidth, itemWidth, hSpacing, margins);
  int logicalItemY = GridUtils::computeItemY(index, gridWidth, itemHeight, vSpacing, margins);

  // Account for header offset in list mode
  if (scrollMgr()) {
    const auto &metrics = scrollMgr()->getMetrics();
    if (metrics.headerOffset > 0) {
      logicalItemY += metrics.headerOffset;
    }
  }

  QRect viewport = m_itemScrollArea->viewport()->rect();
  int curY = vScrollBar->value();
  int curX = hScrollBar->value();
  int viewportWidth = viewport.width();
  int viewportHeight = viewport.height();
  if (viewportHeight <= 0) {
    return;
  }

  // For clipped grids, convert to logical scroll position for visibility check
  int logicalCurY = curY;
  if (scrollMgr()) {
    const auto &metrics = scrollMgr()->getMetrics();
    if (metrics.isClipped) {
      logicalCurY = metrics.toLogicalScrollY(curY, viewportHeight);
    }
  }

  bool isCurrentlyRepeating = m_repeating && m_physicalKeyDown;

  if (handleImmediateCenterForEnsureVisible(index)) {
    return;
  }

  int targetX = allowHorizontalScroll
                    ? AnimationManager::computeHorizontalTargetX(
                          itemX, itemWidth, curX, viewportWidth, margins, hScrollBar->maximum())
                    : curX;
  bool needH = (targetX != curX);

  bool needV = false;
  // Check visibility in logical space
  int logicalDesiredY = AnimationManager::computeDesiredYForVisibility(
      logicalItemY, itemHeight, logicalCurY, viewportHeight, margins, needV);

  if (!needV && !needH) {
    updateViewAndRowAfterVisibility(index, gridWidth);
    return;
  }

  if (needH) {
    hScrollBar->setValue(targetX);
  }

  if (!needV) {
    updateViewAndRowAfterVisibility(index, gridWidth);
    return;
  }

  // Convert logical scroll target to widget scroll position
  int desiredY = toWidgetScrollY(logicalDesiredY);
  desiredY = qBound(0, desiredY, vScrollBar->maximum());

  int startVal = curY;
  int endVal = desiredY;
  if (startVal == endVal) {
    updateViewAndRowAfterVisibility(index, gridWidth);
    return;
  }

  // Stop any running animation and update start position
  if (animMgr() && animMgr()->isVerticalAnimRunning()) {
    animMgr()->verticalAnimation()->stop();
    startVal = vScrollBar->value();
  }

  startEnsureVisibleVAnim(vScrollBar, startVal, endVal, isCurrentlyRepeating);
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
  if (selectionMgr()) {
    selectionMgr()->setLastSelectedRow(m_lastSelectedRow);
  }
}

void ViewportManager::updateViewAndRowAfterVisibility(int index, int gridWidth) {
  if (scrollMgr()) {
    scrollMgr()->updateVirtualView();
  }
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
  if (selectionMgr()) {
    selectionMgr()->setLastSelectedRow(m_lastSelectedRow);
  }
}

bool ViewportManager::shouldExitEnsureItemVisible(int index) const {
  if (QApplication::closingDown() || ((m_isShuttingDown) && *m_isShuttingDown)) {
    return true;
  }
  if (!m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return true;
  }
  if (index < 0) {
    return true;
  }
  return false;
}

void ViewportManager::startEnsureVisibleVAnim(QScrollBar *vScrollBar, int startVal, int endVal,
                                              bool isRepeating) {
  if (!animMgr()) {
    return;
  }

  int itemHeight = 0;
  int vSpacing = 0;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    itemHeight = (*m_collections)[*m_currentCollectionIndex].itemHeight;
    vSpacing = (*m_collections)[*m_currentCollectionIndex].verticalSpacing;
  }

  animMgr()->startEnsureVisibleVAnim(vScrollBar, startVal, endVal, itemHeight, vSpacing,
                                              isRepeating);
}

void ViewportManager::ensureVerticalScrollbarPolicy() {
  if (!m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }
  int idx = *m_currentCollectionIndex;
  if (!(*m_collections)[idx].hideVerticalScrollbar) {
    m_itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

void ViewportManager::applyImmediateViewportPositioningForSelection(int targetIndex) {
  if (!m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }
  if (targetIndex < 0) {
    return;
  }

  // in Horizontal mode, snap the horizontal scrollbar so the
  // wrapped selection is centered along the long axis. Vertical position is
  // unused (column always visible).
  if (scrollMgr() && scrollMgr()->getMetrics().isHorizontal) {
    QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
    if (!hScrollBar) {
      return;
    }
    const auto &metrics = scrollMgr()->getMetrics();
    int viewportW = m_itemScrollArea->viewport()->width();
    if (viewportW <= 0) {
      return;
    }
    int targetX = GridLayoutCalculator::calculateCenterScrollTarget(targetIndex, viewportW,
                                                                    hScrollBar->maximum(), metrics);
    hScrollBar->setValue(qBound(0, targetX, hScrollBar->maximum()));
    return;
  }

  QScrollBar *verticalScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *horizontalScrollBar = m_itemScrollArea->horizontalScrollBar();

  if ((verticalScrollBar) && *m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size()) {
    // Use ScrollManager metrics for accurate list/grid mode dimensions
    int gridWidth = 1;
    int rowHeight = UIConstants::ListView::DEFAULT_ROW_HEIGHT + UIConstants::ListView::ROW_SPACING;
    int itemHeight = UIConstants::ListView::DEFAULT_ROW_HEIGHT;
    int headerOffset = 0;

    if (scrollMgr()) {
      const auto &metrics = scrollMgr()->getMetrics();
      gridWidth = metrics.itemsPerRow > 0 ? metrics.itemsPerRow : 1;
      rowHeight = metrics.itemHeight + metrics.verticalSpacing;
      itemHeight = metrics.itemHeight;
      headerOffset = metrics.headerOffset;
    } else {
      // Fallback to collection config for grid mode
      const CollectionConfig &collection = (*m_collections)[*m_currentCollectionIndex];
      if (collection.gridWidth > 0) {
        gridWidth = collection.gridWidth;
        rowHeight = GridLayoutCalculator::getRowHeight(collection);
        itemHeight = collection.itemHeight;
      }
    }

    if (gridWidth > 0) {
      int row = targetIndex / gridWidth;
      int col = targetIndex % gridWidth;

      int viewportH = m_itemScrollArea->viewport()->height();
      int viewportW = m_itemScrollArea->viewport()->width();

      if (viewportH > 0) {
        int itemY = UIConstants::Grid::MARGINS + headerOffset + (row * rowHeight);
        int targetY = itemY + (itemHeight / 2) - (viewportH / 2);
        targetY = qBound(0, targetY, qMax(0, targetY));
        AnimationManager::stopArrowKeyAnimationIfRunning(verticalScrollBar);
        if (state()) {
          state()->scroll().programmaticScroll = true;
        }
        if (scrollMgr()) {
          scrollMgr()->refreshSelectionOverlayState();
        }
        verticalScrollBar->setValue(targetY);
        QPointer<ScrollManager> scrollMgrPtr = scrollMgr();
        QPointer<InteractionStateHolder> statePtr = state();
        // Defer clearing ProgrammaticScroll flag until after Qt processes
        // the setValue() - ensures scroll restoration completes atomically.
        // Use QPointer so the lambda is lifetime-safe if either object is
        // destroyed before the timer fires.
        QTimer::singleShot(0, this, [statePtr, scrollMgrPtr]() {
          if (statePtr) {
            statePtr->scroll().programmaticScroll = false;
          }
          if (scrollMgrPtr) {
            scrollMgrPtr->refreshSelectionOverlayState();
          }
        });
      }

      if (viewportW > 0 && (horizontalScrollBar)) {
        const CollectionConfig &collection = (*m_collections)[*m_currentCollectionIndex];
        int colWidth = GridLayoutCalculator::getColumnWidth(collection);
        int itemX = UIConstants::Grid::MARGINS + (col * colWidth);
        int targetX = itemX + (collection.itemWidth / 2) - (viewportW / 2);
        targetX = qBound(0, targetX, qMax(0, targetX));
        horizontalScrollBar->setValue(targetX);
      }
    }
  }
}

void ViewportManager::applyImmediateCenterSuppression() {
  m_forceImmediateCenter = true;
  if (state()) {
    state()->arrow().suppressArrowCenter = true;
    state()->scroll().userScrollActive = false;
    state()->arrow().suppressArrowCenterUntilMs =
        QDateTime::currentMSecsSinceEpoch() + UIConstants::Keyboard::ANIMATION_SETTLE_MS;
  }
}
