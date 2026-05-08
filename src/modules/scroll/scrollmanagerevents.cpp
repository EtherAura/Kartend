// Scroll event handling and double-click slots extracted from
// scrollmanager.cpp. All remain ScrollManager members and
// access existing class state (m_scrollTimer, m_arrowKeyViewUpdateTimer,
// m_scrollEventHandler, m_arrowKeyScrollHelper, m_state, etc.).
#include "arrowkeyscrollhelper.h"
#include "artworkmanager.h"
#include "interactionstateholder.h"
#include "itemwidgetfactory.h"
#include "scrolldatamanager.h"
#include "scrolleventhandler.h"
#include "scrollmanager.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "timerutils.h"
#include "uiconstants.h"

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
  if (!m_scrollTimer) {
    return;
  }

  // Prevent reentrant scroll handling which can occur when scroll animations
  // trigger valueChanged signals during processing
  if (m_processingScrollChange) {
    return;
  }
  m_processingScrollChange = true;

  if (m_state && m_state->scroll().programmaticScroll) {
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
  if (!m_scrollTimer->isActive()) {
    m_scrollTimer->start();
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

  if (m_state) {
    m_state->scroll().userScrollActive = true;
  }

  // Stop any running arrow key scroll animation
  if (m_arrowKeyScrollHelper) {
    m_arrowKeyScrollHelper->stopAnimation();
  }
}

void ScrollManager::setupScrollSuppression() {
  if (!m_state) {
    return;
  }

  m_state->arrow().suppressArrowCenter = true;
  qint64 until =
      QDateTime::currentMSecsSinceEpoch() + UIConstants::Mouse::WHEEL_SUPPRESS_ARROW_CENTER_MS;
  m_state->arrow().suppressArrowCenterUntilMs = until;

  InteractionStateHolder *statePtr = m_state;
  // Clear arrow center suppression after the suppression window expires -
  // checks timestamp to avoid clearing if another suppress was scheduled
  QTimer::singleShot(UIConstants::Keyboard::ARROW_CENTER_CLEAR_CHECK_DELAY_MS, this, [statePtr]() {
    if (!statePtr) {
      return;
    }
    qint64 suppressUntilMs = statePtr->arrow().suppressArrowCenterUntilMs;
    if (suppressUntilMs > 0 && QDateTime::currentMSecsSinceEpoch() < suppressUntilMs) {
      return;
    }
    statePtr->arrow().suppressArrowCenter = false;
  });
}

void ScrollManager::finalizeScrollChanges() {
  // Delay clearing UserScrollActive to allow any pending scroll events
  // to be processed with the flag still set. After clearing, trigger
  // artwork update since it was deferred during scrolling.
  QTimer::singleShot(UIConstants::Mouse::USER_SCROLL_ACTIVE_CLEAR_DELAY_MS, this, [this]() {
    if (m_state) {
      m_state->scroll().userScrollActive = false;
    }
    // Trigger artwork update now that user scroll is
    // complete - artwork loading was deferred while
    // userScrollActive was true
    if (m_artworkManager) {
      m_artworkManager->updateViewportArtwork();
    }
  });

  notifyUserActivity();
  if (!m_scrollTimer->isActive()) {
    m_scrollTimer->start();
  }
}

void ScrollManager::onThrottledUpdate() {
  updateVirtualView();
}

void ScrollManager::onSliderMoved(int position) {
  // Prefetch data for the scroll target position during scrollbar drag.
  // This reduces perceived latency by starting range requests before
  // the user releases the scrollbar.
  if (!m_mediaScrollArea || !m_widgetFactory || m_metrics.itemHeight <= 0) {
    return;
  }

  // Calculate which row the slider position corresponds to
  int targetRow = position / m_metrics.itemHeight;
  int targetIndex = targetRow * m_metrics.itemsPerRow;

  // Calculate the media index offset (accounting for subcollections/virtual
  // folders)
  int subcollectionCount = m_dataManager ? m_dataManager->subcollectionCount() : 0;
  int virtualFolderCount = m_dataManager ? m_dataManager->virtualFolderCount() : 0;
  int prefixCount = subcollectionCount + virtualFolderCount;
  int mediaIndex = qMax(0, targetIndex - prefixCount);

  // Align to chunk boundary for efficient database queries
  int chunkSize = m_context.config.showAllSubcollectionItems &&
                          m_totalItems > UIConstants::Database::RANGE_CHUNK_LARGE_THRESHOLD
                      ? UIConstants::Database::RANGE_CHUNK_SIZE_LARGE
                      : UIConstants::Database::RANGE_CHUNK_SIZE_DEFAULT;
  int chunkStart = (mediaIndex / chunkSize) * chunkSize;

  // Request the chunk at the target position to prefetch data
  if (m_widgetFactory) {
    m_widgetFactory->prefetchRangeAt(chunkStart, chunkSize);
  }
}

void ScrollManager::onSubcollectionDoubleClicked(int subcollectionIndex) {
  emit subcollectionEntered(subcollectionIndex);
}

void ScrollManager::onVirtualFolderDoubleClicked(const QString &folderPath) {
  emit virtualFolderEntered(folderPath);
}
