// Idle-triggered autoscroll (attract mode) for Kartend.
// When the user is idle for a configurable timeout, the viewport smoothly
// scrolls through the collection, reversing at top/bottom boundaries.
// Any user interaction immediately stops the autoscroll.
#include "attractmanager.h"
#include "loggingcategories.h"

#include <QApplication>

#include "applicationcontext.h"
#include "attracthelpers.h"
#include "collection/generalsettings.h"
#include "gridlayoutcalculator.h"
#include "igridlayoutscroll.h"
#include "iscrolldatasource.h"
#include "iselectionmanager.h"
#include "uiconstants/attract.h"

#include <QRandomGenerator>
#include <QScopeGuard>
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
AttractManager::AttractManager(QObject *parent) : QObject(parent) {
  m_idleTimer.setSingleShot(true);
  m_scrollTimer.setTimerType(Qt::PreciseTimer);
  m_bouncePauseTimer.setSingleShot(true);

  connect(&m_idleTimer, &QTimer::timeout, this, &AttractManager::onIdleTimeout);
  connect(&m_scrollTimer, &QTimer::timeout, this, &AttractManager::onScrollTick);
  connect(&m_bouncePauseTimer, &QTimer::timeout, this, &AttractManager::onBouncePauseFinished);
  connect(&m_advanceSelectionTimer, &QTimer::timeout, this,
          &AttractManager::onAdvanceSelectionTick);
}

AttractManager::~AttractManager() = default;

bool AttractManager::shuttingDown() const {
  return QApplication::closingDown() || (m_isShuttingDown && *m_isShuttingDown);
}

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
  return m_generalSettings && m_generalSettings->attract.attractModeEnabled;
}

