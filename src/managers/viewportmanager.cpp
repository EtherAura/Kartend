// Manages viewport positioning, item centering, and scroll-to-visible operations.
#include "viewportmanager.h"
#include "animationmanager.h"
#include "artworkmanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"

#include "gridutils.h"
#include "propertyutils.h"
#include "uiconstants.h"

#include <QApplication>
#include <QDateTime>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include "mainwindow.h"

ViewportManager::ViewportManager(QObject *parent) : QObject(parent) {}

ViewportManager::~ViewportManager() = default;

void ViewportManager::setupReferences(const ViewportManagerSetup &setup) {
  m_itemScrollArea = setup.itemScrollArea;
  m_scrollManager = setup.scrollManager;
  m_selectionManager = setup.selectionManager;
  m_animationManager = setup.animationManager;
  m_artworkManager = setup.artworkManager;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_isShuttingDown = setup.isShuttingDown;

  // Connect to AnimationManager's finished signal
  if (m_animationManager) {
    connect(m_animationManager, &AnimationManager::verticalAnimationFinished,
            this, &ViewportManager::onVScrollAnimationFinished);
  }
}

int ViewportManager::getCurrentGridWidth() const {
  // Prefer ScrollManager's value for filtered/nested views
  if (m_scrollManager != nullptr) {
    int width = m_scrollManager->getCurrentGridWidth();
    if (width > 0) {
      return width;
    }
  }
  if (m_collections == nullptr || m_currentCollectionIndex == nullptr) {
    return UIConstants::DEFAULT_GRID_WIDTH;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return UIConstants::DEFAULT_GRID_WIDTH;
  }
  return (*m_collections)[*m_currentCollectionIndex].gridWidth;
}

int ViewportManager::computeVerticalCenterDuration(int distance,
                                                   bool repeatActive) const {
  int itemHeight = 0;
  int vSpacing = 0;
  if ((m_collections != nullptr) && (m_currentCollectionIndex != nullptr) &&
      *m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size()) {
    itemHeight = (*m_collections)[*m_currentCollectionIndex].itemHeight;
    vSpacing = (*m_collections)[*m_currentCollectionIndex].verticalSpacing;
  }
  return AnimationManager::computeVerticalCenterDuration(distance, itemHeight,
                                                         vSpacing, repeatActive);
}

void ViewportManager::centerItemVertically(int index, bool immediate) {
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
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
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0) {
    return;
  }

  QScrollBar *verticalScrollBar = m_itemScrollArea->verticalScrollBar();
  if (verticalScrollBar == nullptr) {
    return;
  }
  int viewportHeight = m_itemScrollArea->viewport()->height();
  if (viewportHeight <= 0) {
    return;
  }

  bool clickScroll = property(PropertyKeys::ClickScroll).toBool();
  bool clickHoldAdv = property(PropertyKeys::ClickHoldAdvancing).toBool();
  bool forceClickAnim = property(PropertyKeys::ClickForceAnim).toBool();

  int targetY = AnimationManager::computeTargetYForIndex(
      index, gridWidth, collection.itemHeight, collection.verticalSpacing,
      viewportHeight, verticalScrollBar->maximum());

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
                   property(PropertyKeys::ClickContinuous).toBool() ||
                   property(PropertyKeys::KeyContinuous).toBool();

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
  return immediate || m_forceImmediateCenter || m_isWrappingNavigation ||
         m_restoringSelection || m_instantPositioning || m_wrapSequenceActive;
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
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
    int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                     ? property(PropertyKeys::PendingSelectionIndex).toInt()
                     : index;
    if (idxDyn >= 0) {
      m_scrollManager->updateSelectionForIndex(idxDyn);
    }
  }
  if (clickScroll) {
    setProperty(PropertyKeys::ClickScroll, false);
  }
  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, false);
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
  if (!property(PropertyKeys::DeferCenterOnClick).toBool()) {
    return false;
  }
  int defIdx = property(PropertyKeys::DeferredCenterIndex).toInt();
  return (defIdx < 0 || defIdx == index);
}

bool ViewportManager::shouldEarlyReturnUserScroll(bool forceImmediate) const {
  return m_itemScrollArea->property(PropertyKeys::UserScrollActive).toBool() &&
         !forceImmediate;
}

