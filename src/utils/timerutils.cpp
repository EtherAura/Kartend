#include "timerutils.h"

#include "uiconstants.h"
#include <QApplication>
#include <QTimer>

// Creates a coordinator for coalesced layout and viewport updates
TimerUtils::Coordinator::Coordinator(QObject *parent)
    : QObject(parent), m_layoutTimer(new QTimer(this)),
      m_viewportTimer(new QTimer(this)) {
  m_layoutTimer->setSingleShot(true);
  m_layoutTimer->setInterval(UIConstants::LAYOUT_UPDATE_DELAY);
  connect(m_layoutTimer, &QTimer::timeout, this,
          &TimerUtils::Coordinator::onLayoutTimerTimeout);

  m_viewportTimer->setSingleShot(true);
  m_viewportTimer->setInterval(UIConstants::VIEWPORT_UPDATE_DELAY);
  connect(m_viewportTimer, &QTimer::timeout, this,
          &TimerUtils::Coordinator::onViewportTimerTimeout);
}

// Destroys coordinator timers
TimerUtils::Coordinator::~Coordinator() {
  if (m_layoutTimer != nullptr) {
    m_layoutTimer->stop();
    m_layoutTimer->disconnect();
    delete m_layoutTimer;
    m_layoutTimer = nullptr;
  }
  if (m_viewportTimer != nullptr) {
    m_viewportTimer->stop();
    m_viewportTimer->disconnect();
    delete m_viewportTimer;
    m_viewportTimer = nullptr;
  }
}

// Schedules a layout update if not already pending
void TimerUtils::Coordinator::scheduleLayoutUpdate() {
  if ((m_layoutTimer != nullptr) && !m_layoutTimer->isActive()) {
    m_layoutTimer->start();
  }
}

// Schedules a viewport update if not already pending
void TimerUtils::Coordinator::scheduleViewportUpdate() {
  if ((m_viewportTimer != nullptr) && !m_viewportTimer->isActive()) {
    m_viewportTimer->start();
  }
}

// Stops both coordinator timers
void TimerUtils::Coordinator::stopAllTimers() {
  if (m_layoutTimer != nullptr) {
    m_layoutTimer->stop();
  }
  if (m_viewportTimer != nullptr) {
    m_viewportTimer->stop();
  }
}

// Emits layout update when timer fires
void TimerUtils::Coordinator::onLayoutTimerTimeout() {
  if (!QApplication::closingDown()) {
    emit layoutUpdateRequested();
  }
}

// Emits viewport update when timer fires
void TimerUtils::Coordinator::onViewportTimerTimeout() {
  if (!QApplication::closingDown()) {
    emit viewportUpdateRequested();
  }
}

// Stops a list of timers
void TimerUtils::stopTimers(const QList<QTimer *> &timers) {
  for (QTimer *timerPtr : timers) {
    if ((timerPtr != nullptr) && timerPtr->isActive()) {
      timerPtr->stop();
    }
  }
}

// Disconnects a list of timers
void TimerUtils::disconnectTimers(const QList<QTimer *> &timers) {
  for (QTimer *timerPtr : timers) {
    if (timerPtr != nullptr) {
      QObject::disconnect(timerPtr, nullptr, nullptr, nullptr);
    }
  }
}

// Stops and disconnects a list of timers
void TimerUtils::stopAndDisconnectTimers(const QList<QTimer *> &timers) {
  stopTimers(timers);
  disconnectTimers(timers);
}

// Deletes a timer later and nulls the pointer
void TimerUtils::deleteLaterTimer(QTimer *&timer) {
  if (timer != nullptr) {
    timer->deleteLater();
    timer = nullptr;
  }
}
