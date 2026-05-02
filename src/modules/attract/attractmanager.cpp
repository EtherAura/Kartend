// Idle-triggered autoscroll (attract mode) for Kartend (Kartend-1pp).
// When the user is idle for a configurable timeout, the viewport smoothly
// scrolls through the collection, reversing at top/bottom boundaries.
// Any user interaction immediately stops the autoscroll.
#include "attractmanager.h"

#include "applicationcontext.h"
#include "collectionutils.h"
#include "scrollmanager.h"
#include "uiconstants.h"

#include <QScrollBar>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcAttractManager, "kartend.attractmanager")

// ─────────────────────────────────────────────────────────────────────────────
// Setup struct getters (ctx fallback pattern)
// ─────────────────────────────────────────────────────────────────────────────
SETUP_GETTER_DEF_UI_SAME(AttractManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_MGR_SAME(AttractManagerSetup, ScrollManager *, ScrollManager, scrollManager)
SETUP_GETTER_DEF_COL_SAME(AttractManagerSetup, const bool *, IsShuttingDown, isShuttingDown)
SETUP_GETTER_DEF_COL_SAME(AttractManagerSetup, const GeneralSettings *, GeneralSettings, generalSettings)

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
AttractManager::AttractManager(QObject *parent)
    : QObject(parent), m_idleTimer(new QTimer(this)), m_scrollTimer(new QTimer(this)),
      m_bouncePauseTimer(new QTimer(this)) {
  m_idleTimer->setSingleShot(true);
  m_scrollTimer->setTimerType(Qt::PreciseTimer);
  m_bouncePauseTimer->setSingleShot(true);

  connect(m_idleTimer, &QTimer::timeout, this, &AttractManager::onIdleTimeout);
  connect(m_scrollTimer, &QTimer::timeout, this, &AttractManager::onScrollTick);
  connect(m_bouncePauseTimer, &QTimer::timeout, this, &AttractManager::onBouncePauseFinished);
}

AttractManager::~AttractManager() = default;

void AttractManager::setupReferences(const AttractManagerSetup &setup) {
  m_itemScrollArea = setup.getItemScrollArea();
  m_scrollManager = setup.getScrollManager();
  m_generalSettings = setup.getGeneralSettings();
  m_isShuttingDown = setup.getIsShuttingDown();

  reloadSettings();
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────
bool AttractManager::isEnabled() const {
  return m_generalSettings && m_generalSettings->attractModeEnabled;
}

void AttractManager::reloadSettings() {
  if (!m_generalSettings) {
    stopAttract();
    m_idleTimer->stop();
    return;
  }

  // Always keep the interval in sync so that enabling attract mode later
  // doesn't fire with a stale / zero interval.
  const int timeoutSec = qBound(UIConstants::Attract::MIN_IDLE_TIMEOUT_SEC,
                                 m_generalSettings->attractModeIdleTimeoutSec,
                                 UIConstants::Attract::MAX_IDLE_TIMEOUT_SEC);
  m_idleTimer->setInterval(timeoutSec * 1000);

  if (!m_generalSettings->attractModeEnabled) {
    stopAttract();
    m_idleTimer->stop();
    return;
  }

  // If not currently active, (re)start the idle countdown
  if (!m_attractActive) {
    resetIdleTimer();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Activity detection
// ─────────────────────────────────────────────────────────────────────────────
void AttractManager::onActivityDetected() {
  if (m_attractActive) {
    stopAttract();
  }
  resetIdleTimer();
}

void AttractManager::resetIdleTimer() {
  if (!isEnabled()) {
    return;
  }
  if (m_isShuttingDown && *m_isShuttingDown) {
    return;
  }
  // Re-sync interval from the live GeneralSettings pointer every time so that
  // changes made through the settings dialog take effect without requiring an
  // explicit reloadSettings() call.
  if (m_generalSettings) {
    const int timeoutSec = qBound(UIConstants::Attract::MIN_IDLE_TIMEOUT_SEC,
                                   m_generalSettings->attractModeIdleTimeoutSec,
                                   UIConstants::Attract::MAX_IDLE_TIMEOUT_SEC);
    m_idleTimer->setInterval(timeoutSec * 1000);
  }
  m_idleTimer->start();
}

// ─────────────────────────────────────────────────────────────────────────────
// Attract mode start / stop
// ─────────────────────────────────────────────────────────────────────────────
void AttractManager::onIdleTimeout() {
  if (!isEnabled()) {
    return;
  }
  if (m_isShuttingDown && *m_isShuttingDown) {
    return;
  }
  startAttract();
}

void AttractManager::startAttract() {
  if (m_attractActive) {
    return;
  }
  if (!m_itemScrollArea) {
    return;
  }

  QScrollBar *vBar = m_itemScrollArea->verticalScrollBar();
  if (!vBar || vBar->maximum() <= 0) {
    // Nothing to scroll - restart idle timer to try again later
    resetIdleTimer();
    return;
  }

  m_attractActive = true;
  m_scrollDirection = 1; // start scrolling down
  m_bouncePaused = false;

  m_scrollTimer->start(UIConstants::Attract::SCROLL_TICK_INTERVAL_MS);

  qCDebug(lcAttractManager) << "Attract mode started";
  emit attractStarted();
}

void AttractManager::stopAttract() {
  if (!m_attractActive) {
    return;
  }

  m_attractActive = false;
  m_bouncePaused = false;
  m_scrollTimer->stop();
  m_bouncePauseTimer->stop();

  qCDebug(lcAttractManager) << "Attract mode stopped";
  emit attractStopped();
}

// ─────────────────────────────────────────────────────────────────────────────
// Autoscroll tick
// ─────────────────────────────────────────────────────────────────────────────
void AttractManager::onScrollTick() {
  if (!m_attractActive || m_bouncePaused) {
    return;
  }
  // If the user disabled attract mode via settings while scrolling, stop.
  if (!isEnabled()) {
    stopAttract();
    return;
  }
  if (!m_itemScrollArea) {
    stopAttract();
    return;
  }

  QScrollBar *vBar = m_itemScrollArea->verticalScrollBar();
  if (!vBar) {
    stopAttract();
    return;
  }

  const int speed = m_generalSettings
                        ? qBound(UIConstants::Attract::MIN_SCROLL_SPEED_PX,
                                 m_generalSettings->attractModeScrollSpeed,
                                 UIConstants::Attract::MAX_SCROLL_SPEED_PX)
                        : UIConstants::Attract::DEFAULT_SCROLL_SPEED_PX;

  const int current = vBar->value();
  const int next = current + (speed * m_scrollDirection);

  if (next >= vBar->maximum()) {
    vBar->setValue(vBar->maximum());
    // Reached bottom - pause then reverse
    m_bouncePaused = true;
    m_bouncePauseTimer->start(UIConstants::Attract::BOUNCE_PAUSE_MS);
    return;
  }
  if (next <= vBar->minimum()) {
    vBar->setValue(vBar->minimum());
    // Reached top - pause then reverse
    m_bouncePaused = true;
    m_bouncePauseTimer->start(UIConstants::Attract::BOUNCE_PAUSE_MS);
    return;
  }

  vBar->setValue(next);

  // Keep virtual scrolling view up to date
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
  }
}

void AttractManager::onBouncePauseFinished() {
  if (!m_attractActive) {
    return;
  }

  m_bouncePaused = false;
  m_scrollDirection = -m_scrollDirection; // reverse direction

  qCDebug(lcAttractManager) << "Bounce: reversing to direction" << m_scrollDirection;
}