bool ViewportManager::handlePendingInitialCenterIfNeeded(
    QScrollBar *verticalScrollBar, int index, int targetYUnbounded,
    bool immediate) {
  Q_UNUSED(targetYUnbounded);
  if (verticalScrollBar->maximum() == 0 && !immediate) {
    if (!property(PropertyKeys::PendingInitialCenter).toBool()) {
      setProperty(PropertyKeys::PendingInitialCenter, true);
      QTimer::singleShot(
          UIConstants::INITIAL_CENTER_SCROLL_DELAY_MS, this, [this, index]() {
            setProperty(PropertyKeys::PendingInitialCenter, false);
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
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
    int startVal = qBound(0, targetY + adjust, verticalScrollBar->maximum());
    verticalScrollBar->setValue(startVal);
    QTimer::singleShot(0, this, [this]() {
      if (m_itemScrollArea) {
        m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, false);
        if (m_scrollManager != nullptr) {
          m_scrollManager->refreshSelectionOverlayState();
        }
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
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return false;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return false;
  }
  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if ((vScrollBar == nullptr) || (hScrollBar == nullptr)) {
    return false;
  }
  QRect viewport = m_itemScrollArea->viewport()->rect();
  int viewportWidth = viewport.width();
  int viewportHeight = viewport.height();
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0 || viewportHeight <= 0) {
    return false;
  }
  int hSpacing = (m_scrollManager != nullptr)
                     ? m_scrollManager->getEffectiveHorizontalSpacing()
                     : collection.horizontalSpacing;
  int margins = UIConstants::GRID_MARGINS;
  int itemX = GridUtils::computeItemX(index, gridWidth, collection.itemWidth,
                                      hSpacing, margins);
  int itemY = GridUtils::computeItemY(index, gridWidth, collection.itemHeight,
                                      collection.verticalSpacing, margins);

  int targetY = GridUtils::computeCenterTarget(
      itemY, collection.itemHeight, viewportHeight, vScrollBar->maximum());
  int targetX = GridUtils::computeCenterTarget(
      itemX, collection.itemWidth, viewportWidth, hScrollBar->maximum());
  vScrollBar->setValue(targetY);
  hScrollBar->setValue(targetX);
  if (m_scrollManager != nullptr) {
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

void ViewportManager::setProgrammaticScrollGuarded(bool enable) {
  if (!m_itemScrollArea) {
    return;
  }
  if (enable) {
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  } else {
    QPointer<QScrollArea> scrollAreaPtr = m_itemScrollArea;
    QPointer<ScrollManager> scrollMgrPtr = m_scrollManager;
    QTimer::singleShot(0, this, [scrollAreaPtr, scrollMgrPtr]() {
      if (scrollAreaPtr) {
        scrollAreaPtr->setProperty(PropertyKeys::ProgrammaticScroll, false);
        if (scrollMgrPtr) {
          scrollMgrPtr->refreshSelectionOverlayState();
        }
      }
    });
  }
}

void ViewportManager::setScrollValueAndUpdateSelection(
    QScrollBar *verticalScrollBar, int targetY, int index) {
  verticalScrollBar->setValue(targetY);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
    int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                     ? property(PropertyKeys::PendingSelectionIndex).toInt()
                     : index;
    if (idxDyn >= 0) {
      m_scrollManager->updateSelectionForIndex(idxDyn);
    }
  }
}

void ViewportManager::clearArtworkSuppressionViewportUpdateIfNeeded() {
  if (m_itemScrollArea &&
      m_itemScrollArea->property(PropertyKeys::SuppressArtwork).toBool() &&
      !m_repeating) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, false);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
    QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, this, [this]() {
      if (!QApplication::closingDown() && m_artworkManager) {
        m_artworkManager->updateViewportArtwork();
      }
    });
  }
}

void ViewportManager::clearArrowCenterSuppressionWhenDue() {
  if (!m_itemScrollArea) {
    return;
  }
  qint64 until =
      m_itemScrollArea->property(PropertyKeys::SuppressArrowCenterUntilMs)
          .toLongLong();
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (until > now) {
    qint64 delay = until - now;
    QPointer<QScrollArea> scrollAreaPtr = m_itemScrollArea;
    constexpr qint64 kMaxArrowCenterSuppressClearMs = 1000;
    QTimer::singleShot(
        static_cast<int>(qMin<qint64>(delay, kMaxArrowCenterSuppressClearMs)),
        this, [scrollAreaPtr]() {
          if (scrollAreaPtr) {
            scrollAreaPtr->setProperty(PropertyKeys::SuppressArrowCenter,
                                       false);
          }
        });
  } else {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, false);
  }
}

void ViewportManager::finalizeImmediateCenteringState(int index,
                                                      int currentRow) {
  if (m_restoringSelection && index == m_targetRestoreIndex) {
    m_restoringSelection = false;
    m_targetRestoreIndex = -1;
  }
  m_isWrappingNavigation = false;
  m_forceImmediateCenter = false;
  if (m_wrapSequenceActive) {
    m_wrapSequenceActive = false;
    m_continuousScrollActive = true;
  }
  if (property(PropertyKeys::ClickScroll).toBool()) {
    setProperty(PropertyKeys::ClickScroll, false);
  }
  if (!m_repeating && !m_physicalKeyDown &&
      !property(PropertyKeys::ClickContinuous).toBool() &&
      !property(PropertyKeys::KeyContinuous).toBool()) {
    m_continuousScrollActive = false;
  }
  m_instantPositioning = false;
  m_lastSelectedRow = currentRow;
  if (m_selectionManager) {
    m_selectionManager->setLastSelectedRow(currentRow);
  }
}

void ViewportManager::onVScrollAnimationFinished() {
  setProperty(PropertyKeys::ClickForceAnim, false);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
    int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                     ? property(PropertyKeys::PendingSelectionIndex).toInt()
                     : -1;
    // Signal that we need a selection update (InteractionManager will handle)
    emit requestSelectionUpdate(idxDyn);
  }
  if (!m_repeating && !m_physicalKeyDown &&
      !property(PropertyKeys::ClickContinuous).toBool() &&
      !property(PropertyKeys::KeyContinuous).toBool()) {
    m_continuousScrollActive = false;
  }
  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, false);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  if (m_itemScrollArea && !m_repeating && !m_physicalKeyDown) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, false);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
    QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, this, [this]() {
      if (!QApplication::closingDown() && m_artworkManager) {
        m_artworkManager->updateViewportArtwork();
      }
    });
  }
  setProperty(PropertyKeys::ClickScroll, false);
  m_instantPositioning = false;
  int gridWidthLocal = getCurrentGridWidth();
  int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                   ? property(PropertyKeys::PendingSelectionIndex).toInt()
                   : -1;
  if (gridWidthLocal > 0 && idxDyn >= 0) {
    m_lastSelectedRow = idxDyn / gridWidthLocal;
    if (m_selectionManager) {
      m_selectionManager->setLastSelectedRow(m_lastSelectedRow);
    }
  }
}

