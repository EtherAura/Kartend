// Handles mouse input including click-hold scrolling, wheel events, and widget finding.
#include "mousemanager.h"
#include "applicationcontext.h"
#include "collectionutils.h"
#include "gridlayoutcalculator.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidget>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcMouseManager, "kartend.mousemanager")
#define debugLog(msg) do { if (lcMouseManager().isDebugEnabled()) { qCDebug(lcMouseManager) << msg; } } while (0)

// MouseManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(MouseManagerSetup, ScrollManager*, ScrollManager, scrollManager)
SETUP_GETTER_DEF_SAME(MouseManagerSetup, SelectionManager*, SelectionManager, selectionManager)
SETUP_GETTER_DEF_SAME(MouseManagerSetup, QScrollArea*, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(MouseManagerSetup, QWidget*, GridContainer, gridContainer)
SETUP_GETTER_DEF_SAME(MouseManagerSetup, const QVector<CollectionConfig>*, Collections, collections)
SETUP_GETTER_DEF_SAME(MouseManagerSetup, const int*, CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_SAME(MouseManagerSetup, GeneralSettings*, GeneralSettings, generalSettings)
SETUP_GETTER_DEF_CTX_ONLY(MouseManagerSetup, InteractionStateHolder*, InteractionState, interactionState)

MouseManager::MouseManager(QObject *parent) : QObject(parent) {}

MouseManager::~MouseManager() {
  if (m_mouseHoldTimer) {
    m_mouseHoldTimer->stop();
  }
  if (m_clickHoldTimer) {
    m_clickHoldTimer->stop();
  }
}

void MouseManager::setupReferences(const MouseManagerSetup &setup) {
  m_scrollManager = setup.getScrollManager();
  m_selectionManager = setup.getSelectionManager();
  m_state = setup.getInteractionState();
  m_itemScrollArea = setup.getItemScrollArea();
  m_gridContainer = setup.getGridContainer();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
  m_generalSettings = setup.getGeneralSettings();
}

// --- Left Mouse Button Tracking ---

void MouseManager::setLeftMouseDown(bool down) {
  m_leftMouseDown = down;
}

// --- Wheel Scrolling State ---

void MouseManager::setWheelScrolling(bool scrolling) {
  m_wheelScrolling = scrolling;
}

// --- Wheel Computation ---

int MouseManager::computeWheelSteps(const QWheelEvent *wheelEvent) {
  if (!wheelEvent) {
    return 0;
  }
  const int wheelAngle = wheelEvent->angleDelta().y();
  if (wheelAngle != 0) {
    int steps = wheelAngle / UIConstants::Mouse::WHEEL_ANGLE_STEP;
    if (steps == 0) {
      steps = (wheelAngle > 0 ? 1 : -1);
    }
    return steps;
  }
  const QPoint pixelDelta = wheelEvent->pixelDelta();
  const int pixelDeltaY = pixelDelta.y();
  if (pixelDeltaY == 0) {
    return 0;
  }
  int steps = pixelDeltaY / UIConstants::Mouse::WHEEL_PIXEL_STEP;
  if (steps == 0) {
    steps = (pixelDeltaY > 0 ? 1 : -1);
  }
  return steps;
}

// --- Click Hold Timer ---

void MouseManager::startClickHoldTimer(const QPoint &clickPos,
                                       int selectedItemIndex, int gridWidth,
                                       int totalItems) {
  m_clickHoldPos = clickPos;
  m_clickHoldSelectedIndex = selectedItemIndex;
  m_clickHoldGridWidth = gridWidth;
  m_clickHoldTotalItems = totalItems;

  if (!m_clickHoldTimer) {
    m_clickHoldTimer = new QTimer(this);
    m_clickHoldTimer->setSingleShot(true);
    connect(m_clickHoldTimer, &QTimer::timeout, this,
            &MouseManager::onClickHoldTimerTimeout);
  }
  int delay = m_generalSettings ? m_generalSettings->clickHoldDelayMs : 500;
  m_clickHoldTimer->start(delay);
}

void MouseManager::stopClickHoldTimer() {
  if (m_clickHoldTimer && m_clickHoldTimer->isActive()) {
    m_clickHoldTimer->stop();
  }
}

bool MouseManager::isClickHoldTimerActive() const {
  return m_clickHoldTimer && m_clickHoldTimer->isActive();
}

void MouseManager::onClickHoldTimerTimeout() {
  if (m_leftMouseDown) {
    startMouseHoldScrolling(m_clickHoldPos, m_clickHoldSelectedIndex,
                            m_clickHoldGridWidth, m_clickHoldTotalItems);
  }
}

// --- Click Hold Horizontal Candidate ---

void MouseManager::updateClickHoldHorizontalCandidate(int previousSelection,
                                                      int targetSelection,
                                                      int gridWidth) {
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;
  m_mouseHoldHorizontalStartIndex = -1;

  if (previousSelection < 0 || targetSelection < 0 ||
      previousSelection == targetSelection) {
    return;
  }
  if (gridWidth <= 0) {
    return;
  }

  const int previousRow = previousSelection / gridWidth;
  const int currentRow = targetSelection / gridWidth;
  if (previousRow != currentRow) {
    return;
  }

  m_mouseHoldHorizontalDirection =
      (targetSelection > previousSelection) ? 1 : -1;
  m_mouseHoldHorizontalStartIndex = targetSelection;
  m_clickHoldHorizontalEligible = true;
}

void MouseManager::clearHorizontalCandidate() {
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;
  m_mouseHoldHorizontalStartIndex = -1;
}

// --- Hold Scrolling Control ---

void MouseManager::startMouseHoldScrolling(const QPoint &clickPos,
                                           int selectedItemIndex,
                                           int gridWidth, int totalItems) {
  Q_UNUSED(clickPos);

  if (!m_scrollManager ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }
  if (totalItems <= 0 || gridWidth <= 0 || selectedItemIndex < 0) {
    return;
  }

  // Cache values for step callback
  m_cachedGridWidth = gridWidth;
  m_cachedSelectedIndex = selectedItemIndex;

  if (!m_mouseHoldTimer) {
    m_mouseHoldTimer = new QTimer(this);
    connect(m_mouseHoldTimer, &QTimer::timeout, this,
            &MouseManager::onMouseHoldScrollStep);
  }

  // Try horizontal hold first
  if (tryStartHorizontalClickHold(totalItems, selectedItemIndex)) {
    return;
  }

  // Clear horizontal state
  m_mouseHoldHorizontal = false;
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;
  m_mouseHoldHorizontalStartIndex = -1;
  if (m_state) {
    m_state->scroll().horizHoldActive = false;
  }

  // Compute vertical direction
  int direction = computeVerticalDirection(selectedItemIndex, gridWidth);
  if (direction == 0) {
    return;
  }

  m_mouseHoldDirection = direction;
  m_mouseHoldScrolling = true;

  int interval = m_generalSettings ? m_generalSettings->clickHoldRepeatIntervalMs : 320;
  m_mouseHoldTimer->start(interval);

  // Signal that hold scrolling started
  if (m_state) {
    m_state->artwork().suppressArtwork = true;
    m_state->artwork().allowDuringSelection = true;
    m_state->scroll().clickScroll = true;
    m_state->scroll().clickHoldAdvancing = true;
  }
  emit requestOverlayVisibility(true);
  emit holdScrollingStarted(false);
}

bool MouseManager::tryStartHorizontalClickHold(int totalItems,
                                               int selectedItemIndex) {
  if (!m_clickHoldHorizontalEligible || m_mouseHoldHorizontalDirection == 0 ||
      !m_mouseHoldTimer) {
    return false;
  }
  if (!CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    m_clickHoldHorizontalEligible = false;
    return false;
  }
  if (selectedItemIndex < 0 || selectedItemIndex >= totalItems) {
    m_clickHoldHorizontalEligible = false;
    return false;
  }

  int startIndex = m_mouseHoldHorizontalStartIndex;
  if (startIndex < 0 || startIndex >= totalItems) {
    startIndex = selectedItemIndex;
  }

  // If we need to restore to the start index
  if (startIndex != selectedItemIndex && startIndex >= 0 &&
      startIndex < totalItems) {
    m_cachedSelectedIndex = startIndex;
    emit requestSelectionUpdate(startIndex);
  }
  m_mouseHoldHorizontalStartIndex = -1;

  m_mouseHoldHorizontal = true;
  m_mouseHoldScrolling = true;
  m_clickHoldHorizontalEligible = false;

  int interval = m_generalSettings ? m_generalSettings->clickHoldRepeatIntervalMs : 320;
  m_mouseHoldTimer->start(interval);

  // Signal properties
  if (m_state) {
    m_state->artwork().suppressArtwork = true;
    m_state->artwork().allowDuringSelection = true;
    m_state->scroll().horizHoldActive = true;
    m_state->scroll().clickScroll = true;
    m_state->scroll().clickHoldAdvancing = true;
  }
  emit requestOverlayVisibility(true);
  emit holdScrollingStarted(true);

  return true;
}

void MouseManager::stopMouseHoldScrolling() {
  if (m_mouseHoldTimer) {
    m_mouseHoldTimer->stop();
  }

  bool wasScrolling = m_mouseHoldScrolling;

  m_mouseHoldScrolling = false;
  m_mouseHoldDirection = 0;
  m_mouseHoldHorizontal = false;
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;
  m_mouseHoldHorizontalStartIndex = -1;

  // Signal property changes
  if (m_state) {
    m_state->scroll().clickScroll = false;
    m_state->scroll().clickHoldAdvancing = false;
    m_state->scroll().horizHoldActive = false;
  }
  emit requestOverlayVisibility(false);

  if (wasScrolling) {
    emit holdScrollingStopped();
  }
}

// --- Private ---

int MouseManager::computeVerticalDirection(int selectedItemIndex,
                                           int gridWidth) const {
  if (!m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return 0;
  }

  const CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vBar = m_itemScrollArea->verticalScrollBar();
  if (!vBar) {
    return 0;
  }

  int selectedRow = selectedItemIndex / gridWidth;
  int rowHeight = GridLayoutCalculator::getRowHeight(config);
  int selectedItemY = UIConstants::Grid::MARGINS + (selectedRow * rowHeight) +
                      (config.itemHeight / 2);

  int scrollTop = vBar->value();
  int viewportHeight = m_itemScrollArea->viewport()->height();
  int viewportTop = scrollTop;
  int viewportBottom = scrollTop + viewportHeight;
  int viewportCenterY = scrollTop + (viewportHeight / 2);

  if (selectedItemY < viewportTop + rowHeight) {
    return -1;
  } else if (selectedItemY > viewportBottom - rowHeight) {
    return 1;
  } else if (selectedItemY < viewportCenterY) {
    return -1;
  } else if (selectedItemY > viewportCenterY) {
    return 1;
  }
  return 0;
}

void MouseManager::onMouseHoldScrollStep() {
  if (!m_mouseHoldScrolling || !m_scrollManager) {
    stopMouseHoldScrolling();
    return;
  }

  int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0 || m_cachedGridWidth <= 0) {
    stopMouseHoldScrolling();
    return;
  }

  if (m_mouseHoldHorizontal) {
    emit scrollStepRequested(m_mouseHoldHorizontalDirection, true);
  } else {
    emit scrollStepRequested(m_mouseHoldDirection, false);
  }
}

// --- Widget Finding Utilities (static) ---

std::pair<ItemWidget *, int> MouseManager::findBestWidgetForClick(
    const QPoint &clickPos, ScrollManager *scrollManager,
    QWidget *gridContainer) {
  if (!scrollManager || !gridContainer) {
    return {nullptr, -1};
  }

  // Build candidate list preserving widget->index mapping
  QVector<std::pair<ItemWidget *, int>> candidates;
  const auto &active = scrollManager->getActiveWidgets();
  candidates.reserve(active.size());
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if (it.value() && it.value()->isVisible()) {
      candidates.append({it.value(), it.key()});
    }
  }
  if (candidates.isEmpty()) {
    return {nullptr, -1};
  }

  QPoint virtualContainerOffset(0, 0);
  QWidget *virtualContainer =
      candidates.first().first ? candidates.first().first->parentWidget()
                               : nullptr;
  if (virtualContainer &&
      virtualContainer->parentWidget() == gridContainer) {
    virtualContainerOffset = virtualContainer->pos();
  }
  QPoint posInVC = clickPos - virtualContainerOffset;

  // Find widgets directly under click position
  QVector<std::pair<ItemWidget *, int>> under;
  under.reserve(candidates.size());
  for (const auto &[widget, idx] : candidates) {
    if (!widget) {
      continue;
    }
    if (widget->geometry().contains(posInVC)) {
      under.append({widget, idx});
    }
  }

  // Find closest widget from either under-click or all candidates
  const auto &searchList = under.isEmpty() ? candidates : under;
  ItemWidget *best = nullptr;
  int bestIdx = -1;
  qint64 bestDist2 = -1;
  
  for (const auto &[widget, idx] : searchList) {
    if (!widget) {
      continue;
    }
    const QRect geometry = widget->geometry();
    const QPoint centerPoint = geometry.center();
    const qint64 deltaX =
        static_cast<qint64>(centerPoint.x()) - static_cast<qint64>(posInVC.x());
    const qint64 deltaY =
        static_cast<qint64>(centerPoint.y()) - static_cast<qint64>(posInVC.y());
    const qint64 dist2 = (deltaX * deltaX) + (deltaY * deltaY);
    if (bestDist2 < 0 || dist2 < bestDist2) {
      bestDist2 = dist2;
      best = widget;
      bestIdx = idx;
    }
  }
  
  return {best, bestIdx};
}

ItemWidget *MouseManager::findClosestWidget(
    const QVector<ItemWidget *> &candidates, const QPoint &clickPos) {
  ItemWidget *best = nullptr;
  qint64 bestDist2 = -1;
  for (ItemWidget *widget : candidates) {
    if (!widget) {
      continue;
    }
    const QRect geometry = widget->geometry();
    const QPoint centerPoint = geometry.center();
    const qint64 deltaX =
        static_cast<qint64>(centerPoint.x()) - static_cast<qint64>(clickPos.x());
    const qint64 deltaY =
        static_cast<qint64>(centerPoint.y()) - static_cast<qint64>(clickPos.y());
    const qint64 dist2 = (deltaX * deltaX) + (deltaY * deltaY);
    if (bestDist2 < 0 || dist2 < bestDist2) {
      bestDist2 = dist2;
      best = widget;
    }
  }
  return best;
}
