// Handles mouse input including click-hold scrolling, wheel events, and widget
// finding.
#include "mousemanager.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/validationhelpers.h"
#include "gridlayoutcalculator.h"
#include "igridlayoutscroll.h"
#include "ikeyboardmanager.h"
#include "interactionstateholder.h"
#include "iscrolldatasource.h"
#include "iselectionmanager.h"
#include "itemwidget.h"
#include "mousehelpers.h"
#include "uiconstants/grid.h"
#include "uiconstants/mouse.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidget>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcMouseManager, "kartend.mousemanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcMouseManager().isDebugEnabled()) {                                                       \
      qCDebug(lcMouseManager) << msg;                                                              \
    }                                                                                              \
  } while (0)

// MouseManagerSetup getter definitions (non-manager fields only — sibling
// managers + InteractionStateHolder are read directly from ctx at runtime).
SETUP_GETTER_DEF_UI_SAME(MouseManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_UI_SAME(MouseManagerSetup, QWidget *, GridContainer, gridContainer)
SETUP_GETTER_DEF_COL_SAME(MouseManagerSetup, const QVector<CollectionConfig> *, Collections,
                          collections)
SETUP_GETTER_DEF_COL_SAME(MouseManagerSetup, const int *, CurrentCollectionIndex,
                          currentCollectionIndex)
SETUP_GETTER_DEF_COL_SAME(MouseManagerSetup, GeneralSettings *, GeneralSettings, generalSettings)

MouseManager::MouseManager(QObject *parent) : QObject(parent) {}

MouseManager::~MouseManager() {
  m_mouseHoldTimer.stop();
  m_clickHoldTimer.stop();
}

void MouseManager::setupReferences(const MouseManagerSetup &setup) {
  m_ctx = setup.ctx;
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
    return MouseHelpers::wheelStepsFromAngle(wheelAngle, UIConstants::Mouse::WHEEL_ANGLE_STEP);
  }
  const QPoint pixelDelta = wheelEvent->pixelDelta();
  return MouseHelpers::wheelStepsFromPixel(pixelDelta.y(), UIConstants::Mouse::WHEEL_PIXEL_STEP);
}

// --- Click Hold Timer ---

void MouseManager::startClickHoldTimer(const QPoint &clickPos, int selectedItemIndex, int gridWidth,
                                       int totalItems) {
  m_clickHoldPos = clickPos;
  m_clickHoldSelectedIndex = selectedItemIndex;
  m_clickHoldGridWidth = gridWidth;
  m_clickHoldTotalItems = totalItems;

  if (!m_clickHoldTimerInited) {
    m_clickHoldTimer.setSingleShot(true);
    connect(&m_clickHoldTimer, &QTimer::timeout, this, &MouseManager::onClickHoldTimerTimeout);
    m_clickHoldTimerInited = true;
  }
  int delay = m_generalSettings ? m_generalSettings->input.clickHoldDelayMs : 500;
  m_clickHoldTimer.start(delay);
}

void MouseManager::stopClickHoldTimer() {
  if (m_clickHoldTimer.isActive()) {
    m_clickHoldTimer.stop();
  }
}

bool MouseManager::isClickHoldTimerActive() const {
  return m_clickHoldTimer.isActive();
}

void MouseManager::onClickHoldTimerTimeout() {
  if (m_leftMouseDown) {
    startMouseHoldScrolling(m_clickHoldPos, m_clickHoldSelectedIndex, m_clickHoldGridWidth,
                            m_clickHoldTotalItems);
  }
}

// --- Click Hold Horizontal Candidate ---

void MouseManager::updateClickHoldHorizontalCandidate(int previousSelection, int targetSelection,
                                                      int gridWidth) {
  const auto cand =
      MouseHelpers::computeHorizontalCandidate(previousSelection, targetSelection, gridWidth);
  m_clickHoldHorizontalEligible = cand.eligible;
  m_mouseHoldHorizontalDirection = cand.direction;
  m_mouseHoldHorizontalStartIndex = cand.startIndex;
}

void MouseManager::clearHorizontalCandidate() {
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;
  m_mouseHoldHorizontalStartIndex = -1;
}

// --- Hold Scrolling Control ---

