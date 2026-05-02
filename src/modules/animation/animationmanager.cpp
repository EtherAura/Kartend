// Manages smooth scroll animations with easing curves for vertical and
// horizontal scrolling.
#include "animationmanager.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "gridutils.h"
#include "interactionstateholder.h"
#include "scrollmanager.h"
#include "uiconstants.h"

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

// AnimationManagerSetup getter definitions
SETUP_GETTER_DEF_UI_SAME(AnimationManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_MGR_SAME(AnimationManagerSetup, ScrollManager *, ScrollManager, scrollManager)
SETUP_GETTER_DEF_MGR_SAME(AnimationManagerSetup, ArtworkManager *, ArtworkManager, artworkManager)
SETUP_GETTER_DEF_MGR_CTX_ONLY(AnimationManagerSetup, InteractionStateHolder *, InteractionState,
                          interactionState)

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
  m_generalSettings = setup.generalSettings;
  m_itemScrollArea = setup.getItemScrollArea();
  m_scrollManager = setup.getScrollManager();
  m_artworkManager = setup.getArtworkManager();
  m_state = setup.getInteractionState();
}

// --- Vertical Animation ---

void AnimationManager::ensureVAnimCreated(QScrollBar *vScrollBar) {
  if (!m_vScrollAnim) {
    m_vScrollAnim = new QPropertyAnimation(vScrollBar, "value", this);
  }
}

