// Sibling TU: hold-repeat + selection-calculation logic for KeyboardManager.
#include "keyboardmanager.h"

#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QLineEdit>
#include <QScrollArea>
#include <QStackedWidget>
#include <QWidget>

#include "applicationcontext.h"
#include "collectionutils.h"
#include "interactionstateholder.h"
#include "keyboardhelpers.h"
#include "scrollmanager.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcKeyboardManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcKeyboardManager().isDebugEnabled()) {                                                    \
      qCDebug(lcKeyboardManager) << msg;                                                           \
    }                                                                                              \
  } while (0)

void KeyboardManager::beginHoldRepeat() {
  if (m_isShuttingDown) {
    return;
  }

  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;

  // Check if we're in list mode for faster repeat intervals
  bool isListMode = false;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    isListMode = (*m_collections)[*m_currentCollectionIndex].viewType == ViewType::List;
  }

  // Get repeat interval from settings, use list or grid settings based on view
  // mode
  int baseInterval;
  if (isListMode) {
    baseInterval = m_generalSettings ? m_generalSettings->listKeyboardRepeatIntervalMs : 50;
  } else {
    baseInterval = m_generalSettings ? m_generalSettings->keyboardRepeatIntervalMs : 260;
  }
  // scale the key-repeat cadence by the global scroll-velocity
  // multiplier. Higher multiplier → shorter interval → more items/second
  // while the arrow key is held. Helper guards against zero-division and
  // clamps the effective interval to at least 10ms to avoid saturating the
  // event loop.
  const double velocityMult = m_generalSettings ? m_generalSettings->scrollVelocityMultiplier : 1.0;
  baseInterval = KeyboardHelpers::scaleRepeatInterval(baseInterval, velocityMult, 10);
  int verticalInterval = baseInterval;
  int horizontalInterval = baseInterval / 2;
  constexpr qint64 kSuppressArrowCenterHoldMs = 60000; // 60s safeguard window

  if (!m_repeatTimer) {
    m_repeatTimer = new QTimer(this);
    m_repeatTimer->setSingleShot(false);
    connect(m_repeatTimer, &QTimer::timeout, this, &KeyboardManager::onRepeatStep);
  }
  if (m_repeatStartTimer) {
    m_repeatStartTimer->stop();
  }

  m_repeating = true;
  m_repeatInterval = m_repeatVertical ? verticalInterval : horizontalInterval;

  if (state) {
    state->scroll().horizHoldActive = !m_repeatVertical;
    state->scroll().keyContinuous = true;
  }
  m_continuousScrollActive = true;

  if (state) {
    state->arrow().arrowKeyScrolling = true;
    state->artwork().suppressArtwork = true;
    state->artwork().allowDuringSelection = true;
    if (!m_repeatVertical) {
      state->arrow().suppressArrowCenter = true;
      state->arrow().suppressArrowCenterUntilMs =
          QDateTime::currentMSecsSinceEpoch() + kSuppressArrowCenterHoldMs;
    }
  }

  m_repeatTimer->start(m_repeatInterval);
}

void KeyboardManager::stopRepeat(bool suppressRecentering) {
  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;
  IScrollManager *scroll = m_ctx ? m_ctx->scrollManager() : nullptr;

  if (m_isShuttingDown || QApplication::closingDown()) {
    clearRepeatState();
    if (state) {
      state->scroll().keyContinuous = false;
    }
    return;
  }

  if (m_repeatTimer) {
    m_repeatTimer->stop();
  }
  if (m_repeatStartTimer) {
    m_repeatStartTimer->stop();
  }

  clearRepeatState();

  if (state) {
    state->scroll().horizHoldActive = false;
    state->scroll().keyContinuous = false;
    state->click().armFirstClickDelay = false;
    state->click().pendingInitialCenter = false;
    state->arrow().arrowKeyScrolling = false;
    state->setGlideAnimating(false);
    if (scroll) {
      scroll->refreshSelectionOverlayState();
    }
  }

  if (scroll) {
    scroll->setForceSelectionOverlayVisible(false);
  }

  if (state) {
    state->artwork().suppressArtwork = false;
    state->artwork().allowDuringSelection = true;
    state->clearArrowCenterSuppression();
  }

  emit stopRepeatRequested(suppressRecentering);
}

void KeyboardManager::clearRepeatState() {
  m_repeating = false;
  m_repeatKey = Qt::Key_unknown;
  m_repeatDelta = 0;
  m_repeatVertical = false;
  m_wrapSequenceActive = false;
  m_hasPendingNavigationKey = false;
  m_pendingNavigationKey = Qt::Key_unknown;
  m_pendingNavigationKeyAtMs = 0;
}

void KeyboardManager::onRepeatStep() {
  if (!m_repeating || !m_physicalKeyDown || m_repeatDelta == 0) {
    stopRepeat();
    return;
  }
  IScrollManager *scroll = m_ctx ? m_ctx->scrollManager() : nullptr;
  if (!scroll || !m_collections || !m_currentCollectionIndex) {
    stopRepeat();
    return;
  }
  if (!m_stackedWidget || !m_itemsPage || m_stackedWidget->currentWidget() != m_itemsPage) {
    stopRepeat();
    return;
  }
  if (!CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    stopRepeat();
    return;
  }

  emit repeatStepRequested();
}

void KeyboardManager::onRepeatStartTimeout() {
  beginHoldRepeat();
}