void ViewportManager::ensureHorizontallyVisible(int index) {
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
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
  if (hScrollBar == nullptr) {
    return;
  }

  int hSpacing = (m_scrollManager != nullptr)
                     ? m_scrollManager->getEffectiveHorizontalSpacing()
                     : collection.horizontalSpacing;
  int margins = UIConstants::GRID_MARGINS;
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
    if (m_scrollManager != nullptr) {
      m_scrollManager->updateVirtualView();
    }
    return;
  }

  bool hold = property(PropertyKeys::HorizHoldActive).toBool();

  if (m_animationManager) {
    m_animationManager->initHorizontalAnimIfNeeded(hScrollBar);

    if (hold) {
      int startX = hScrollBar->value();
      m_animationManager->animateHorizontalHold(hScrollBar, startX, targetX);
    } else {
      m_animationManager->animateHorizontalSmooth(hScrollBar, curX, targetX);
    }
  }

  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
  }
}

void ViewportManager::ensureItemVisible(int index, bool allowHorizontalScroll) {
  if (shouldExitEnsureItemVisible(index)) {
    return;
  }

  if (property(PropertyKeys::DeferCenterOnClick).toBool() &&
      !m_physicalKeyDown) {
    return;
  }
  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0) {
    return;
  }

  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if ((vScrollBar == nullptr) || (hScrollBar == nullptr)) {
    return;
  }

  int hSpacing = (m_scrollManager != nullptr)
                     ? m_scrollManager->getEffectiveHorizontalSpacing()
                     : collection.horizontalSpacing;
  int margins = UIConstants::GRID_MARGINS;

  int itemX = GridUtils::computeItemX(index, gridWidth, collection.itemWidth,
                                      hSpacing, margins);
  int itemY = GridUtils::computeItemY(index, gridWidth, collection.itemHeight,
                                      collection.verticalSpacing, margins);

  QRect viewport = m_itemScrollArea->viewport()->rect();
  int curX = hScrollBar->value();
  int curY = vScrollBar->value();
  int viewportWidth = viewport.width();
  int viewportHeight = viewport.height();
  if (viewportHeight <= 0) {
    return;
  }

  bool isRepeating = m_repeating && m_physicalKeyDown;

  if (handleImmediateCenterForEnsureVisible(index)) {
    return;
  }

  int targetX = allowHorizontalScroll
                    ? AnimationManager::computeHorizontalTargetX(
                          itemX, collection.itemWidth, curX, viewportWidth,
                          margins, hScrollBar->maximum())
                    : curX;
  bool needH = (targetX != curX);

  bool needV = false;
  int desiredY = AnimationManager::computeDesiredYForVisibility(
      itemY, collection.itemHeight, curY, viewportHeight, margins, needV);

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

  startEnsureVisibleVAnim(vScrollBar, startVal, endVal, isRepeating);
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
  if (m_selectionManager) {
    m_selectionManager->setLastSelectedRow(m_lastSelectedRow);
  }
}

