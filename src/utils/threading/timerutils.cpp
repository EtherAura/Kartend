// Provides debounced timer coordination for viewport and layout updates.
#include "timerutils.h"

#include "uiconstants/timing.h"
#include <QApplication>
#include <QTimer>

// ============================================================================
// BoundedWait Implementation
// ============================================================================

TimerUtils::BoundedWait::BoundedWait(QObject *parent) : QObject(parent) {}

TimerUtils::BoundedWait::~BoundedWait() = default;

void TimerUtils::BoundedWait::finish() {
  m_finished = true;
  m_loop.quit();
}

bool TimerUtils::BoundedWait::run(int timeoutMs) {
  if (m_finished) {
    // Finished before the wait began: nothing to wait for, and a quit() issued
    // before exec() would not end the loop below.
    return true;
  }
  bool timedOut = false;
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, &m_loop, [this, &timedOut]() {
    timedOut = true;
    m_loop.quit();
  });
  watchdog.start(timeoutMs);
  m_loop.exec();
  // exec() comes back with neither flag set in two cases, both while the
  // application is quitting: Qt refused to enter the loop (it returned -1 at
  // once), or a quit that arrived during the wait ended every loop of the
  // thread. The operation is still in flight and still needs its events, so
  // process them here until it finishes or the watchdog fires. The watchdog is
  // itself a pending event, so waiting for more events cannot outlast the
  // deadline.
  while (!m_finished && !timedOut) {
    QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);
  }
  return m_finished;
}

// ============================================================================
// DebouncedTimer Implementation
// ============================================================================

TimerUtils::DebouncedTimer::DebouncedTimer(int intervalMs, QObject *parent)
    : QObject(parent), m_timer(new QTimer(this)) {
  m_timer->setSingleShot(true);
  m_timer->setInterval(intervalMs);
  connect(m_timer, &QTimer::timeout, this, [this]() {
    if (!QApplication::closingDown()) {
      emit triggered();
    }
  });
}

TimerUtils::DebouncedTimer::~DebouncedTimer() {
  if (m_timer) {
    m_timer->stop();
    m_timer->disconnect();
  }
}

void TimerUtils::DebouncedTimer::trigger() {
  if (m_timer) {
    m_timer->start(); // Restarts timer, extending the debounce window
  }
}

void TimerUtils::DebouncedTimer::cancel() {
  if (m_timer) {
    m_timer->stop();
  }
}

void TimerUtils::DebouncedTimer::setInterval(int intervalMs) {
  if (m_timer) {
    m_timer->setInterval(intervalMs);
  }
}

// ============================================================================
// Coordinator Implementation
// ============================================================================

// Creates a coordinator for coalesced layout and viewport updates
TimerUtils::Coordinator::Coordinator(QObject *parent)
    : QObject(parent), m_layoutTimer(new QTimer(this)), m_viewportTimer(new QTimer(this)) {
  m_layoutTimer->setSingleShot(true);
  m_layoutTimer->setInterval(UIConstants::Timing::LAYOUT_UPDATE_DELAY_MS);
  connect(m_layoutTimer, &QTimer::timeout, this, &TimerUtils::Coordinator::onLayoutTimerTimeout);

  m_viewportTimer->setSingleShot(true);
  m_viewportTimer->setInterval(UIConstants::Timing::VIEWPORT_UPDATE_DELAY_MS);
  connect(m_viewportTimer, &QTimer::timeout, this,
          &TimerUtils::Coordinator::onViewportTimerTimeout);
}

// Destroys coordinator timers
TimerUtils::Coordinator::~Coordinator() {
  if (m_layoutTimer) {
    m_layoutTimer->stop();
    m_layoutTimer->disconnect();
    delete m_layoutTimer;
    m_layoutTimer = nullptr;
  }
  if (m_viewportTimer) {
    m_viewportTimer->stop();
    m_viewportTimer->disconnect();
    delete m_viewportTimer;
    m_viewportTimer = nullptr;
  }
}

// Schedules a layout update if not already pending
void TimerUtils::Coordinator::scheduleLayoutUpdate() {
  if ((m_layoutTimer) && !m_layoutTimer->isActive()) {
    m_layoutTimer->start();
  }
}

// Schedules a viewport update if not already pending
void TimerUtils::Coordinator::scheduleViewportUpdate() {
  if ((m_viewportTimer) && !m_viewportTimer->isActive()) {
    m_viewportTimer->start();
  }
}

// Stops both coordinator timers
void TimerUtils::Coordinator::stopAllTimers() {
  if (m_layoutTimer) {
    m_layoutTimer->stop();
  }
  if (m_viewportTimer) {
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
  for (QTimer *const timerPtr : timers) {
    if ((timerPtr) && timerPtr->isActive()) {
      timerPtr->stop();
    }
  }
}

// Disconnects a list of timers
void TimerUtils::disconnectTimers(const QList<QTimer *> &timers) {
  for (const QTimer *const timerPtr : timers) {
    if (timerPtr) {
      QObject::disconnect(timerPtr, nullptr, nullptr, nullptr);
    }
  }
}

// Stops and disconnects a list of timers
void TimerUtils::stopAndDisconnectTimers(const QList<QTimer *> &timers) {
  stopTimers(timers);
  disconnectTimers(timers);
}
