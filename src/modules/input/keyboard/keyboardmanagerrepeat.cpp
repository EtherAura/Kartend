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
#include "collection/validationhelpers.h"
#include "collectiontypes.h"
#include "interactionstateholder.h"
#include "iselectionoverlayscroll.h"
#include "keyboardhelpers.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcKeyboardManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcKeyboardManager().isDebugEnabled()) {                                                    \
      qCDebug(lcKeyboardManager) << msg;                                                           \
    }                                                                                              \
  } while (0)

void KeyboardManager::applyRepeatPhase(RepeatPhase phase) {
  const bool active = phase != RepeatPhase::Idle;

  // Repeat is one continuous-scroll source among several (wheel, mouse-hold,
  // glide), so it only ASSERTS the shared flag on entry; the clear stays with
  // the animation/arrow handlers that own the glide tail (unchanged behaviour —
  // the old stopRepeat likewise never cleared m_continuousScrollActive).
  if (active) {
    m_continuousScrollActive = true;
  }

  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;
  if (!state) {
    return;
  }

  state->scroll().horizHoldActive = (phase == RepeatPhase::HorizontalRepeat);
  state->scroll().keyContinuous = active;
  state->arrow().arrowKeyScrolling = active;
  state->artwork().suppressArtwork = active;
  // Forced true in BOTH phases pre-refactor (begin sets it, stop re-sets it): a
  // deliberate "artwork may show during a programmatic selection" affordance
  // that repeat never revokes.
  state->artwork().allowDuringSelection = true;

  if (phase == RepeatPhase::HorizontalRepeat) {
    // A fast horizontal sweep suppresses arrow-centering for a long safeguard
    // window so it isn't fighting re-centering on every step.
    constexpr qint64 kSuppressArrowCenterHoldMs = 60000; // 60s safeguard window
    state->arrow().suppressArrowCenter = true;
    state->arrow().suppressArrowCenterUntilMs =
        QDateTime::currentMSecsSinceEpoch() + kSuppressArrowCenterHoldMs;
  } else if (phase == RepeatPhase::Idle) {
    state->clearArrowCenterSuppression();
  }
  // VerticalRepeat leaves arrow-center suppression untouched — pre-refactor
  // beginHoldRepeat only armed it for horizontal and never cleared it here.
}

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
    baseInterval = m_generalSettings ? m_generalSettings->input.listKeyboardRepeatIntervalMs : 50;
  } else {
    baseInterval = m_generalSettings ? m_generalSettings->input.keyboardRepeatIntervalMs : 260;
  }
  // scale the key-repeat cadence by the global scroll-velocity
  // multiplier. Higher multiplier → shorter interval → more items/second
  // while the arrow key is held. Helper guards against zero-division and
  // clamps the effective interval to at least 10ms to avoid saturating the
  // event loop.
  const double velocityMult =
      m_generalSettings ? m_generalSettings->input.scrollVelocityMultiplier : 1.0;
  baseInterval = KeyboardHelpers::scaleRepeatInterval(baseInterval, velocityMult, 10);
  int verticalInterval = baseInterval;
  // Kartend-l06g6: re-apply the 10ms floor AFTER halving — halving a clamped
  // base could yield a 5ms (200Hz) horizontal timer at high velocity
  // multipliers, defeating the event-loop-saturation guard documented above.
  int horizontalInterval = std::max(10, baseInterval / 2);

  // m_repeatTimer is created + connected once in initTimers() (ctor), so the
  // lazy create here was dead and a second run would have double-connected
  // onRepeatStep (Kartend-6bt1).
  if (m_repeatStartTimer) {
    m_repeatStartTimer->stop();
  }

  m_repeating = true;
  m_repeatInterval = m_repeatVertical ? verticalInterval : horizontalInterval;

  // Enter the repeat state: one call sets every repeat-owned flag (Kartend-4su6t).
  applyRepeatPhase(m_repeatVertical ? RepeatPhase::VerticalRepeat : RepeatPhase::HorizontalRepeat);

  m_repeatTimer->start(m_repeatInterval);
}

void KeyboardManager::stopRepeat(bool suppressRecentering) {
  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;
  ISelectionOverlayScroll *scroll = m_ctx ? m_ctx->scrollOverlay() : nullptr;

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

  // Return every repeat-owned flag to Idle in one place (Kartend-4su6t). The
  // clear order relative to the overlay refresh below is irrelevant — the
  // refresh reads none of these flags (only glideAnimating, handled next).
  applyRepeatPhase(RepeatPhase::Idle);

  if (state) {
    // Defensive resets of state repeat does NOT own but that must not survive a
    // stop — a stop doubles as the "cancel any in-flight motion" catch-all.
    // glideAnimating is cleared BEFORE the overlay refresh (which reads it),
    // preserving the pre-refactor ordering.
    state->click().armFirstClickDelay = false;
    state->click().pendingInitialCenter = false;
    state->setGlideAnimating(false);
    if (scroll) {
      scroll->refreshSelectionOverlayState();
    }
  }

  if (scroll) {
    scroll->setForceSelectionOverlayVisible(false);
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
  ISelectionOverlayScroll *scroll = m_ctx ? m_ctx->scrollOverlay() : nullptr;
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