void ViewportManager::updateViewAndRowAfterVisibility(int index,
                                                      int gridWidth) {
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
  }
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
  if (m_selectionManager) {
    m_selectionManager->setLastSelectedRow(m_lastSelectedRow);
  }
}

bool ViewportManager::shouldExitEnsureItemVisible(int index) const {
  if (QApplication::closingDown() ||
      ((m_isShuttingDown != nullptr) && *m_isShuttingDown)) {
    return true;
  }
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return true;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
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
  if ((m_collections != nullptr) && (m_currentCollectionIndex != nullptr) &&
      *m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size()) {
    itemHeight = (*m_collections)[*m_currentCollectionIndex].itemHeight;
    vSpacing = (*m_collections)[*m_currentCollectionIndex].verticalSpacing;
  }

  m_animationManager->startEnsureVisibleVAnim(vScrollBar, startVal, endVal,
                                              itemHeight, vSpacing, isRepeating);
}

void ViewportManager::ensureVerticalScrollbarPolicy() {
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  int idx = *m_currentCollectionIndex;
  if (idx < 0 || idx >= m_collections->size()) {
    return;
  }
  if (!(*m_collections)[idx].hideVerticalScrollbar) {
    m_itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

void ViewportManager::applyImmediateViewportPositioningForSelection(
    int targetIndex) {
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return;
  }
  if (targetIndex < 0) {
    return;
  }

  QScrollBar *verticalScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *horizontalScrollBar = m_itemScrollArea->horizontalScrollBar();

  if ((verticalScrollBar != nullptr) && *m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size()) {
    const CollectionConfig &collection =
        (*m_collections)[*m_currentCollectionIndex];
    if (collection.gridWidth > 0) {
      int row = targetIndex / collection.gridWidth;
      int col = targetIndex % collection.gridWidth;

      int viewportH = m_itemScrollArea->viewport()->height();
      int viewportW = m_itemScrollArea->viewport()->width();

      if (viewportH > 0) {
        int itemY =
            UIConstants::GRID_MARGINS +
            (row * (collection.itemHeight + collection.verticalSpacing));
        int targetY = itemY + (collection.itemHeight / 2) - (viewportH / 2);
        targetY = qBound(0, targetY, qMax(0, targetY));
        AnimationManager::stopArrowKeyAnimationIfRunning(verticalScrollBar);
        m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
        if (m_scrollManager != nullptr) {
          m_scrollManager->refreshSelectionOverlayState();
        }
        verticalScrollBar->setValue(targetY);
        QPointer<QScrollArea> scrollAreaPtr = m_itemScrollArea;
        QPointer<ScrollManager> scrollMgrPtr = m_scrollManager;
        QTimer::singleShot(0, this, [scrollAreaPtr, scrollMgrPtr]() {
          if (scrollAreaPtr) {
            scrollAreaPtr->setProperty(PropertyKeys::ProgrammaticScroll, false);
            if (scrollMgrPtr) {
              scrollMgrPtr->refreshSelectionOverlayState();
            }
          }
        });
      }

      if (viewportW > 0 && (horizontalScrollBar != nullptr)) {
        int itemX =
            UIConstants::GRID_MARGINS +
            (col * (collection.itemWidth + collection.horizontalSpacing));
        int targetX = itemX + (collection.itemWidth / 2) - (viewportW / 2);
        targetX = qBound(0, targetX, qMax(0, targetX));
        horizontalScrollBar->setValue(targetX);
      }
    }
  }
}

void ViewportManager::applyImmediateCenterSuppression() {
  m_forceImmediateCenter = true;
  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, true);
    m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, false);
    const qint64 until = QDateTime::currentMSecsSinceEpoch() +
                         UIConstants::ARROW_KEY_ANIMATION_SETTLE_MS;
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs,
                                  until);
  }
}
