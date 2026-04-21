// Selection update + overlay coordination methods extracted from
// scrollmanager.cpp (Kartend-mhf). All remain ScrollManager members and
// access existing class state (m_overlayManager, m_selectionState,
// m_arrowKeyScrollHelper, m_selectionCoordinator, m_activeWidgets, m_metrics,
// etc.) via the same raw-alias / unique_ptr members defined in scrollmanager.h.
#include "arrowkeyscrollhelper.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "scrollmanager.h"
#include "selectioncoordinator.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "uiconstants.h"

#include <QLoggingCategory>
#include <QPoint>
#include <QRect>
#include <QScrollArea>
#include <QString>
#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcScrollManager().isDebugEnabled()) {                                  \
      qCDebug(lcScrollManager) << msg;                                         \
    }                                                                          \
  } while (0)

// Performs arrow key recentering unless suppressed by user scroll or timing
// properties
// Handles arrow key scroll animation to center selected item
void ScrollManager::onArrowKeyViewUpdate() {
  if (!m_arrowKeyScrollHelper || !m_virtualContainer ||
      !m_selectionState->hasSelection() ||
      m_selectionState->lastSelectedIndex() >= m_totalItems) {
    return;
  }

  // Update metrics for the helper
  m_arrowKeyScrollHelper->setItemMetrics(
      m_metrics.itemHeight, m_metrics.verticalSpacing, m_metrics.margins);

  // Delegate to helper with position callback
  m_arrowKeyScrollHelper->performUpdate(
      m_selectionState->lastSelectedIndex(), m_totalItems,
      m_metrics.itemsPerRow,
      [this](int idx) { return getItemPosition(idx).y(); });
}

auto ScrollManager::selectionOverlayRectForIndex(int visualIndex) const
    -> QRect {
  // Delegate to coordinator if available (shares the same calculation)
  if (m_selectionCoordinator) {
    return m_selectionCoordinator->rectForIndex(visualIndex, m_totalItems);
  }

  // Fallback for when coordinator is not configured
  if (visualIndex < 0 || visualIndex >= m_totalItems) {
    return {};
  }
  QPoint pos = getItemPosition(visualIndex);
  return SelectionOverlayManager::overlayRectForPosition(
      pos, m_metrics.itemWidth, m_metrics.itemHeight);
}

void ScrollManager::refreshSelectionOverlayState() {
  if (!m_overlayManager) {
    return;
  }

  debugLog(
      QString(
          "refreshSelectionOverlayState: lastSelectedIndex=%1 forceVisible=%2")
          .arg(m_selectionState->lastSelectedIndex())
          .arg(m_overlayManager->isForceVisible()));

  if (!m_overlayManager->shouldKeepVisible()) {
    debugLog("  HIDING overlay (not force visible)");
    m_overlayManager->hide();

    bool glideWasActive = m_state && m_state->glideAnimating();
    if (m_state) {
      m_state->setGlideAnimating(false);
    }

    if (glideWasActive && m_selectionState->hasSelection()) {
      ensureWidgetForIndex(m_selectionState->lastSelectedIndex());
      if (auto *selectedWidget = m_activeWidgets.value(
              m_selectionState->lastSelectedIndex(), nullptr)) {
        debugLog("  requesting widget repaint for selection border");
        selectedWidget->update();
      }
    }
    return;
  }

  // Don't interfere with an ongoing selection overlay animation
  if (m_overlayManager->isAnimating()) {
    debugLog("  animation running, just show/raise");
    m_overlayManager->raise();
    return;
  }

  // During click-hold, we respect the current visibility state (managed by move
  // handlers) If visible, keep it on top. If hidden (e.g. wrapped row), keep it
  // hidden.
  if (m_overlayManager->isForceVisible()) {
    if (m_overlayManager->isVisible()) {
      debugLog("  click-hold mode, raising visible overlay");
      m_overlayManager->raise();
    } else {
      debugLog("  click-hold mode, overlay hidden (respecting state)");
    }
    return;
  }

  if (!m_selectionState->hasSelection()) {
    return;
  }

  // Position overlay directly (non-click-hold case or initial positioning)
  QRect rect =
      selectionOverlayRectForIndex(m_selectionState->lastSelectedIndex());
  debugLog(QString("  setting geometry to rect: x=%1 y=%2 w=%3 h=%4")
               .arg(rect.x())
               .arg(rect.y())
               .arg(rect.width())
               .arg(rect.height()));
  if (rect.isValid()) {
    m_overlayManager->showAtRect(rect);
  }
}

