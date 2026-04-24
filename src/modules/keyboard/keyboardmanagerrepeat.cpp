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
  // Kartend-9cl: scale the key-repeat cadence by the global scroll-velocity
  // multiplier. Higher multiplier → shorter interval → more items/second
  // while the arrow key is held. Guard against zero-division and clamp the
  // effective interval to at least 10ms to avoid saturating the event loop.
  const double velocityMult =
      m_generalSettings ? m_generalSettings->scrollVelocityMultiplier : 1.0;
  if (velocityMult > 0.0 && velocityMult != 1.0) {
    baseInterval = qMax(10, static_cast<int>(baseInterval / velocityMult + 0.5));
  }
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

  if (m_state) {
    m_state->scroll().horizHoldActive = !m_repeatVertical;
    m_state->scroll().keyContinuous = true;
  }
  m_continuousScrollActive = true;

  if (m_state) {
    m_state->arrow().arrowKeyScrolling = true;
  }

  if (m_state) {
    m_state->artwork().suppressArtwork = true;
    m_state->artwork().allowDuringSelection = true;
    if (!m_repeatVertical) {
      m_state->arrow().suppressArrowCenter = true;
      m_state->arrow().suppressArrowCenterUntilMs =
          QDateTime::currentMSecsSinceEpoch() + kSuppressArrowCenterHoldMs;
    }
  }

  m_repeatTimer->start(m_repeatInterval);
}

void KeyboardManager::stopRepeat(bool suppressRecentering) {
  if (m_isShuttingDown || QApplication::closingDown()) {
    clearRepeatState();
    if (m_state) {
      m_state->scroll().keyContinuous = false;
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

  if (m_state) {
    m_state->scroll().horizHoldActive = false;
    m_state->scroll().keyContinuous = false;
    m_state->click().armFirstClickDelay = false;
    m_state->click().pendingInitialCenter = false;
  }

  if (m_state) {
    m_state->arrow().arrowKeyScrolling = false;
    m_state->setGlideAnimating(false);
    if (m_scrollManager) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }

  if (m_scrollManager) {
    m_scrollManager->setForceSelectionOverlayVisible(false);
  }

  if (m_state) {
    m_state->artwork().suppressArtwork = false;
    m_state->artwork().allowDuringSelection = true;
    m_state->clearArrowCenterSuppression();
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
  if (!m_scrollManager || !m_collections || !m_currentCollectionIndex) {
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
