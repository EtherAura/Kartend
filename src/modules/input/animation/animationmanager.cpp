// Manages smooth scroll animations with easing curves for vertical and
// horizontal scrolling.
#include "animationmanager.h"
#include "applicationcontext.h"
#include "gridutils.h"
#include "iartworkmanager.h"
#include "interactionstateholder.h"
#include "scrollmanager.h"
#include "uiconstants/animation.h"

#include <cmath>
#include <QApplication>
#include <QTimer>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcAnimationManager, "kartend.animationmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcAnimationManager().isDebugEnabled()) {                                                   \
      qCDebug(lcAnimationManager) << msg;                                                          \
    }                                                                                              \
  } while (0)

// AnimationManagerSetup getter definitions (non-manager fields only — sibling
// managers are read directly from ctx at runtime).
SETUP_GETTER_DEF_UI_SAME(AnimationManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)

AnimationManager::AnimationManager(QObject *parent) : QObject(parent) {}

AnimationManager::~AnimationManager() {
  if (m_vScrollAnim) {
    m_vScrollAnim->stop();
  }
  if (m_hScrollAnim) {
    m_hScrollAnim->stop();
  }
}

void AnimationManager::setupReferences(const AnimationManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_generalSettings = setup.generalSettings;
  m_itemScrollArea = setup.getItemScrollArea();
}

// --- Vertical Animation ---

void AnimationManager::ensureVAnimCreated(QScrollBar *vScrollBar) {
  if (!m_vScrollAnim) {
    m_vScrollAnim = new QPropertyAnimation(vScrollBar, "value", this);
    // Kartend-73ql5: mirror the animation's run state into the shared
    // interaction state as the single source of truth, so ScrollManager's
    // throttle can tell "a vertical animation is driving updateVirtualView per
    // frame" from "an instant programmatic jump that still needs one throttled
    // refresh". Tracking stateChanged (rather than each start/stop/finish call
    // site) means no path can leave the flag stuck.
    connect(m_vScrollAnim, &QPropertyAnimation::stateChanged, this,
            [this](QAbstractAnimation::State newState, QAbstractAnimation::State) {
              if (auto *state = m_ctx ? m_ctx->interactionState() : nullptr) {
                state->setVerticalAnimActive(newState == QAbstractAnimation::Running);
              }
            });
  }
}

void AnimationManager::configureAndStartVerticalAnimation(QScrollBar *vScrollBar, int curY,
                                                          int targetY, int duration,
                                                          bool clickScroll, bool clickHoldAdv) {
  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;
  ensureVAnimCreated(vScrollBar);

  // TEMPORARY DIAGNOSTIC (2026-08-21) — see VANIM note below.
  qCWarning(lcAnimationManager).nospace()
      << "VANIM configureAndStart from=" << curY << " to=" << targetY << " dur=" << duration
      << " delta=" << (targetY - curY);
  m_vScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  m_vScrollAnim->setStartValue(curY);
  m_vScrollAnim->setEndValue(targetY);
  m_vScrollAnim->setDuration(duration);

  QObject::disconnect(m_vScrollAnim, nullptr, this, nullptr);
  connect(
      m_vScrollAnim, &QPropertyAnimation::valueChanged, this, [this, clickScroll, clickHoldAdv]() {
        // Emit signals for external handling
        emit requestVirtualViewUpdate();
        emit requestSelectionOverlayRefresh();

        // Check for near-completion of click scroll
        if (clickScroll && !clickHoldAdv &&
            qAbs(m_vScrollAnim->currentValue().toInt() - m_vScrollAnim->endValue().toInt()) < 3) {
          // Signal handled externally via properties
        }
      });
  connect(m_vScrollAnim, &QPropertyAnimation::finished, this,
          &AnimationManager::onVScrollAnimationFinished);

  if (state) {
    state->scroll().programmaticScroll = true;
  }
  emit requestSelectionOverlayRefresh();
  m_vScrollAnim->start();
}

void AnimationManager::stopActiveVerticalAnims(QScrollBar *verticalScrollBar) {
  if ((m_vScrollAnim) && m_vScrollAnim->state() == QAbstractAnimation::Running) {
    m_vScrollAnim->stop();
  }
  if (auto *arrowKeyAnim =
          verticalScrollBar->findChild<QPropertyAnimation *>("arrowKeyScrollAnim")) {
    if (arrowKeyAnim->state() == QAbstractAnimation::Running) {
      arrowKeyAnim->stop();
    }
  }
}

