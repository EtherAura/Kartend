// Scroll event handling and double-click slots extracted from
// scrollmanager.cpp. All remain ScrollManager members and access existing
// class state.
#include "applicationcontext.h"
#include "arrowkeyscrollhelper.h"
#include "iartworkmanager.h"
#include "interactionstateholder.h"
#include "itemwidgetfactory.h"
#include "scrolldatamanager.h"
#include "scrolleventhandler.h"
#include "scrollhelpers.h"
#include "scrollmanager.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "timerutils.h"
#include "uiconstants/database.h"
#include "uiconstants/keyboard.h"
#include "uiconstants/mouse.h"

#include <QLoggingCategory>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcScrollManager().isDebugEnabled()) {                                                      \
      qCDebug(lcScrollManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

// Handles scroll changes throttling artwork updates and arrow-centering
// suppression
void ScrollManager::onScrollChanged() {
  if (m_destroying) {
    return;
  }

  // Prevent reentrant scroll handling which can occur when scroll animations
  // trigger valueChanged signals during processing
  if (m_processingScrollChange) {
    return;
  }
  m_processingScrollChange = true;

  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;
  if (state && state->scroll().programmaticScroll) {
    handleProgrammaticScroll();
    m_processingScrollChange = false;
    return;
  }

  handleUserScroll();
  setupScrollSuppression();
  finalizeScrollChanges();
  m_processingScrollChange = false;
}

void ScrollManager::handleProgrammaticScroll() {
  notifyUserActivity();
  if (!m_scrollTimer.isActive()) {
    m_scrollTimer.start();
  }
}

void ScrollManager::handleUserScroll() {
  if (m_scrollEventHandler) {
    m_scrollEventHandler->setUserScrollActive(true);
  }
  if (m_userScrollIdleTimer) {
    m_userScrollIdleTimer->trigger();
  }

  // Schedule pool prewarm for when scroll activity settles
  if (m_prewarmIdleTimer) {
    m_prewarmIdleTimer->trigger();
  }

  if (auto *state = m_ctx ? m_ctx->interactionState() : nullptr) {
    state->scroll().userScrollActive = true;
  }

  // Stop any running arrow key scroll animation
  if (m_arrowKeyScrollHelper) {
    m_arrowKeyScrollHelper->stopAnimation();
  }
}

void ScrollManager::setupScrollSuppression() {
  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;
  if (!state) {
    return;
  }

  state->arrow().suppressArrowCenter = true;
  qint64 until =
      QDateTime::currentMSecsSinceEpoch() + UIConstants::Mouse::WHEEL_SUPPRESS_ARROW_CENTER_MS;
  state->arrow().suppressArrowCenterUntilMs = until;

  // Kartend-43ngf: restart the reusable single-shot timer instead of allocating
  // a fresh QTimer::singleShot per scroll event. It clears the suppression after
  // the window expires (timestamp-checked in its timeout handler).
  m_arrowCenterClearTimer.start();
}

void ScrollManager::finalizeScrollChanges() {
  // Delay clearing UserScrollActive to allow any pending scroll events to be
  // processed with the flag still set. After clearing, the deferred artwork
  // refresh fires. Kartend-43ngf: restart the reusable single-shot timer
  // instead of allocating a fresh QTimer::singleShot per scroll event (its
  // handler clears userScrollActive and runs the debounced artwork refresh).
  m_scrollSettleTimer.start();

  notifyUserActivity();
  if (!m_scrollTimer.isActive()) {
    m_scrollTimer.start();
  }
}

void ScrollManager::onThrottledUpdate() {
  // Kartend-73ql5: while a vertical scroll animation is running, its
  // QPropertyAnimation::valueChanged already drives updateVirtualView() once
  // per frame (AnimationManager → requestVirtualViewUpdate). The throttle
  // timer was started from handleProgrammaticScroll() for the same scrollbar
  // movement, so firing it here would dispatch a second, redundant full
  // updateVirtualView per ~100ms window on top of the per-frame ones. Skip it
  // while the animation owns the frame — the animation's final frame leaves the
  // view current. A non-animated programmatic jump (verticalAnimActive ==
  // false, no per-frame driver) still refreshes the view here.
  if (auto *state = m_ctx ? m_ctx->interactionState() : nullptr) {
    if (state->verticalAnimActive()) {
      return;
    }
  }
  updateVirtualView();
}

void ScrollManager::onSliderMoved(int position) {
  // Prefetch data for the scroll target position during scrollbar drag.
  // This reduces perceived latency by starting range requests before
  // the user releases the scrollbar.
  if (!m_mediaScrollArea || !m_widgetFactory || m_metrics.itemHeight <= 0) {
    return;
  }

  const int subcollectionCount = m_dataManager ? m_dataManager->subcollectionCount() : 0;
  const int virtualFolderCount = m_dataManager ? m_dataManager->virtualFolderCount() : 0;

  const auto plan = ScrollHelpers::computeSliderPrefetchPlan(
      position, m_metrics.itemHeight, m_metrics.itemsPerRow, subcollectionCount, virtualFolderCount,
      m_context.config.showAllSubcollectionItems, m_totalItems,
      UIConstants::Database::RANGE_CHUNK_LARGE_THRESHOLD,
      UIConstants::Database::RANGE_CHUNK_SIZE_DEFAULT,
      UIConstants::Database::RANGE_CHUNK_SIZE_LARGE);

  if (!plan.valid) {
    return;
  }
  m_widgetFactory->prefetchRangeAt(plan.chunkStart, plan.chunkSize);
}

void ScrollManager::onSubcollectionDoubleClicked(int subcollectionIndex) {
  emit subcollectionEntered(subcollectionIndex);
}

void ScrollManager::onVirtualFolderDoubleClicked(const QString &folderPath) {
  emit virtualFolderEntered(folderPath);
}