void ScrollManager::setForceSelectionOverlayVisible(bool force) {
  debugLog(QString("setForceSelectionOverlayVisible: %1").arg(force));
  if (!m_overlayManager) {
    return;
  }
  if (m_overlayManager->isForceVisible() == force) {
    return;
  }
  m_overlayManager->setForceVisible(force);
  refreshSelectionOverlayState();
}

void ScrollManager::handleHorizontalMoveAnimation(int selectedIndex,
                                                  int prevIndex) {
  if (!m_overlayManager) {
    return;
  }

  debugLog(
      QString("handleHorizontalMoveAnimation: prev=%1 sel=%2 forceVisible=%3")
          .arg(prevIndex)
          .arg(selectedIndex)
          .arg(m_overlayManager->isForceVisible()));

  // Calculate target rect for the new selection
  QRect targetRect = selectionOverlayRectForIndex(selectedIndex);
  if (!targetRect.isValid()) {
    ensureWidgetForIndex(selectedIndex);
    if (auto *w = m_activeWidgets.value(selectedIndex, nullptr)) {
      targetRect = SelectionOverlayManager::overlayRectForWidget(w);
    }
  }
  if (!targetRect.isValid()) {
    debugLog("  targetRect invalid, returning");
    return;
  }

  debugLog(QString("  targetRect: x=%1 y=%2")
               .arg(targetRect.x())
               .arg(targetRect.y()));

  // Get start rect from previous selection if overlay not visible
  QRect startRect;
  if (!m_overlayManager->isVisible()) {
    startRect = selectionOverlayRectForIndex(prevIndex);
    if (!startRect.isValid()) {
      ensureWidgetForIndex(prevIndex);
      if (auto *w = m_activeWidgets.value(prevIndex, nullptr)) {
        startRect = SelectionOverlayManager::overlayRectForWidget(w);
      }
    }
  }

  // Update widget selection states
  int committedIdx = m_selectionState->committedSelectedIndex();
  bool needsUpdate = m_selectionState->needsCommitUpdate(selectedIndex);
  if (needsUpdate) {
    if (auto *prevWidget = m_activeWidgets.value(committedIdx, nullptr)) {
      prevWidget->setSelected(false);
    }
  }
  ensureWidgetForIndex(selectedIndex);
  if (auto *currWidget = m_activeWidgets.value(selectedIndex, nullptr)) {
    currWidget->setSelected(true);
  } else {
  }
  m_selectionState->commitSelection(selectedIndex);

  // Animate to target
  m_overlayManager->animateTo(targetRect, startRect);

  debugLog(QString("  animation started to (%1,%2)")
               .arg(targetRect.x())
               .arg(targetRect.y()));
}

void ScrollManager::handleDirectSelectionUpdate(int selectedIndex) {
  bool keepOverlay = m_overlayManager && m_overlayManager->shouldKeepVisible();

  if (m_overlayManager && m_overlayManager->isVisible()) {
    m_overlayManager->stopAnimation();
    if (!keepOverlay) {
      m_overlayManager->hide();
      if (m_state) {
        m_state->setGlideAnimating(false);
      }
    }
  }

  if (m_selectionState->needsCommitUpdate(selectedIndex)) {
    if (auto *prevSel = m_activeWidgets.value(
            m_selectionState->committedSelectedIndex(), nullptr)) {
      prevSel->setSelected(false);
    }
  }
  ensureWidgetForIndex(selectedIndex);
  auto *currSel = m_activeWidgets.value(selectedIndex, nullptr);
  if (currSel) {
    currSel->setSelected(true);
    // Force update to ensure border is drawn if we are in widget-border mode
    currSel->update();
  }

  if (keepOverlay) {
    // For direct updates (non-gliding) during click-hold (e.g. row wrap),
    // use widget border instead of overlay to avoid visual glitches.
    // This ensures we always have a visible selection indicator.
    if (m_state) {
      m_state->setGlideAnimating(false);
    }
    if (m_overlayManager) {
      m_overlayManager->hide();
      debugLog(
          "  handleDirectSelectionUpdate: hiding overlay, using widget border");
    }
  }
  m_selectionState->commitSelection(selectedIndex);
}