bool AnimationManager::isVerticalAnimRunning() const {
  return (m_vScrollAnim) && m_vScrollAnim->state() == QAbstractAnimation::Running;
}

bool AnimationManager::handleExistingVerticalAnimIfRunning(QScrollBar *verticalScrollBar,
                                                           int targetY, bool clickScroll,
                                                           bool clickHoldAdv, int &curY,
                                                           int &distance) {
  if (!isVerticalAnimRunning()) {
    return false;
  }
  if (clickScroll && !clickHoldAdv) {
    m_vScrollAnim->setEndValue(targetY);
    return true;
  }
  m_vScrollAnim->stop();
  curY = verticalScrollBar->value();
  distance = qAbs(targetY - curY);
  return false;
}

void AnimationManager::setProgrammaticScrollGuarded(bool enable) {
  InteractionStateHolder *state = m_ctx ? m_ctx->interactionState() : nullptr;
  if (!state) {
    return;
  }
  if (enable) {
    state->scroll().programmaticScroll = true;
    emit requestSelectionOverlayRefresh();
  } else {
    // Defer clearing ProgrammaticScroll flag until after Qt processes
    // pending events - ensures the property change takes effect atomically
    QTimer::singleShot(0, this, [this]() {
      if (auto *s = m_ctx ? m_ctx->interactionState() : nullptr) {
        s->scroll().programmaticScroll = false;
        emit requestSelectionOverlayRefresh();
      }
    });
  }
}

void AnimationManager::updateVirtualViewAndSelectionDuringVAnim(bool clickScroll,
                                                                bool clickHoldAdv) {
  emit requestVirtualViewUpdate();
  emit requestSelectionUpdate();

  if (clickScroll && !clickHoldAdv && (m_vScrollAnim) &&
      qAbs(m_vScrollAnim->currentValue().toInt() - m_vScrollAnim->endValue().toInt()) < 3) {
    // Signal that click scroll is completing - handled externally
  }
}

void AnimationManager::onVScrollAnimationFinished() {
  emit requestVirtualViewUpdate();
  if (auto *state = m_ctx ? m_ctx->interactionState() : nullptr) {
    state->scroll().programmaticScroll = false;
    emit requestSelectionOverlayRefresh();
  }
  emit verticalAnimationFinished();
}

// --- Horizontal Animation ---

void AnimationManager::initHorizontalAnimIfNeeded(QScrollBar *hScrollBar) {
  if (!m_hScrollAnim) {
    m_hScrollAnim = new QPropertyAnimation(hScrollBar, "value", this);
  }
}

void AnimationManager::animateHorizontalHold(QScrollBar *hScrollBar, int startX, int targetX) {
  initHorizontalAnimIfNeeded(hScrollBar);

  int distance = qAbs(targetX - startX);
  constexpr double kPixelsPerSecond = 700.0;
  constexpr double kMillisecondsPerSecond = 1000.0;
  constexpr int kMinHoldDurationMs = 30;
  int duration =
      static_cast<int>(std::round((distance / kPixelsPerSecond) * kMillisecondsPerSecond));
  duration = std::max(duration, kMinHoldDurationMs);

  m_hScrollAnim->setEasingCurve(QEasingCurve::Linear);
  m_hScrollAnim->setStartValue(startX);
  m_hScrollAnim->setEndValue(targetX);
  m_hScrollAnim->setDuration(duration);

  QObject::disconnect(m_hScrollAnim, nullptr, this, nullptr);
  connect(m_hScrollAnim, &QPropertyAnimation::valueChanged, this,
          [this]() { emit requestVirtualViewUpdate(); });
  connect(m_hScrollAnim, &QPropertyAnimation::finished, this,
          &AnimationManager::onHScrollAnimationFinished);

  // Signal to set GlideAnimating property on grid container
  emit requestGlideAnimationStart();

  if (auto *state = m_ctx ? m_ctx->interactionState() : nullptr) {
    state->scroll().programmaticScroll = true;
    emit requestSelectionOverlayRefresh();
  }
  m_hScrollAnim->start();
  // Clear ProgrammaticScroll flag in next event loop iteration - the
  // animation is now running and the flag served its purpose during setup
  QTimer::singleShot(0, this, [this]() {
    if (auto *s = m_ctx ? m_ctx->interactionState() : nullptr) {
      s->scroll().programmaticScroll = false;
      emit requestSelectionOverlayRefresh();
    }
  });
}

