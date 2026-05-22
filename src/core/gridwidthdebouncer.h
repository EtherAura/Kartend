#ifndef GRIDWIDTHDEBOUNCER_H
#define GRIDWIDTHDEBOUNCER_H

#include <functional>
#include <QObject>
#include <QPointer>

namespace TimerUtils {
class DebouncedTimer;
}

/// Three-stage debounced pipeline for menu-driven grid-width adjustments
/// (the Ctrl++ / Ctrl+- shortcuts that step the active collection's
/// gridWidth up/down).
///
/// Stages, each fronted by an independent DebouncedTimer:
///   1. **save** — persists m_collections to disk (LONG delay). Coalesces
///      a burst of width changes into one settings write.
///   2. **precalc** — runs the expensive pre-calculate layout + virtual
///      view rebuild (LONG delay). Bumps the active generation counter so
///      the finalize stage can detect staleness.
///   3. **finalize** — applies the final virtual-view update, refreshes
///      artwork for the new viewport, and re-centers the horizontal
///      scrollbar (MEDIUM delay). Triggered by the precalc stage. If a
///      newer precalc has incremented `pending` past `active` between
///      precalc completion and finalize firing, this stage bails because
///      another finalize will follow.
///
/// Callers wire stage-specific callbacks via wire() — typically the
/// callbacks include their own m_isShuttingDown / QApplication::closingDown
/// guards.
class GridWidthDebouncer : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(GridWidthDebouncer)

public:
  using StageCallback = std::function<void()>;

  explicit GridWidthDebouncer(QObject *parent = nullptr);

  /// Install the per-stage callbacks. Safe to call once after construction.
  /// @p onPrecalc runs preCalculateLayout + forceVirtualViewUpdate.
  /// @p onFinalize runs updateVirtualView + updateViewportArtwork +
  /// centerHorizontalScrollbar.
  void wire(StageCallback onSave, StageCallback onPrecalc, StageCallback onFinalize);

  /// Schedule the persistence save stage.
  void triggerSave();

  /// Increment the pending generation and schedule the precalc stage.
  /// The finalize stage uses the generation to detect staleness.
  void triggerPrecalc();

  /// True when a save callback is queued but hasn't fired yet — used by
  /// closeEvent to decide whether to flush.
  [[nodiscard]] bool hasPendingSave() const;

  /// Force the queued save to fire immediately. Used during shutdown so the
  /// user's last grid-width adjustment hits disk.
  void flushPendingSave();

private:
  void onPrecalcStage();
  void onFinalizeStage();

  QPointer<TimerUtils::DebouncedTimer> m_saveTimer;
  QPointer<TimerUtils::DebouncedTimer> m_precalcTimer;
  QPointer<TimerUtils::DebouncedTimer> m_finalizeTimer;

  StageCallback m_onSave;
  StageCallback m_onPrecalc;
  StageCallback m_onFinalize;

  int m_pendingGeneration = 0;
  int m_activeGeneration = 0;
};

#endif // GRIDWIDTHDEBOUNCER_H