void ScrollManager::prewarmSurroundingWidgets(int selectedIndex) {
  const int itemsPerRow =
      (m_metrics.itemsPerRow > 0 ? m_metrics.itemsPerRow : 1);
  const int prewarmRows = UIConstants::Grid::BUFFER_ROWS;
  const int halfWindow = prewarmRows * itemsPerRow;
  int start = std::max(0, selectedIndex - halfWindow);
  int end = std::min(m_totalItems - 1, selectedIndex + halfWindow);
  for (int visualIndex = start; visualIndex <= end; ++visualIndex) {
    ensureWidgetForIndex(visualIndex);
  }
}

void ScrollManager::scheduleArrowKeyUpdate(int selectedIndex) {
  if (!m_arrowKeyScrollHelper || !m_arrowKeyViewUpdateTimer) {
    return;
  }

  // Check if update should be skipped due to suppression
  if (m_arrowKeyScrollHelper->shouldSkipUpdate()) {
    return;
  }

  bool extendedHold = m_state && m_state->arrow().arrowKeyScrolling;
  static constexpr int ARROW_KEY_UPDATE_DELAY_EXTENDED_MS = 16;
  static constexpr int ARROW_KEY_UPDATE_DELAY_NORMAL_MS = 8;
  const int delayMs = extendedHold ? ARROW_KEY_UPDATE_DELAY_EXTENDED_MS
                                   : ARROW_KEY_UPDATE_DELAY_NORMAL_MS;
  m_arrowKeyViewUpdateTimer->start(delayMs);

  Q_UNUSED(selectedIndex)
}

// Updates selection visuals and manages prewarming, overlay animation, and
// arrow-centering properties
void ScrollManager::updateSelectionForIndex(int selectedIndex) {
  if (lcScrollManager().isDebugEnabled()) {
    bool forceVisible = m_overlayManager && m_overlayManager->isForceVisible();
    debugLog(
        QString("updateSelectionForIndex: sel=%1 lastSel=%2 forceVisible=%3")
            .arg(selectedIndex)
            .arg(m_selectionState->lastSelectedIndex())
            .arg(forceVisible));
  }

  if (m_destroying || (!m_mediaScrollArea) || selectedIndex < 0 ||
      selectedIndex >= m_totalItems) {
    debugLog("  early return due to invalid state");
    return;
  }

  int prevIndex = m_selectionState->lastSelectedIndex();
  const bool sameSelection = (prevIndex == selectedIndex);

  if (!sameSelection) {
    updateSelectionDirection(selectedIndex, prevIndex);
  }

  prewarmSurroundingWidgets(selectedIndex);

  // Ensure widget exists and get reference
  ensureWidgetForIndex(selectedIndex);
  ItemWidget *currentWidget = m_activeWidgets.value(selectedIndex, nullptr);

  const bool keepOverlay =
      m_overlayManager && m_overlayManager->shouldKeepVisible();

  if (!currentWidget) {
    handleMissingWidgetSelection(selectedIndex, keepOverlay);
    return;
  }

  if (sameSelection) {
    handleSameSelectionUpdate(selectedIndex, currentWidget, keepOverlay);
    return;
  }

  handleNewSelectionUpdate(selectedIndex, prevIndex, currentWidget);
  scheduleArrowKeyUpdate(selectedIndex);
}

void ScrollManager::updateSelectionDirection(int selectedIndex, int prevIndex) {
  m_selectionState->updateForNewSelection(selectedIndex, prevIndex,
                                          m_metrics.itemsPerRow);
  debugLog(QString("  updated lastSelectedIndex to %1").arg(selectedIndex));
}

void ScrollManager::handleMissingWidgetSelection(int selectedIndex,
                                                 bool keepOverlay) {
  debugLog(QString("  currentWidget is null for index %1").arg(selectedIndex));
  // Widget not available, but during click-hold we still update overlay
  // position
  if (keepOverlay && m_overlayManager) {
    QRect rect = selectionOverlayRectForIndex(selectedIndex);
    if (rect.isValid()) {
      m_overlayManager->showAtRect(rect);
      debugLog(QString("  positioned overlay directly at (%1,%2)")
                   .arg(rect.x())
                   .arg(rect.y()));
    }
  }
}