void AnimationManager::animateHorizontalSmooth(QScrollBar *hScrollBar, int startX, int targetX) {
  initHorizontalAnimIfNeeded(hScrollBar);

  if (m_hScrollAnim->state() == QAbstractAnimation::Running) {
    m_hScrollAnim->stop();
  }

  m_hScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  m_hScrollAnim->setStartValue(startX);
  m_hScrollAnim->setEndValue(targetX);
  m_hScrollAnim->setDuration(UIConstants::Animation::HSCROLL_ANIM_DURATION_MS);

  QObject::disconnect(m_hScrollAnim, nullptr, this, nullptr);
  connect(m_hScrollAnim, &QPropertyAnimation::valueChanged, this,
          [this]() { emit requestVirtualViewUpdate(); });
  connect(m_hScrollAnim, &QPropertyAnimation::finished, this,
          &AnimationManager::onHScrollAnimationFinished);

  // Signal to set GlideAnimating property on grid container
  emit requestGlideAnimationStart();

  m_hScrollAnim->start();
}

bool AnimationManager::isHorizontalAnimRunning() const {
  return (m_hScrollAnim) && m_hScrollAnim->state() == QAbstractAnimation::Running;
}

void AnimationManager::stopArrowKeyAnimationIfRunning(QScrollBar *scrollBar) {
  if (!scrollBar) {
    return;
  }
  if (auto *anim = scrollBar->findChild<QPropertyAnimation *>("arrowKeyScrollAnim")) {
    if (anim->state() == QAbstractAnimation::Running) {
      anim->stop();
    }
  }
}

void AnimationManager::onHScrollAnimationFinished() {
  emit requestVirtualViewUpdate();
  emit horizontalAnimationFinished();
}

// Pure-helper static methods (computeVerticalCenterDuration,
// computeTargetYForIndex, computeHorizontalTargetX,
// computeDesiredYForVisibility) live in animationhelpers.cpp so unit tests
// can link them without pulling in ScrollManager / ArtworkManager / etc.

// --- Ensure Visible Animation ---

void AnimationManager::startEnsureVisibleVAnim(QScrollBar *vScrollBar, int startVal, int endVal,
                                               int itemHeight, int verticalSpacing,
                                               bool isRepeating) {
  ensureVAnimCreated(vScrollBar);

  if (m_vScrollAnim->state() == QAbstractAnimation::Running) {
    m_vScrollAnim->stop();
  }

  int distance = qAbs(endVal - startVal);
  int durationMs = m_generalSettings ? m_generalSettings->input.scrollAnimationDurationMs : 1500;
  int duration =
      computeVerticalCenterDuration(distance, itemHeight, verticalSpacing, isRepeating, durationMs);

  // TEMPORARY DIAGNOSTIC (2026-08-21). PRIME SUSPECT: this is the
  // ensure-visible / centre-on-selection glide. A line here whose delta
  // OPPOSES the direction the user is scrolling is the snap-back.
  qCWarning(lcAnimationManager).nospace()
      << "VANIM ensureVisible   from=" << startVal << " to=" << endVal << " dur=" << duration
      << " delta=" << (endVal - startVal);
  m_vScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  m_vScrollAnim->setStartValue(startVal);
  m_vScrollAnim->setEndValue(endVal);
  m_vScrollAnim->setDuration(duration);

  QObject::disconnect(m_vScrollAnim, nullptr, this, nullptr);
  connect(m_vScrollAnim, &QPropertyAnimation::valueChanged, this,
          [this]() { emit requestVirtualViewUpdate(); });
  connect(m_vScrollAnim, &QPropertyAnimation::finished, this,
          &AnimationManager::onVScrollAnimationFinished);

  if (auto *state = m_ctx ? m_ctx->interactionState() : nullptr) {
    state->scroll().programmaticScroll = true;
  }
  m_vScrollAnim->start();
}

// --- Wheel Scroll Animation ---

int AnimationManager::getVerticalAnimEndValue() const {
  if (m_vScrollAnim) {
    return m_vScrollAnim->endValue().toInt();
  }
  return 0;
}