void AnimationManager::configureAndStartVerticalAnimation(QScrollBar *vScrollBar, int curY,
                                                          int targetY, int duration,
                                                          bool clickScroll, bool clickHoldAdv) {
  ensureVAnimCreated(vScrollBar);

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

  if (m_state) {
    m_state->scroll().programmaticScroll = true;
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
  if (!m_state) {
    return;
  }
  if (enable) {
    m_state->scroll().programmaticScroll = true;
    emit requestSelectionOverlayRefresh();
  } else {
    // Defer clearing ProgrammaticScroll flag until after Qt processes
    // pending events - ensures the property change takes effect atomically
    QTimer::singleShot(0, this, [this]() {
      if (m_state) {
        m_state->scroll().programmaticScroll = false;
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
  if (m_state) {
    m_state->scroll().programmaticScroll = false;
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

  if (m_state) {
    m_state->scroll().programmaticScroll = true;
    emit requestSelectionOverlayRefresh();
  }
  m_hScrollAnim->start();
  // Clear ProgrammaticScroll flag in next event loop iteration - the
  // animation is now running and the flag served its purpose during setup
  QTimer::singleShot(0, this, [this]() {
    if (m_state) {
      m_state->scroll().programmaticScroll = false;
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

// --- Duration Calculation ---

int AnimationManager::computeVerticalCenterDuration(int distance, int itemHeight,
                                                    int verticalSpacing, bool repeatActive,
                                                    int durationMs) {
  int stepSpan = qMax(1, itemHeight + verticalSpacing);
  double rows = static_cast<double>(distance) / static_cast<double>(stepSpan);
  rows = std::max(rows, 1.0);

  // Use provided duration directly
  int baseDuration = durationMs;
  double raw = rows * static_cast<double>(baseDuration);

  int duration = static_cast<int>(std::round(raw));
  duration = qBound(baseDuration, duration, baseDuration);
  return duration;
}

// --- Target Calculation ---

int AnimationManager::computeTargetYForIndex(int index, int gridWidth, int itemHeight,
                                             int verticalSpacing, int viewportHeight,
                                             int scrollbarMax, int totalHeight, int logicalHeight,
                                             int headerOffset) {
  int itemY = GridUtils::computeItemY(index, gridWidth, itemHeight, verticalSpacing,
                                      UIConstants::Grid::MARGINS);
  // Add header offset for list view mode
  itemY += headerOffset;
  // Calculate target in logical space first (center the item)
  int logicalTargetY = itemY + (itemHeight / 2) - (viewportHeight / 2);

  // For clipped grids, convert logical scroll target to widget scroll position
  // using viewport-aware interpolation for precise endpoint mapping
  int targetY = logicalTargetY;
  if (totalHeight > 0 && logicalHeight > totalHeight && viewportHeight > 0) {
    int widgetMax = totalHeight - viewportHeight;
    int logicalMax = logicalHeight - viewportHeight;
    if (logicalMax > 0 && widgetMax > 0) {
      // Clamp logical target to valid range before conversion
      logicalTargetY = qBound(0, logicalTargetY, logicalMax);
      // Convert: widgetScrollY = logicalScrollY * widgetMax / logicalMax
      targetY = static_cast<int>(static_cast<double>(logicalTargetY) * widgetMax / logicalMax);
    }
  }

  return qBound(0, targetY, scrollbarMax);
}

int AnimationManager::computeHorizontalTargetX(int itemX, int collectionItemWidth, int curX,
                                               int viewportWidth, int margins, int scrollMax) {
  int itemLeft = itemX;
  int itemRight = itemX + collectionItemWidth;
  int visibleLeft = curX;
  int visibleRight = curX + viewportWidth;

  int targetX = curX;

  // Check if item is out of view on the left
  if (itemLeft < visibleLeft + margins) {
    targetX = itemLeft - margins;
  }
  // Check if item is out of view on the right
  else if (itemRight > visibleRight - margins) {
    targetX = itemRight - viewportWidth + margins;
  }

  return qBound(0, targetX, scrollMax);
}

int AnimationManager::computeDesiredYForVisibility(int itemY, int itemHeight, int curY,
                                                   int viewportHeight, int margins,
                                                   bool &needVertical) {
  int itemTop = itemY;
  int itemBottom = itemY + itemHeight;
  int visibleTop = curY;
  int visibleBottom = curY + viewportHeight;

  needVertical = false;
  int desiredY = curY;

  // Check if item is above the visible area
  if (itemTop < visibleTop + margins) {
    desiredY = itemTop - margins;
    needVertical = true;
  }
  // Check if item is below the visible area
  else if (itemBottom > visibleBottom - margins) {
    desiredY = itemBottom - viewportHeight + margins;
    needVertical = true;
  }

  return desiredY;
}

// --- Ensure Visible Animation ---

void AnimationManager::startEnsureVisibleVAnim(QScrollBar *vScrollBar, int startVal, int endVal,
                                               int itemHeight, int verticalSpacing,
                                               bool isRepeating) {
  ensureVAnimCreated(vScrollBar);

  if (m_vScrollAnim->state() == QAbstractAnimation::Running) {
    m_vScrollAnim->stop();
  }

  int distance = qAbs(endVal - startVal);
  int durationMs = m_generalSettings ? m_generalSettings->scrollAnimationDurationMs : 1500;
  int duration =
      computeVerticalCenterDuration(distance, itemHeight, verticalSpacing, isRepeating, durationMs);

  m_vScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  m_vScrollAnim->setStartValue(startVal);
  m_vScrollAnim->setEndValue(endVal);
  m_vScrollAnim->setDuration(duration);

  QObject::disconnect(m_vScrollAnim, nullptr, this, nullptr);
  connect(m_vScrollAnim, &QPropertyAnimation::valueChanged, this,
          [this]() { emit requestVirtualViewUpdate(); });
  connect(m_vScrollAnim, &QPropertyAnimation::finished, this,
          &AnimationManager::onVScrollAnimationFinished);

  if (m_state) {
    m_state->scroll().programmaticScroll = true;
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

  m_vScrollAnim->setStartValue(effectiveStart);
  m_vScrollAnim->setEndValue(endVal);

  // Use configured scroll animation duration for consistent glide feel.
  // The chaining from current position prevents stuttering while preserving
  // the smooth deceleration over the full duration.
  int duration = m_generalSettings ? m_generalSettings->scrollAnimationDurationMs
                                   : UIConstants::Animation::SMOOTH_SCROLL_WHEEL_DURATION_MS;

  m_vScrollAnim->setDuration(duration);

  // OutCubic provides smooth deceleration for natural momentum feel
  m_vScrollAnim->setEasingCurve(QEasingCurve::OutCubic);

  QObject::disconnect(m_vScrollAnim, nullptr, this, nullptr);
  connect(m_vScrollAnim, &QPropertyAnimation::valueChanged, this,
          [this]() { emit requestVirtualViewUpdate(); });
  connect(m_vScrollAnim, &QPropertyAnimation::finished, this, onFinished);

  m_vScrollAnim->start();
}
