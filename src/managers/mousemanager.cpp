#include "mousemanager.h"
#include "collectionutils.h"
#include "keyboardmanager.h"
#include "mainwindow.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"

#include <QScrollArea>
#include <QScrollBar>

MouseManager::MouseManager(QObject *parent) : QObject(parent) {}

MouseManager::~MouseManager() {
  if (m_mouseHoldTimer != nullptr) {
    m_mouseHoldTimer->stop();
  }
}

void MouseManager::setupReferences(const MouseManagerSetup &setup) {
  m_scrollManager = setup.scrollManager;
  m_selectionManager = setup.selectionManager;
  m_mainWindow = setup.mainWindow;
  m_itemScrollArea = setup.itemScrollArea;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
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