void MouseManager::startMouseHoldScrolling(const QPoint &clickPos, int selectedItemIndex,
                                           int gridWidth, int totalItems) {
  Q_UNUSED(clickPos);

  IScrollDataSource *scroll = m_ctx ? m_ctx->scrollData() : nullptr;
  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;
  if (!scroll || !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }
  if (totalItems <= 0 || gridWidth <= 0 || selectedItemIndex < 0) {
    return;
  }

  // Cache values for step callback
  m_cachedGridWidth = gridWidth;
  m_cachedSelectedIndex = selectedItemIndex;

  if (!m_mouseHoldTimerInited) {
    connect(&m_mouseHoldTimer, &QTimer::timeout, this, &MouseManager::onMouseHoldScrollStep);
    m_mouseHoldTimerInited = true;
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
  if (state) {
    state->scroll().horizHoldActive = false;
  }

  // Compute vertical direction
  int direction = computeVerticalDirection(selectedItemIndex, gridWidth);
  if (direction == 0) {
    return;
  }

  m_mouseHoldDirection = direction;
  m_mouseHoldScrolling = true;

  // Check if we're in list mode for faster repeat intervals
  bool isListMode = false;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    isListMode = (*m_collections)[*m_currentCollectionIndex].viewType == ViewType::List;
  }

  int interval;
  if (isListMode) {
    interval = m_generalSettings ? m_generalSettings->input.listClickHoldRepeatIntervalMs : 80;
  } else {
    interval = m_generalSettings ? m_generalSettings->input.clickHoldRepeatIntervalMs : 320;
  }
  m_mouseHoldTimer.start(interval);

  // Signal that hold scrolling started
  if (state) {
    state->artwork().suppressArtwork = true;
    state->artwork().allowDuringSelection = true;
    state->scroll().clickScroll = true;
    state->scroll().clickHoldAdvancing = true;
  }
  emit requestOverlayVisibility(true);
  emit holdScrollingStarted(false);
}

bool MouseManager::tryStartHorizontalClickHold(int totalItems, int selectedItemIndex) {
  if (!m_clickHoldHorizontalEligible || m_mouseHoldHorizontalDirection == 0 ||
      !m_mouseHoldTimerInited) {
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
  if (startIndex != selectedItemIndex && startIndex >= 0 && startIndex < totalItems) {
    m_cachedSelectedIndex = startIndex;
    emit requestSelectionUpdate(startIndex);
  }
  m_mouseHoldHorizontalStartIndex = -1;

  m_mouseHoldHorizontal = true;
  m_mouseHoldScrolling = true;
  m_clickHoldHorizontalEligible = false;

  // Check if we're in list mode for faster repeat intervals
  bool isListMode = false;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    isListMode = (*m_collections)[*m_currentCollectionIndex].viewType == ViewType::List;
  }

  int interval;
  if (isListMode) {
    interval = m_generalSettings ? m_generalSettings->input.listClickHoldRepeatIntervalMs : 80;
  } else {
    interval = m_generalSettings ? m_generalSettings->input.clickHoldRepeatIntervalMs : 320;
  }
  m_mouseHoldTimer.start(interval);

  // Signal properties
  if (auto *state = m_ctx ? m_ctx->interactionState() : nullptr) {
    state->artwork().suppressArtwork = true;
    state->artwork().allowDuringSelection = true;
    state->scroll().horizHoldActive = true;
    state->scroll().clickScroll = true;
    state->scroll().clickHoldAdvancing = true;
  }
  emit requestOverlayVisibility(true);
  emit holdScrollingStarted(true);

  return true;
}

void MouseManager::stopMouseHoldScrolling() {
  m_mouseHoldTimer.stop();

  bool wasScrolling = m_mouseHoldScrolling;

  m_mouseHoldScrolling = false;
  m_mouseHoldDirection = 0;
  m_mouseHoldHorizontal = false;
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;
  m_mouseHoldHorizontalStartIndex = -1;

  // Signal property changes
  if (auto *state = m_ctx ? m_ctx->interactionState() : nullptr) {
    state->scroll().clickScroll = false;
    state->scroll().clickHoldAdvancing = false;
    state->scroll().horizHoldActive = false;
  }
  emit requestOverlayVisibility(false);

  if (wasScrolling) {
    emit holdScrollingStopped();
  }
}

// --- Private ---

int MouseManager::computeVerticalDirection(int selectedItemIndex, int gridWidth) const {
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
  int selectedItemY =
      UIConstants::Grid::MARGINS + (selectedRow * rowHeight) + (config.gridLayout.itemHeight / 2);

  int scrollTop = vBar->value();
  int viewportHeight = m_itemScrollArea->viewport()->height();
  return MouseHelpers::verticalScrollDirection(selectedItemY, /*viewportTopY=*/scrollTop,
                                               /*viewportBottomY=*/scrollTop + viewportHeight,
                                               /*viewportCenterY=*/scrollTop + (viewportHeight / 2),
                                               rowHeight);
}

