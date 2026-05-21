#include "arrowkeyscrollhelper.h"
#include "collectionutils.h"
#include "interactionstateholder.h"
#include "scrolleventhandler.h"
#include "uiconstants/grid.h"
#include "uiconstants/item.h"
#include <cmath>
#include <QDateTime>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

ArrowKeyScrollHelper::ArrowKeyScrollHelper(QObject *parent) : QObject(parent) {
  m_updateTimer = new QTimer(this);
  m_updateTimer->setSingleShot(true);
}

ArrowKeyScrollHelper::~ArrowKeyScrollHelper() {
  if (m_updateTimer) {
    m_updateTimer->stop();
  }
}

void ArrowKeyScrollHelper::setItemMetrics(int itemHeight, int verticalSpacing, int margins) {
  m_itemHeight = itemHeight;
  m_verticalSpacing = verticalSpacing;
  m_margins = margins;
}

void ArrowKeyScrollHelper::scheduleUpdate(int selectedIndex, bool extendedHold) {
  if (!m_updateTimer) {
    return;
  }

  bool suppressArrowCenter = m_state && m_state->arrow().suppressArrowCenter;
  bool userScrollActiveProp = m_state && m_state->scroll().userScrollActive;
  bool programmatic = m_state && m_state->scroll().programmaticScroll;

  bool timeSuppressed = false;
  if (m_state) {
    qint64 until = m_state->arrow().suppressArrowCenterUntilMs;
    if (until > 0 && QDateTime::currentMSecsSinceEpoch() < until) {
      timeSuppressed = true;
    }
  }

  bool userScrollActiveFromHandler =
      m_scrollEventHandler && m_scrollEventHandler->isUserScrollActive();

  if (!suppressArrowCenter && !timeSuppressed && !userScrollActiveProp &&
      !userScrollActiveFromHandler && !programmatic) {
    static constexpr int ARROW_KEY_UPDATE_DELAY_EXTENDED_MS = 16;
    static constexpr int ARROW_KEY_UPDATE_DELAY_NORMAL_MS = 8;
    const int delayMs =
        extendedHold ? ARROW_KEY_UPDATE_DELAY_EXTENDED_MS : ARROW_KEY_UPDATE_DELAY_NORMAL_MS;
    m_updateTimer->start(delayMs);
  }

  Q_UNUSED(selectedIndex)
}

auto ArrowKeyScrollHelper::shouldSkipUpdate() const -> bool {
  if ((m_scrollEventHandler && m_scrollEventHandler->isUserScrollActive()) ||
      (m_state && m_state->arrow().suppressArrowCenter) ||
      (m_state && m_state->scroll().userScrollActive)) {
    return true;
  }

  qint64 until = m_state ? m_state->arrow().suppressArrowCenterUntilMs : 0;
  return (until > 0 && QDateTime::currentMSecsSinceEpoch() < until);
}

void ArrowKeyScrollHelper::performUpdate(int selectedIndex, int totalItems, int itemsPerRow,
                                         std::function<int(int)> getItemPositionY) {
  if (!m_scrollArea || selectedIndex < 0 || selectedIndex >= totalItems) {
    return;
  }

  QScrollBar *verticalScrollBar = m_scrollArea->verticalScrollBar();
  if (!verticalScrollBar) {
    return;
  }

  if (shouldSkipUpdate()) {
    stopAnimation();
    return;
  }

  int viewportHeight = m_scrollArea->viewport() ? m_scrollArea->viewport()->height() : 0;
  if (viewportHeight <= 0) {
    return;
  }

  int itemY = getItemPositionY(selectedIndex);
  int target = calculateCenterTarget(itemY, viewportHeight);
  int current = verticalScrollBar->value();

  if (qAbs(current - target) <= 2) {
    emit requestViewUpdate();
    return;
  }

  setupAndStartAnimation(verticalScrollBar, current, target);

  Q_UNUSED(itemsPerRow)
}