void AttractManager::reloadSettings() {
  if (!m_generalSettings) {
    stopAttract();
    m_idleTimer.stop();
    return;
  }

  // Always keep the interval in sync so that enabling attract mode later
  // doesn't fire with a stale / zero interval.
  const int timeoutSec = qBound(UIConstants::Attract::MIN_IDLE_TIMEOUT_SEC,
                                m_generalSettings->attract.attractModeIdleTimeoutSec,
                                UIConstants::Attract::MAX_IDLE_TIMEOUT_SEC);
  m_idleTimer.setInterval(timeoutSec * 1000);

  // Sync the advance-selection interval too so a live settings change is
  // reflected without restarting attract mode.
  const int advanceSec = qBound(UIConstants::Attract::MIN_ADVANCE_INTERVAL_SEC,
                                m_generalSettings->attract.attractModeAdvanceSelectionIntervalSec,
                                UIConstants::Attract::MAX_ADVANCE_INTERVAL_SEC);
  m_advanceSelectionTimer.setInterval(advanceSec * 1000);

  if (!m_generalSettings->attract.attractModeEnabled) {
    stopAttract();
    m_idleTimer.stop();
    return;
  }

  // If attract is already running, honour a runtime toggle of the
  // sub-features (auto-scroll, advance-selection) without forcing a full
  // restart.
  if (m_attractActive) {
    if (m_generalSettings->attract.attractModeAutoScrollEnabled) {
      if (!m_scrollTimer.isActive() && !m_bouncePaused) {
        m_scrollTimer.start(UIConstants::Attract::SCROLL_TICK_INTERVAL_MS);
      }
    } else {
      m_scrollTimer.stop();
      m_bouncePauseTimer.stop();
      m_bouncePaused = false;
    }
    if (m_generalSettings->attract.attractModeAdvanceSelectionEnabled) {
      if (!m_advanceSelectionTimer.isActive()) {
        m_advanceSelectionTimer.start();
      }
    } else {
      m_advanceSelectionTimer.stop();
    }
    // If both sub-features were turned off mid-session, exit attract mode and
    // arm the idle countdown so it can re-enter once one is enabled again.
    if (!m_scrollTimer.isActive() && !m_advanceSelectionTimer.isActive()) {
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
    m_idleTimer.stop();
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
  if (shuttingDown()) {
    return;
  }
  // Re-sync interval from the live GeneralSettings pointer every time so that
  // changes made through the settings dialog take effect without requiring an
  // explicit reloadSettings() call.
  if (m_generalSettings) {
    const int timeoutSec = qBound(UIConstants::Attract::MIN_IDLE_TIMEOUT_SEC,
                                  m_generalSettings->attract.attractModeIdleTimeoutSec,
                                  UIConstants::Attract::MAX_IDLE_TIMEOUT_SEC);
    m_idleTimer.setInterval(timeoutSec * 1000);
  }
  m_idleTimer.start();
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
  if (shuttingDown()) {
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

  const bool wantScroll = m_generalSettings->attract.attractModeAutoScrollEnabled;
  const bool wantAdvance = m_generalSettings->attract.attractModeAdvanceSelectionEnabled;
  if (!wantScroll && !wantAdvance) {
    // Both sub-features off — nothing for attract to do. Re-arm the idle
    // timer so we re-evaluate next time the user goes idle.
    resetIdleTimer();
    return;
  }

  // In Horizontal view the long axis is X, so attract drives the horizontal
  // scrollbar; otherwise the vertical one.
  IGridLayoutScroll *scroll = m_ctx ? m_ctx->scrollGrid() : nullptr;
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
    m_scrollTimer.start(UIConstants::Attract::SCROLL_TICK_INTERVAL_MS);
  }
  startAdvanceSelectionTimerIfEnabled();

  qCDebug(lcAttractManager) << "Attract mode started";
  emit attractStarted();
}

void AttractManager::startAdvanceSelectionTimerIfEnabled() {
  if (!m_generalSettings || !m_generalSettings->attract.attractModeAdvanceSelectionEnabled) {
    return;
  }
  const int advanceSec = qBound(UIConstants::Attract::MIN_ADVANCE_INTERVAL_SEC,
                                m_generalSettings->attract.attractModeAdvanceSelectionIntervalSec,
                                UIConstants::Attract::MAX_ADVANCE_INTERVAL_SEC);
  m_advanceSelectionTimer.start(advanceSec * 1000);
}

void AttractManager::stopAttract() {
  if (!m_attractActive) {
    return;
  }

  m_attractActive = false;
  m_bouncePaused = false;
  m_scrollTimer.stop();
  m_bouncePauseTimer.stop();
  m_advanceSelectionTimer.stop();

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
  IGridLayoutScroll *scroll = m_ctx ? m_ctx->scrollGrid() : nullptr;
  const bool isHorizontal = scroll && scroll->getMetrics().isHorizontal;
  QScrollBar *bar = isHorizontal ? m_itemScrollArea->horizontalScrollBar()
                                 : m_itemScrollArea->verticalScrollBar();
  if (!bar) {
    stopAttract();
    return;
  }

  const double speed = m_generalSettings ? qBound(UIConstants::Attract::MIN_SCROLL_SPEED_PX,
                                                  m_generalSettings->attract.attractModeScrollSpeed,
                                                  UIConstants::Attract::MAX_SCROLL_SPEED_PX)
                                         : UIConstants::Attract::DEFAULT_SCROLL_SPEED_PX;

  const auto step = AttractHelpers::accumulateScrollDelta(m_scrollAccumulator, speed);
  m_scrollAccumulator = step.newAccumulator;
  if (step.delta == 0) {
    return;
  }

  const auto pos = AttractHelpers::nextScrollPosition(bar->value(), step.delta, m_scrollDirection,
                                                      bar->minimum(), bar->maximum());
  bar->setValue(pos.next);

  if (pos.hitMax || pos.hitMin) {
    m_scrollAccumulator = 0.0;
    m_bouncePaused = true;
    m_bouncePauseTimer.start(UIConstants::Attract::BOUNCE_PAUSE_MS);
    return;
  }

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
  if (shuttingDown()) {
    return;
  }
  if (!m_generalSettings || !m_generalSettings->attract.attractModeAdvanceSelectionEnabled) {
    m_advanceSelectionTimer.stop();
    return;
  }
  IScrollDataSource *scroll = m_ctx ? m_ctx->scrollData() : nullptr;
  ISelectionManager *selection = m_ctx ? m_ctx->selectionManager() : nullptr;
  if (!scroll || !selection) {
    return;
  }

  const int total = scroll->getTotalItems();
  if (total <= 0) {
    return;
  }

  const int current = selection->currentSelectedIndex();
  int next;
  if (m_generalSettings->attract.attractModeAdvanceSelectionRandom) {
    const auto range = AttractHelpers::randomAdvanceRange(current, total, m_scrollDirection);
    if (range.isEmpty()) {
      next = current; // Pathological state — falls through to the no-op return below.
    } else {
      next = range.rangeStart + QRandomGenerator::global()->bounded(range.size());
    }
  } else {
    next = AttractHelpers::linearAdvanceIndex(current, total);
  }
  if (next == current || next < 0) {
    return;
  }

  // Mark this selection change as attract-driven so the wired selectionChanged
  // → onActivityDetected handler skips it (otherwise attract would stop on its
  // own first tick).
  m_drivingSelection = true;
  // Cleared each tick so the invariant assertion below sees only THIS tick's
  // observation; the guard slot sets it back to true synchronously (see below).
  m_drivenSelectionObserved = false;
  // Reset via a scope guard: requestSelectIndex runs slots synchronously and
  // arbitrarily deep — if one throws or tears the manager down, a bare reset
  // would be skipped, leaving the flag stuck true and permanently suppressing
  // real-input detection (Kartend-77ay).
  //
  // INVARIANT (Kartend-b93at): the chain requestSelectIndex → selectItemByIndex
  // → SelectionManager::selectionChanged → the isDrivingSelection() guard in
  // InteractionManager::connectAttractManagerSignals must run as synchronous
  // (default Direct, same-thread) connections. If any link is switched to
  // Qt::QueuedConnection, selectionChanged fires AFTER this scope guard has
  // already cleared m_drivingSelection, the guard reads false, and attract mode
  // stops itself on its first tick.
  const auto guard = qScopeGuard([this]() { m_drivingSelection = false; });
  // Kartend-ic4h6: attract mode moving the selection is indistinguishable from
  // a spurious revert when you are looking at the screen rather than the code.
  // Say so explicitly, so one instrumented run separates "attract advanced" from
  // "something put the old selection back".
  if (lcPerfTrace().isDebugEnabled()) {
    qCDebug(lcPerfTrace) << "Selection ATTRACT-DRIVEN advance to index=" << next;
  }
  emit requestSelectIndex(next);

  // Enforce the b93at invariant in debug builds: if the selection actually moved
  // to `next`, its selectionChanged must have fired synchronously within the
  // emit above and the guard must have recorded it. If not, a Qt::QueuedConnection
  // deferred it past this scope — exactly the silent regression the invariant
  // above warns about. Gated on the move actually happening so a rejected/no-op
  // select can't false-positive. Q_ASSERT_X evaluates its condition in debug only.
  Q_ASSERT_X(selection->currentSelectedIndex() != next || m_drivenSelectionObserved,
             "AttractManager::onAdvanceSelectionTick",
             "attract-driven selectionChanged did not fire synchronously; a "
             "Qt::QueuedConnection broke the Kartend-b93at Direct chain");
}
