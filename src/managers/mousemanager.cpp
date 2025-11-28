// Handles mouse input including click-hold scrolling, wheel events, and widget finding.
#include "mousemanager.h"
#include "collectionutils.h"
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidget>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcMouseManager, "kartend.mousemanager")
#define debugLog(msg) qCDebug(lcMouseManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

MouseManager::MouseManager(QObject *parent) : QObject(parent) {}

MouseManager::~MouseManager() {
  if (m_mouseHoldTimer != nullptr) {
    m_mouseHoldTimer->stop();
  }
  if (m_clickHoldTimer != nullptr) {
    m_clickHoldTimer->stop();
  }
}

void MouseManager::setupReferences(const MouseManagerSetup &setup) {
  m_scrollManager = setup.scrollManager;
  m_selectionManager = setup.selectionManager;
  m_itemScrollArea = setup.itemScrollArea;
  m_gridContainer = setup.gridContainer;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
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
  if (wheelEvent == nullptr) {
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

  if (m_clickHoldTimer == nullptr) {
    m_clickHoldTimer = new QTimer(this);
    m_clickHoldTimer->setSingleShot(true);
    connect(m_clickHoldTimer, &QTimer::timeout, this,
            &MouseManager::onClickHoldTimerTimeout);
  }
  m_clickHoldTimer->start(UIConstants::CLICK_HOLD_START_MS);
}

void MouseManager::stopClickHoldTimer() {
  if (m_clickHoldTimer != nullptr && m_clickHoldTimer->isActive()) {
    m_clickHoldTimer->stop();
  }
}

bool MouseManager::isClickHoldTimerActive() const {
  return m_clickHoldTimer != nullptr && m_clickHoldTimer->isActive();
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

  if (m_scrollManager == nullptr || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return;
  }
  if (totalItems <= 0 || gridWidth <= 0 || selectedItemIndex < 0) {
    return;
  }

  // Cache values for step callback
  m_cachedGridWidth = gridWidth;
  m_cachedSelectedIndex = selectedItemIndex;

  if (m_mouseHoldTimer == nullptr) {
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
  emit requestSetProperty(PropertyKeys::HorizHoldActive, false);

  // Compute vertical direction
  int direction = computeVerticalDirection(selectedItemIndex, gridWidth);
  if (direction == 0) {
    return;
  }

  m_mouseHoldDirection = direction;
  m_mouseHoldScrolling = true;

  m_mouseHoldTimer->start(UIConstants::ARROW_KEY_BASE_INTERVAL_MS);

  // Signal that hold scrolling started
  emit requestScrollAreaProperty(PropertyKeys::SuppressArtwork, true);
  emit requestScrollAreaProperty(PropertyKeys::AllowArtworkDuringSelection, true);
  emit requestSetProperty(PropertyKeys::ClickScroll, true);
  emit requestSetProperty(PropertyKeys::ClickHoldAdvancing, true);
  emit requestOverlayVisibility(true);
  emit holdScrollingStarted(false);
}

bool MouseManager::tryStartHorizontalClickHold(int totalItems,
                                               int selectedItemIndex) {
  if (!m_clickHoldHorizontalEligible || m_mouseHoldHorizontalDirection == 0 ||
      m_mouseHoldTimer == nullptr) {
    return false;
  }
  if (m_collections == nullptr || m_currentCollectionIndex == nullptr ||
      *m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
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

  m_mouseHoldTimer->start(UIConstants::CLICK_HOLD_HORIZONTAL_INTERVAL_MS);

  // Signal properties
  emit requestScrollAreaProperty(PropertyKeys::SuppressArtwork, true);
  emit requestScrollAreaProperty(PropertyKeys::AllowArtworkDuringSelection, true);
  emit requestSetProperty(PropertyKeys::HorizHoldActive, true);
  emit requestSetProperty(PropertyKeys::ClickScroll, true);
  emit requestSetProperty(PropertyKeys::ClickHoldAdvancing, true);
  emit requestOverlayVisibility(true);
  emit holdScrollingStarted(true);

  return true;
}

void MouseManager::stopMouseHoldScrolling() {
  if (m_mouseHoldTimer != nullptr) {
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
  emit requestSetProperty(PropertyKeys::ClickScroll, false);
  emit requestSetProperty(PropertyKeys::ClickHoldAdvancing, false);
  emit requestSetProperty(PropertyKeys::HorizHoldActive, false);
  emit requestOverlayVisibility(false);

  if (wasScrolling) {
    emit holdScrollingStopped();
  }
}

// --- Private ---

int MouseManager::computeVerticalDirection(int selectedItemIndex,
                                           int gridWidth) const {
  if (!m_itemScrollArea || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr) {
    return 0;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return 0;
  }

  const CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vBar = m_itemScrollArea->verticalScrollBar();
  if (vBar == nullptr) {
    return 0;
  }

  int selectedRow = selectedItemIndex / gridWidth;
  int rowHeight = config.itemHeight + config.verticalSpacing;
  int selectedItemY = UIConstants::GRID_MARGINS + (selectedRow * rowHeight) +
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
  if (!m_mouseHoldScrolling || m_scrollManager == nullptr) {
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

MediaItemWidget *MouseManager::findBestWidgetForClick(
    const QPoint &clickPos, ScrollManager *scrollManager,
    QWidget *gridContainer) {
  if (scrollManager == nullptr || gridContainer == nullptr) {
    return nullptr;
  }

  QVector<MediaItemWidget *> candidates;
  const auto &active = scrollManager->getActiveWidgets();
  candidates.reserve(active.size());
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if (it.value() != nullptr && it.value()->isVisible()) {
      candidates.append(it.value());
    }
  }
  if (candidates.isEmpty()) {
    return nullptr;
  }

  QPoint virtualContainerOffset(0, 0);
  QWidget *virtualContainer =
      candidates.first() != nullptr ? candidates.first()->parentWidget()
                                    : nullptr;
  if (virtualContainer != nullptr &&
      virtualContainer->parentWidget() == gridContainer) {
    virtualContainerOffset = virtualContainer->pos();
  }
  QPoint posInVC = clickPos - virtualContainerOffset;

  QVector<MediaItemWidget *> under;
  under.reserve(candidates.size());
  for (MediaItemWidget *widget : candidates) {
    if (widget == nullptr) {
      continue;
    }
    if (widget->geometry().contains(posInVC)) {
      under.append(widget);
    }
  }

  if (!under.isEmpty()) {
    return findClosestWidget(under, posInVC);
  }
  return findClosestWidget(candidates, posInVC);
}

MediaItemWidget *MouseManager::findClosestWidget(
    const QVector<MediaItemWidget *> &candidates, const QPoint &clickPos) {
  MediaItemWidget *best = nullptr;
  qint64 bestDist2 = -1;
  for (MediaItemWidget *widget : candidates) {
    if (widget == nullptr) {
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
