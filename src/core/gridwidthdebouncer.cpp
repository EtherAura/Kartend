#include "gridwidthdebouncer.h"

#include "timerutils.h"
#include "uiconstants/timing.h"

GridWidthDebouncer::GridWidthDebouncer(QObject *parent) : QObject(parent) {
  m_saveTimer = new TimerUtils::DebouncedTimer(UIConstants::Timing::LONG_DELAY_MS, this);
  m_precalcTimer = new TimerUtils::DebouncedTimer(UIConstants::Timing::LONG_DELAY_MS, this);
  m_finalizeTimer = new TimerUtils::DebouncedTimer(UIConstants::Timing::MEDIUM_DELAY_MS, this);

  connect(m_saveTimer, &TimerUtils::DebouncedTimer::triggered, this, [this]() {
    if (m_onSave) {
      m_onSave();
    }
  });
  connect(m_precalcTimer, &TimerUtils::DebouncedTimer::triggered, this,
          &GridWidthDebouncer::onPrecalcStage);
  connect(m_finalizeTimer, &TimerUtils::DebouncedTimer::triggered, this,
          &GridWidthDebouncer::onFinalizeStage);
}

void GridWidthDebouncer::wire(StageCallback onSave, StageCallback onPrecalc,
                              StageCallback onFinalize) {
  m_onSave = std::move(onSave);
  m_onPrecalc = std::move(onPrecalc);
  m_onFinalize = std::move(onFinalize);
}

void GridWidthDebouncer::triggerSave() {
  m_saveTimer->trigger();
}

void GridWidthDebouncer::triggerPrecalc() {
  ++m_pendingGeneration;
  m_precalcTimer->trigger();
}

bool GridWidthDebouncer::hasPendingSave() const {
  return m_saveTimer && m_saveTimer->isPending();
}

void GridWidthDebouncer::flushPendingSave() {
  if (m_saveTimer) {
    m_saveTimer->triggerImmediate();
  }
}

void GridWidthDebouncer::onPrecalcStage() {
  // Mark this generation as the one whose finalize stage we're queuing. If
  // another adjustment increments pending before finalize fires, finalize
  // will bail — a later precalc round will queue a fresh finalize for the
  // newer generation.
  m_activeGeneration = m_pendingGeneration;
  if (m_onPrecalc) {
    m_onPrecalc();
  }
  m_finalizeTimer->trigger();
}

void GridWidthDebouncer::onFinalizeStage() {
  if (m_activeGeneration != m_pendingGeneration) {
    return;
  }
  if (m_onFinalize) {
    m_onFinalize();
  }
}
