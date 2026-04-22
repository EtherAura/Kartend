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

  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0) {
    return;
  }

  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if (!hScrollBar) {
    return;
  }

  int hSpacing = (m_scrollManager)
                     ? m_scrollManager->getEffectiveHorizontalSpacing()
                     : collection.horizontalSpacing;
  int margins = UIConstants::Grid::MARGINS;
  int itemX = GridUtils::computeItemX(index, gridWidth, collection.itemWidth,
                                      hSpacing, margins);

  QRect viewport = m_itemScrollArea->viewport()->rect();
  int curX = hScrollBar->value();
  int viewportWidth = viewport.width();
  int targetX = curX;

  if (itemX < curX + margins) {
    targetX = qMax(0, itemX - margins);
  } else if (itemX + collection.itemWidth > curX + viewportWidth - margins) {
    targetX =
        qMax(0, qMin(itemX + collection.itemWidth - viewportWidth + margins,
                     hScrollBar->maximum()));
  }

  if (targetX == curX) {
    if (m_scrollManager) {
      m_scrollManager->updateVirtualView();
    }
    return;
  }

  bool hold = m_state ? m_state->scroll().horizHoldActive : false;

  if (m_animationManager) {
    m_animationManager->initHorizontalAnimIfNeeded(hScrollBar);

    if (hold) {
      int startX = hScrollBar->value();
      m_animationManager->animateHorizontalHold(hScrollBar, startX, targetX);
    } else {
      m_animationManager->animateHorizontalSmooth(hScrollBar, curX, targetX);
    }
  }

  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
  }
}

void ViewportManager::ensureItemVisible(int index, bool allowHorizontalScroll) {
  if (shouldExitEnsureItemVisible(index)) {
    return;
  }

  if (m_state && m_state->click().deferCenterOnClick && !m_physicalKeyDown) {
    return;
  }
  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];

  // Get metrics from ScrollManager for correct dimensions in both grid and list
  // modes
  int gridWidth = collection.gridWidth;
  int itemHeight = collection.itemHeight;
  int itemWidth = collection.itemWidth;
  int vSpacing = collection.verticalSpacing;
  int hSpacing = collection.horizontalSpacing;

  if (m_scrollManager) {
    const auto &metrics = m_scrollManager->getMetrics();
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

  int itemX =
      GridUtils::computeItemX(index, gridWidth, itemWidth, hSpacing, margins);
  int logicalItemY =
      GridUtils::computeItemY(index, gridWidth, itemHeight, vSpacing, margins);

  // Account for header offset in list mode
  if (m_scrollManager) {
    const auto &metrics = m_scrollManager->getMetrics();
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
  if (m_scrollManager) {
    const auto &metrics = m_scrollManager->getMetrics();
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
                          itemX, itemWidth, curX, viewportWidth, margins,
                          hScrollBar->maximum())
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
  if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
    m_animationManager->verticalAnimation()->stop();
    startVal = vScrollBar->value();
  }

  startEnsureVisibleVAnim(vScrollBar, startVal, endVal, isCurrentlyRepeating);
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
  if (m_selectionManager) {
    m_selectionManager->setLastSelectedRow(m_lastSelectedRow);
  }
}

void ViewportManager::updateViewAndRowAfterVisibility(int index,
                                                      int gridWidth) {
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
  }
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
  if (m_selectionManager) {
    m_selectionManager->setLastSelectedRow(m_lastSelectedRow);
  }
}

bool ViewportManager::shouldExitEnsureItemVisible(int index) const {
  if (QApplication::closingDown() ||
      ((m_isShuttingDown) && *m_isShuttingDown)) {
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

void ViewportManager::startEnsureVisibleVAnim(QScrollBar *vScrollBar,
                                              int startVal, int endVal,
                                              bool isRepeating) {
  if (!m_animationManager) {
    return;
  }

  int itemHeight = 0;
  int vSpacing = 0;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    itemHeight = (*m_collections)[*m_currentCollectionIndex].itemHeight;
    vSpacing = (*m_collections)[*m_currentCollectionIndex].verticalSpacing;
  }

  m_animationManager->startEnsureVisibleVAnim(
      vScrollBar, startVal, endVal, itemHeight, vSpacing, isRepeating);
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

void ViewportManager::applyImmediateViewportPositioningForSelection(
    int targetIndex) {
  if (!m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }
  if (targetIndex < 0) {
    return;
  }

  QScrollBar *verticalScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *horizontalScrollBar = m_itemScrollArea->horizontalScrollBar();

  if ((verticalScrollBar) && *m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size()) {
    // Use ScrollManager metrics for accurate list/grid mode dimensions
    int gridWidth = 1;
    int rowHeight = UIConstants::ListView::DEFAULT_ROW_HEIGHT +
                    UIConstants::ListView::ROW_SPACING;
    int itemHeight = UIConstants::ListView::DEFAULT_ROW_HEIGHT;
    int headerOffset = 0;

    if (m_scrollManager) {
      const auto &metrics = m_scrollManager->getMetrics();
      gridWidth = metrics.itemsPerRow > 0 ? metrics.itemsPerRow : 1;
      rowHeight = metrics.itemHeight + metrics.verticalSpacing;
      itemHeight = metrics.itemHeight;
      headerOffset = metrics.headerOffset;
    } else {
      // Fallback to collection config for grid mode
      const CollectionConfig &collection =
          (*m_collections)[*m_currentCollectionIndex];
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
        int itemY =
            UIConstants::Grid::MARGINS + headerOffset + (row * rowHeight);
        int targetY = itemY + (itemHeight / 2) - (viewportH / 2);
        targetY = qBound(0, targetY, qMax(0, targetY));
        AnimationManager::stopArrowKeyAnimationIfRunning(verticalScrollBar);
        if (m_state) {
          m_state->scroll().programmaticScroll = true;
        }
        if (m_scrollManager) {
          m_scrollManager->refreshSelectionOverlayState();
        }
        verticalScrollBar->setValue(targetY);
        QPointer<ScrollManager> scrollMgrPtr = m_scrollManager;
        InteractionStateHolder *statePtr = m_state;
        // Defer clearing ProgrammaticScroll flag until after Qt processes
        // the setValue() - ensures scroll restoration completes atomically
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
        const CollectionConfig &collection =
            (*m_collections)[*m_currentCollectionIndex];
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
  if (m_state) {
    m_state->arrow().suppressArrowCenter = true;
    m_state->scroll().userScrollActive = false;
    m_state->arrow().suppressArrowCenterUntilMs =
        QDateTime::currentMSecsSinceEpoch() +
        UIConstants::Keyboard::ANIMATION_SETTLE_MS;
  }
}