void ScrollManager::handleSameSelectionUpdate(int selectedIndex,
                                              ItemWidget *currentWidget,
                                              bool keepOverlay) {
  debugLog("  same selection, returning");

  // If an animation is running, we MUST let it finish to achieve the "glide"
  // effect. Interrupting it here would cause the overlay to disappear and the
  // widget border to snap back immediately, defeating the purpose of the
  // animation.
  if (m_overlayManager && m_overlayManager->isAnimating()) {
    debugLog("  animation running during same-selection update - letting it "
             "continue");
    if (!m_overlayManager->isVisible()) {
      m_overlayManager->raise();
    }
    return;
  }

  if (keepOverlay && m_overlayManager) {
    m_overlayManager->showAtWidget(currentWidget);
    if (!m_overlayManager->isVisible()) {
      // Fallback to index-based positioning if widget-based failed
      QRect rect = selectionOverlayRectForIndex(selectedIndex);
      if (rect.isValid()) {
        m_overlayManager->showAtRect(rect);
      }
    }
  } else if (m_overlayManager && m_overlayManager->isVisible()) {
    m_overlayManager->hide();
  }

  if (m_selectionState->needsCommitUpdate(selectedIndex)) {
    if (auto *prevSel = m_activeWidgets.value(
            m_selectionState->committedSelectedIndex(), nullptr)) {
      prevSel->setSelected(false);
    }
  }
  currentWidget->setSelected(true);
  m_selectionState->commitSelection(selectedIndex);

  if (m_state && !keepOverlay) {
    m_state->setGlideAnimating(false);
  }
  scheduleArrowKeyUpdate(selectedIndex);
}

void ScrollManager::handleNewSelectionUpdate(int selectedIndex, int prevIndex,
                                             ItemWidget *currentWidget) {
  // Use coordinator for movement analysis
  bool isHorizontalMove = false;
  if (m_selectionCoordinator) {
    auto movement = m_selectionCoordinator->analyzeMovement(
        selectedIndex, prevIndex, m_metrics.itemsPerRow);
    isHorizontalMove = movement.isHorizontal;
  } else {
    calculateMovementDirection(selectedIndex, prevIndex, m_metrics.itemsPerRow,
                               isHorizontalMove);
  }

  debugLog(QString("  isHorizontalMove=%1").arg(isHorizontalMove));

  if (isHorizontalMove) {
    handleHorizontalMoveAnimation(selectedIndex, prevIndex);
  } else {
    handleDirectSelectionUpdate(selectedIndex);
  }

  Q_UNUSED(currentWidget)
}

void ScrollManager::calculateMovementDirection(int selectedIndex, int prevIndex,
                                               int itemsPerRow,
                                               bool &isHorizontalMove) {
  if (prevIndex < 0) {
    isHorizontalMove = false;
    return;
  }

  // Allow jumps > 1 if on the same row (for rapid click-hold advancing)
  int prevRow = GridUtils::computeItemRow(prevIndex, itemsPerRow);
  int currRow = GridUtils::computeItemRow(selectedIndex, itemsPerRow);

  if (prevRow == currRow) {
    isHorizontalMove = true;
    return;
  }

  int diff = std::abs(selectedIndex - prevIndex);
  if (diff != 1) {
    isHorizontalMove = false;
    return;
  }

  if (itemsPerRow <= 1) {
    isHorizontalMove = false;
    return;
  }

  int prevCol = prevIndex % itemsPerRow;
  if (prevCol < 0) {
    prevCol += itemsPerRow;
  }
  int currCol = selectedIndex % itemsPerRow;
  if (currCol < 0) {
    currCol += itemsPerRow;
  }

  bool wrappedForward = (prevCol == itemsPerRow - 1) && (currCol == 0);
  bool wrappedBackward = (prevCol == 0) && (currCol == itemsPerRow - 1);
  isHorizontalMove = wrappedForward || wrappedBackward;
}
