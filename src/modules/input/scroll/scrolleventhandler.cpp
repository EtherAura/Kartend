#include "scrolleventhandler.h"
#include "interactionstateholder.h"
#include "timerutils.h"
#include "uiconstants/mouse.h"
#include <QAbstractAnimation>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

ScrollEventHandler::ScrollEventHandler(QObject *parent) : QObject(parent) {}

ScrollEventHandler::~ScrollEventHandler() {
  disconnectEvents();
}

void ScrollEventHandler::setScrollArea(QScrollArea *scrollArea) {
  disconnectEvents();
  m_scrollArea = scrollArea;
}

void ScrollEventHandler::connectEvents() {
  if (!m_scrollArea) {
    return;
  }

  if (auto *vBar = m_scrollArea->verticalScrollBar()) {
    connectVerticalEvents(vBar);
  }

  if (auto *hBar = m_scrollArea->horizontalScrollBar()) {
    connectHorizontalEvents(hBar);
  }
}

void ScrollEventHandler::disconnectEvents() {
  if (m_vScrollConn) {
    QObject::disconnect(m_vScrollConn);
    m_vScrollConn = QMetaObject::Connection();
  }
  if (m_hScrollConn) {
    QObject::disconnect(m_hScrollConn);
    m_hScrollConn = QMetaObject::Connection();
  }
}

void ScrollEventHandler::connectVerticalEvents(QScrollBar *scrollBar) {
  m_vScrollConn =
      connect(scrollBar, &QScrollBar::valueChanged, this, &ScrollEventHandler::scrollChanged);

  connect(scrollBar, &QScrollBar::sliderPressed, this,
          [this, scrollBar]() { onSliderPressed(scrollBar); });

  connect(scrollBar, &QScrollBar::sliderReleased, this, [this]() { onSliderReleased(); });

  connect(scrollBar, &QAbstractSlider::actionTriggered, this,
          [this, scrollBar](int) { onActionTriggered(scrollBar); });

  // Emit sliderMoved during drag for prefetch optimization
  connect(scrollBar, &QScrollBar::sliderMoved, this, &ScrollEventHandler::sliderMoved);
}

void ScrollEventHandler::connectHorizontalEvents(QScrollBar *scrollBar) {
  m_hScrollConn =
      connect(scrollBar, &QScrollBar::valueChanged, this, &ScrollEventHandler::scrollChanged);

  connect(scrollBar, &QScrollBar::sliderPressed, this,
          [this, scrollBar]() { onSliderPressed(scrollBar); });

  connect(scrollBar, &QScrollBar::sliderReleased, this, [this]() { onSliderReleased(); });

  connect(scrollBar, &QAbstractSlider::actionTriggered, this,
          [this, scrollBar](int) { onActionTriggered(scrollBar); });
}

void ScrollEventHandler::onSliderPressed(QScrollBar *scrollBar) {
  m_userScrollActive = true;

  if (m_idleTimer) {
    m_idleTimer->trigger();
  }

  if (m_state) {
    m_state->scroll().userScrollActive = true;
  }

  // Stop any arrow key animation
  if (scrollBar) {
    if (auto *arrowKeyAnimation =
            scrollBar->findChild<QPropertyAnimation *>("arrowKeyScrollAnim")) {
      if (arrowKeyAnimation->state() == QAbstractAnimation::Running) {
        arrowKeyAnimation->stop();
      }
    }
  }

  emit userScrollStarted();
}

void ScrollEventHandler::onSliderReleased() {
  m_userScrollActive = false;

  if (m_idleTimer) {
    m_idleTimer->trigger();
  }

  // Delay clearing UserScrollActive to allow any pending scroll events
  // to be processed with the flag still set
  QTimer::singleShot(UIConstants::Mouse::USER_SCROLL_ACTIVE_CLEAR_DELAY_MS, this, [this]() {
    if (m_state) {
      m_state->scroll().userScrollActive = false;
    }
  });

  emit userScrollEnded();
}

void ScrollEventHandler::onActionTriggered(QScrollBar *scrollBar) {
  m_userScrollActive = true;

  if (m_idleTimer) {
    m_idleTimer->trigger();
  }

  if (m_state) {
    m_state->scroll().userScrollActive = true;
  }

  // Stop any arrow key animation (for vertical scrollbar)
  if (scrollBar) {
    if (auto *arrowKeyAnimation =
            scrollBar->findChild<QPropertyAnimation *>("arrowKeyScrollAnim")) {
      if (arrowKeyAnimation->state() == QAbstractAnimation::Running) {
        arrowKeyAnimation->stop();
      }
    }
  }
}