void MouseManager::onMouseHoldScrollStep() {
  IScrollDataSource *scroll = m_ctx ? m_ctx->scrollData() : nullptr;
  if (!m_mouseHoldScrolling || !scroll) {
    stopMouseHoldScrolling();
    return;
  }

  int totalItems = scroll->getTotalItems();
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

std::pair<ItemWidget *, int> MouseManager::findBestWidgetForClick(const QPoint &clickPos,
                                                                  IScrollDataSource *scrollData,
                                                                  IGridLayoutScroll *scrollGrid,
                                                                  QWidget *gridContainer) {
  if (!scrollData || !scrollGrid || !gridContainer) {
    return {nullptr, -1};
  }

  const auto &active = scrollData->getActiveWidgets();
  if (active.isEmpty()) {
    return {nullptr, -1};
  }

  // Widgets live inside a virtual container offset within gridContainer; shift
  // the click into that space (the coordinate system the grid layout and
  // indexAtPosition both use). Any active widget's parent gives the offset.
  ItemWidget *anyWidget = nullptr;
  for (auto it = active.constBegin(); it != active.constEnd() && !anyWidget; ++it) {
    anyWidget = it.value();
  }
  QPoint virtualContainerOffset(0, 0);
  QWidget *virtualContainer = anyWidget ? anyWidget->parentWidget() : nullptr;
  if (virtualContainer && virtualContainer->parentWidget() == gridContainer) {
    virtualContainerOffset = virtualContainer->pos();
  }
  const QPoint posInVC = clickPos - virtualContainerOffset;

  // Fast path (Kartend-th8z): map the cursor straight to a grid index instead of
  // scanning every active widget. Trust it only when the resolved widget really
  // contains the point — that defers inter-tile gaps, out-of-grid clicks, and
  // any index-space skew (filtering / subcollection rows) to the snap-to-nearest
  // fallback below, so the observable result is unchanged.
  const int fastIdx = GridLayoutCalculator::indexAtPosition(posInVC, scrollGrid->getMetrics(),
                                                            scrollData->getTotalItems());
  if (fastIdx >= 0) {
    ItemWidget *hit = active.value(fastIdx);
    if (hit && hit->isVisible() && hit->geometry().contains(posInVC)) {
      return {hit, fastIdx};
    }
  }

  // Fallback (original behavior): build the candidate list, then snap to the
  // nearest visible widget by center distance, preferring any that contain the
  // point.
  QVector<std::pair<ItemWidget *, int>> candidates;
  candidates.reserve(active.size());
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if (it.value() && it.value()->isVisible()) {
      candidates.append({it.value(), it.key()});
    }
  }
  if (candidates.isEmpty()) {
    return {nullptr, -1};
  }

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
    const qint64 deltaX = static_cast<qint64>(centerPoint.x()) - static_cast<qint64>(posInVC.x());
    const qint64 deltaY = static_cast<qint64>(centerPoint.y()) - static_cast<qint64>(posInVC.y());
    const qint64 dist2 = (deltaX * deltaX) + (deltaY * deltaY);
    if (bestDist2 < 0 || dist2 < bestDist2) {
      bestDist2 = dist2;
      best = widget;
      bestIdx = idx;
    }
  }

  return {best, bestIdx};
}

ItemWidget *MouseManager::findClosestWidget(const QVector<ItemWidget *> &candidates,
                                            const QPoint &clickPos) {
  ItemWidget *best = nullptr;
  qint64 bestDist2 = -1;
  for (ItemWidget *widget : candidates) {
    if (!widget) {
      continue;
    }
    const QRect geometry = widget->geometry();
    const QPoint centerPoint = geometry.center();
    const qint64 deltaX = static_cast<qint64>(centerPoint.x()) - static_cast<qint64>(clickPos.x());
    const qint64 deltaY = static_cast<qint64>(centerPoint.y()) - static_cast<qint64>(clickPos.y());
    const qint64 dist2 = (deltaX * deltaX) + (deltaY * deltaY);
    if (bestDist2 < 0 || dist2 < bestDist2) {
      bestDist2 = dist2;
      best = widget;
    }
  }
  return best;
}
