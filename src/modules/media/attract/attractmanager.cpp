// Idle-triggered autoscroll (attract mode) for Kartend.
// When the user is idle for a configurable timeout, the viewport smoothly
// scrolls through the collection, reversing at top/bottom boundaries.
// Any user interaction immediately stops the autoscroll.
#include "attractmanager.h"

#include "applicationcontext.h"
#include "collectionutils.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"

#include <QRandomGenerator>
#include <QScrollBar>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcAttractManager, "kartend.attractmanager")

// ─────────────────────────────────────────────────────────────────────────────
// Setup struct getters (ctx fallback pattern, non-manager fields only)
// ─────────────────────────────────────────────────────────────────────────────
SETUP_GETTER_DEF_UI_SAME(AttractManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_COL_SAME(AttractManagerSetup, const bool *, IsShuttingDown, isShuttingDown)
SETUP_GETTER_DEF_COL_SAME(AttractManagerSetup, const GeneralSettings *, GeneralSettings,
                          generalSettings)

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
AttractManager::AttractManager(QObject *parent)
    : QObject(parent), m_idleTimer(new QTimer(this)), m_scrollTimer(new QTimer(this)),
      m_bouncePauseTimer(new QTimer(this)), m_advanceSelectionTimer(new QTimer(this)) {
  m_idleTimer->setSingleShot(true);
  m_scrollTimer->setTimerType(Qt::PreciseTimer);
  m_bouncePauseTimer->setSingleShot(true);

  connect(m_idleTimer, &QTimer::timeout, this, &AttractManager::onIdleTimeout);
  connect(m_scrollTimer, &QTimer::timeout, this, &AttractManager::onScrollTick);
  connect(m_bouncePauseTimer, &QTimer::timeout, this, &AttractManager::onBouncePauseFinished);
  connect(m_advanceSelectionTimer, &QTimer::timeout, this, &AttractManager::onAdvanceSelectionTick);
}

AttractManager::~AttractManager() = default;

void AttractManager::setupReferences(const AttractManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_itemScrollArea = setup.getItemScrollArea();
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

  // Sync the advance-selection interval too so a live settings change is
  // reflected without restarting attract mode.
  const int advanceSec = qBound(UIConstants::Attract::MIN_ADVANCE_INTERVAL_SEC,
                                m_generalSettings->attractModeAdvanceSelectionIntervalSec,
                                UIConstants::Attract::MAX_ADVANCE_INTERVAL_SEC);
  m_advanceSelectionTimer->setInterval(advanceSec * 1000);

  if (!m_generalSettings->attractModeEnabled) {
    stopAttract();
    m_idleTimer->stop();
    return;
  }

  // If attract is already running, honour a runtime toggle of the
  // sub-features (auto-scroll, advance-selection) without forcing a full
  // restart.
  if (m_attractActive) {
    if (m_generalSettings->attractModeAutoScrollEnabled) {
      if (!m_scrollTimer->isActive() && !m_bouncePaused) {
        m_scrollTimer->start(UIConstants::Attract::SCROLL_TICK_INTERVAL_MS);
      }
    } else {
      m_scrollTimer->stop();
      m_bouncePauseTimer->stop();
      m_bouncePaused = false;
    }
    if (m_generalSettings->attractModeAdvanceSelectionEnabled) {
      if (!m_advanceSelectionTimer->isActive()) {
        m_advanceSelectionTimer->start();
      }
    } else {
      m_advanceSelectionTimer->stop();
    }
    // If both sub-features were turned off mid-session, exit attract mode and
    // arm the idle countdown so it can re-enter once one is enabled again.
    if (!m_scrollTimer->isActive() && !m_advanceSelectionTimer->isActive()) {
      stopAttract();
      resetIdleTimer();
    }
    return;
  }

  // If not currently active, (re)start the idle countdown
  resetIdleTimer();
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

void AttractManager::setSuspended(bool suspended) {
  if (m_suspended == suspended) {
    return;
  }
  m_suspended = suspended;
  if (suspended) {
    // Halt everything: any active scroll, the bounce-pause, and the idle
    // countdown. While the launched program is running, nothing in the
    // frontend should be moving.
    if (m_attractActive) {
      stopAttract();
    }
    m_idleTimer->stop();
  } else {
    // On resume, treat the user as freshly active so attract waits the
    // full timeout before kicking in (avoids attract starting the instant
    // the launched program exits, before the user has a chance to look).
    resetIdleTimer();
  }
}

void AttractManager::resetIdleTimer() {
  if (!isEnabled()) {
    return;
  }
  if (m_suspended) {
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
  if (m_suspended) {
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
  if (!m_itemScrollArea || !m_generalSettings) {
    return;
  }

  const bool wantScroll = m_generalSettings->attractModeAutoScrollEnabled;
  const bool wantAdvance = m_generalSettings->attractModeAdvanceSelectionEnabled;
  if (!wantScroll && !wantAdvance) {
    // Both sub-features off — nothing for attract to do. Re-arm the idle
    // timer so we re-evaluate next time the user goes idle.
    resetIdleTimer();
    return;
  }

  // In Horizontal view the long axis is X, so attract drives the horizontal
  // scrollbar; otherwise the vertical one.
  ScrollManager *scroll = m_ctx ? m_ctx->scrollManager() : nullptr;
  const bool isHorizontal = scroll && scroll->getMetrics().isHorizontal;
  QScrollBar *bar = isHorizontal ? m_itemScrollArea->horizontalScrollBar()
                                 : m_itemScrollArea->verticalScrollBar();
  const bool scrollable = bar && bar->maximum() > 0;
  if (wantScroll && !scrollable && !wantAdvance) {
    // Wanted to scroll but content fits viewport, and advance is off too —
    // try again on the next idle cycle.
    resetIdleTimer();
    return;
  }

  m_attractActive = true;
  m_scrollDirection = 1; // start scrolling down
  m_bouncePaused = false;
  m_scrollAccumulator = 0.0;

  if (wantScroll && scrollable) {
    m_scrollTimer->start(UIConstants::Attract::SCROLL_TICK_INTERVAL_MS);
  }
  startAdvanceSelectionTimerIfEnabled();

  qCDebug(lcAttractManager) << "Attract mode started";
  emit attractStarted();
}

void AttractManager::startAdvanceSelectionTimerIfEnabled() {
  if (!m_generalSettings || !m_generalSettings->attractModeAdvanceSelectionEnabled) {
    return;
  }
  const int advanceSec = qBound(UIConstants::Attract::MIN_ADVANCE_INTERVAL_SEC,
                                m_generalSettings->attractModeAdvanceSelectionIntervalSec,
                                UIConstants::Attract::MAX_ADVANCE_INTERVAL_SEC);
  m_advanceSelectionTimer->start(advanceSec * 1000);
}

void AttractManager::stopAttract() {
  if (!m_attractActive) {
    return;
  }

  m_attractActive = false;
  m_bouncePaused = false;
  m_scrollTimer->stop();
  m_bouncePauseTimer->stop();
  m_advanceSelectionTimer->stop();

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

  // Pick the bar that runs along the long axis of the current view.
  ScrollManager *scroll = m_ctx ? m_ctx->scrollManager() : nullptr;
  const bool isHorizontal = scroll && scroll->getMetrics().isHorizontal;
  QScrollBar *bar = isHorizontal ? m_itemScrollArea->horizontalScrollBar()
                                 : m_itemScrollArea->verticalScrollBar();
  if (!bar) {
    stopAttract();
    return;
  }

  const double speed = m_generalSettings ? qBound(UIConstants::Attract::MIN_SCROLL_SPEED_PX,
                                                  m_generalSettings->attractModeScrollSpeed,
                                                  UIConstants::Attract::MAX_SCROLL_SPEED_PX)
                                         : UIConstants::Attract::DEFAULT_SCROLL_SPEED_PX;

  // Sub-pixel accumulator: the scrollbar only accepts ints, so accumulate the
  // fractional pixels-per-tick and only advance the bar by the integer part
  // each time the accumulator crosses 1. Lets very slow speeds (e.g. 0.5
  // px/tick → ~32 px/sec) work even though one frame can't move <1 px.
  m_scrollAccumulator += speed;
  const int delta = static_cast<int>(m_scrollAccumulator);
  if (delta == 0) {
    return;
  }
  m_scrollAccumulator -= delta;

  const int current = bar->value();
  const int next = current + (delta * m_scrollDirection);

  if (next >= bar->maximum()) {
    bar->setValue(bar->maximum());
    m_scrollAccumulator = 0.0;
    // Reached far end - pause then reverse
    m_bouncePaused = true;
    m_bouncePauseTimer->start(UIConstants::Attract::BOUNCE_PAUSE_MS);
    return;
  }
  if (next <= bar->minimum()) {
    bar->setValue(bar->minimum());
    m_scrollAccumulator = 0.0;
    // Reached near end - pause then reverse
    m_bouncePaused = true;
    m_bouncePauseTimer->start(UIConstants::Attract::BOUNCE_PAUSE_MS);
    return;
  }

  bar->setValue(next);

  // Keep virtual scrolling view up to date
  if (scroll) {
    scroll->updateVirtualView();
  }
}

void AttractManager::onBouncePauseFinished() {
  if (!m_attractActive) {
    return;
  }

  m_bouncePaused = false;
  m_scrollDirection = -m_scrollDirection; // reverse direction
  m_scrollAccumulator = 0.0;

  qCDebug(lcAttractManager) << "Bounce: reversing to direction" << m_scrollDirection;
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection advance tick (independent timer)
// ─────────────────────────────────────────────────────────────────────────────
void AttractManager::onAdvanceSelectionTick() {
  if (!m_attractActive || !isEnabled()) {
    return;
  }
  if (m_isShuttingDown && *m_isShuttingDown) {
    return;
  }
  if (!m_generalSettings || !m_generalSettings->attractModeAdvanceSelectionEnabled) {
    m_advanceSelectionTimer->stop();
    return;
  }
  ScrollManager *scroll = m_ctx ? m_ctx->scrollManager() : nullptr;
  SelectionManager *selection = m_ctx ? m_ctx->selectionManager() : nullptr;
  if (!scroll || !selection) {
    return;
  }

  const int total = scroll->getTotalItems();
  if (total <= 0) {
    return;
  }

  const int current = selection->currentSelectedIndex();
  int next;
  if (m_generalSettings->attractModeAdvanceSelectionRandom) {
    // Bias the random pick toward the current scroll direction so selection
    // moves visually in step with the autoscroll. Forward → pick from
    // (current, end); reverse → pick from [0, current). If we're at the
    // boundary in the scroll direction, fall back to the opposite half so
    // the pick still produces a visible change before the next bounce.
    int rangeStart;
    int rangeEnd; // exclusive
    if (m_scrollDirection >= 0) {
      rangeStart = current + 1;
      rangeEnd = total;
      if (rangeStart >= rangeEnd) {
        rangeStart = 0;
        rangeEnd = qMax(0, current);
      }
    } else {
      rangeStart = 0;
      rangeEnd = qMax(0, current);
      if (rangeStart >= rangeEnd) {
        rangeStart = current + 1;
        rangeEnd = total;
      }
    }
    if (rangeStart >= rangeEnd) {
      // Single-item collection (or pathological state); nothing to do.
      next = current;
    } else {
      next = rangeStart + QRandomGenerator::global()->bounded(rangeEnd - rangeStart);
    }
  } else {
    // Wrap around at the end so cycling continues indefinitely.
    next = (current >= 0) ? (current + 1) % total : 0;
  }
  if (next == current) {
    return;
  }

  // Mark this selection change as attract-driven so the wired selectionChanged
  // → onActivityDetected handler skips it (otherwise attract would stop on its
  // own first tick).
  m_drivingSelection = true;
  emit requestSelectIndex(next);
  m_drivingSelection = false;
}