void AnimationManager::startWheelScrollAnimation(QScrollBar *vScrollBar, int startVal, int endVal,
                                                 std::function<void()> onFinished) {
  ensureVAnimCreated(vScrollBar);

  // Chain from current animation position for smooth blending when wheel
  // events arrive in quick succession during slow scrolling
  int effectiveStart = startVal;
  if (m_vScrollAnim->state() == QAbstractAnimation::Running) {
    effectiveStart = m_vScrollAnim->currentValue().toInt();
    m_vScrollAnim->stop();
  }

  // TEMPORARY DIAGNOSTIC (2026-08-21) — the user's own wheel glide. Pair each
  // of these with the next VANIM line: a wheel glide immediately followed by
  // an ensureVisible glide in the OPPOSITE direction is the revert.
  qCWarning(lcAnimationManager).nospace()
      << "VANIM wheelScroll     from=" << effectiveStart << " to=" << endVal
      << " delta=" << (endVal - effectiveStart) << " (requestedStart=" << startVal << ")";
  m_vScrollAnim->setStartValue(effectiveStart);
  m_vScrollAnim->setEndValue(endVal);

  // Use configured scroll animation duration for consistent glide feel.
  // The chaining from current position prevents stuttering while preserving
  // the smooth deceleration over the full duration.
  int duration = m_generalSettings ? m_generalSettings->input.scrollAnimationDurationMs
                                   : UIConstants::Animation::SMOOTH_SCROLL_WHEEL_DURATION_MS;

  m_vScrollAnim->setDuration(duration);

  // OutCubic provides smooth deceleration for natural momentum feel
  m_vScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

  QObject::disconnect(m_vScrollAnim, nullptr, this, nullptr);
  connect(m_vScrollAnim, &QPropertyAnimation::valueChanged, this,
          [this]() { emit requestVirtualViewUpdate(); });
  connect(m_vScrollAnim, &QPropertyAnimation::finished, this, onFinished);
  // Always re-fire the canonical vertical-finished slot so listeners wired in
  // InteractionManager (programmaticScroll cleanup, verticalAnimationFinished
  // consumers) still see the end-of-anim event after the user callback runs —
  // the horizontal twin already does this; the wheel-vertical path didn't,
  // leaving those listeners silently un-notified for wheel scrolls (Kartend-y74i).
  connect(m_vScrollAnim, &QPropertyAnimation::finished, this,
          &AnimationManager::onVScrollAnimationFinished);

  m_vScrollAnim->start();
}

void AnimationManager::startWheelScrollAnimationHorizontal(QScrollBar *hScrollBar, int startVal,
                                                           int endVal,
                                                           std::function<void()> onFinished) {
  initHorizontalAnimIfNeeded(hScrollBar);

  // Same chain-from-current-value trick as the vertical version: when wheel
  // notches stack up faster than the animation completes, blend the next
  // animation from where this one currently is rather than snapping back.
  int effectiveStart = startVal;
  if (m_hScrollAnim->state() == QAbstractAnimation::Running) {
    effectiveStart = m_hScrollAnim->currentValue().toInt();
    m_hScrollAnim->stop();
  }

  m_hScrollAnim->setStartValue(effectiveStart);
  m_hScrollAnim->setEndValue(endVal);

  int duration = m_generalSettings ? m_generalSettings->input.scrollAnimationDurationMs
                                   : UIConstants::Animation::SMOOTH_SCROLL_WHEEL_DURATION_MS;
  m_hScrollAnim->setDuration(duration);
  m_hScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

  QObject::disconnect(m_hScrollAnim, nullptr, this, nullptr);
  connect(m_hScrollAnim, &QPropertyAnimation::valueChanged, this,
          [this]() { emit requestVirtualViewUpdate(); });
  connect(m_hScrollAnim, &QPropertyAnimation::finished, this, onFinished);
  // Always re-fire the canonical horizontal-finished signal so listeners
  // wired in InteractionManager (glide-animating cleanup, overlay refresh)
  // still see the end-of-anim event after the user callback runs.
  connect(m_hScrollAnim, &QPropertyAnimation::finished, this,
          &AnimationManager::onHScrollAnimationFinished);

  // Mirror the smooth-glide signal so any glide-overlay logic latches the
  // same way it does for the snap variant.
  emit requestGlideAnimationStart();

  m_hScrollAnim->start();
}