auto ArrowKeyScrollHelper::calculateCenterTarget(int itemY, int viewportHeight) const -> int {
  int itemHeight = m_itemHeight > 0 ? m_itemHeight : UIConstants::Item::DEFAULT_HEIGHT;
  int margins = m_margins > 0 ? m_margins : UIConstants::Grid::MARGINS;

  int target = (margins + itemY) + (itemHeight / 2) - (viewportHeight / 2);

  if (!m_scrollArea) {
    return qMax(0, target);
  }

  QScrollBar *verticalScrollBar = m_scrollArea->verticalScrollBar();
  if (!verticalScrollBar) {
    return qMax(0, target);
  }

  return qBound(0, target, verticalScrollBar->maximum());
}

void ArrowKeyScrollHelper::setupAndStartAnimation(QScrollBar *scrollBar, int current, int target) {
  // Get base duration from settings
  int baseDuration = m_generalSettings ? m_generalSettings->scrollAnimationDurationMs : 1500;

  int rowStride = m_itemHeight + m_verticalSpacing;
  int singleRowDuration = baseDuration + baseDuration;
  singleRowDuration = std::max(baseDuration, std::min(singleRowDuration, baseDuration));

  static constexpr double DEFAULT_PIXELS_PER_MILLISECOND = 0.5;
  double pixelsPerMillisecond =
      (rowStride > 0 && singleRowDuration > 0)
          ? static_cast<double>(rowStride) / static_cast<double>(singleRowDuration)
          : DEFAULT_PIXELS_PER_MILLISECOND;

  auto *animation = scrollBar->findChild<QPropertyAnimation *>("arrowKeyScrollAnim");
  if (!animation) {
    animation = new QPropertyAnimation(scrollBar, "value", scrollBar);
    animation->setObjectName("arrowKeyScrollAnim");
    animation->setEasingCurve(QEasingCurve::OutCubic);
  }

  // Check if animation is running and can be extended smoothly
  if (animation->state() == QAbstractAnimation::Running) {
    int animEnd = animation->endValue().toInt();
    int animStart = animation->startValue().toInt();
    bool movingDown = animEnd > animStart;
    bool newTargetDown = target > current;

    // If continuing in the same direction, just update the target
    // This creates smooth continuous scrolling instead of stuttering restarts
    if (movingDown == newTargetDown) {
      int newDistance = std::abs(target - current);
      int newDuration = (pixelsPerMillisecond > 0.0)
                            ? static_cast<int>(std::round(static_cast<double>(newDistance) /
                                                          pixelsPerMillisecond))
                            : singleRowDuration;
      newDuration = std::max(baseDuration, std::min(newDuration, baseDuration));

      // Update end value and duration without stopping - smooth extension
      animation->setEndValue(target);
      animation->setDuration(newDuration);
      return;
    }

    // Direction changed - need to stop and restart
    animation->stop();
  }

  int distance = std::abs(current - target);
  int duration =
      (pixelsPerMillisecond > 0.0)
          ? static_cast<int>(std::round(static_cast<double>(distance) / pixelsPerMillisecond))
          : singleRowDuration;
  duration = std::max(baseDuration, std::min(duration, baseDuration));

  animation->setDuration(duration);
  animation->setStartValue(current);
  animation->setEndValue(target);

  QObject::disconnect(animation, nullptr, this, nullptr);
  connect(animation, &QPropertyAnimation::valueChanged, this,
          &ArrowKeyScrollHelper::requestViewUpdate);

  if (m_state) {
    m_state->scroll().programmaticScroll = true;
  }
  connect(animation, &QPropertyAnimation::finished, this, [this]() {
    if (m_state) {
      m_state->scroll().programmaticScroll = false;
    }
  });

  animation->start();
}

void ArrowKeyScrollHelper::stopAnimation() {
  if (!m_scrollArea) {
    return;
  }

  QScrollBar *verticalScrollBar = m_scrollArea->verticalScrollBar();
  if (!verticalScrollBar) {
    return;
  }

  if (auto *arrowKeyAnimation =
          verticalScrollBar->findChild<QPropertyAnimation *>("arrowKeyScrollAnim")) {
    if (arrowKeyAnimation->state() == QAbstractAnimation::Running) {
      arrowKeyAnimation->stop();
    }
  }
}
