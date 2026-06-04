#ifndef TIMERUTILS_H
#define TIMERUTILS_H

#include <functional>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

namespace TimerUtils {

/**
 * @brief A generic debounced timer that coalesces rapid calls into single
 * executions.
 *
 * Thread-affinity: NOT thread-safe. trigger() / triggerImmediate() / cancel()
 * drive an internal QTimer, which Qt requires to be touched only from the thread
 * that owns it. These are UI-thread coalescers — call them from the
 * DebouncedTimer's own (typically the GUI) thread; a cross-thread call is
 * undefined behaviour. Marshal through a QueuedConnection if a worker thread
 * needs to trigger one (Kartend-3pi4).
 *
 * Usage:
 *   DebouncedTimer timer(100, this);  // 100ms debounce interval
 *   connect(&timer, &DebouncedTimer::triggered, this,
 * &MyClass::handleDebounced); timer.trigger();  // Call many times, only fires
 * once after 100ms of inactivity
 */
class DebouncedTimer : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DebouncedTimer)
public:
  explicit DebouncedTimer(int intervalMs, QObject *parent = nullptr);
  ~DebouncedTimer() override;

  void trigger();          // Schedule a debounced trigger
  void triggerImmediate(); // Trigger immediately, resetting any pending
  void cancel();           // Cancel any pending trigger
  [[nodiscard]] bool isPending() const;
  [[nodiscard]] int interval() const;
  void setInterval(int intervalMs);

signals:
  void triggered();

private:
  QTimer *m_timer = nullptr;
};

/**
 * @brief Coordinator for layout and viewport update debouncing.
 *
 * Provides centralized debounced timers for common UI update patterns.
 *
 * Thread-affinity: NOT thread-safe — it owns DebouncedTimers, so drive it only
 * from its own (GUI) thread; cross-thread scheduling is undefined behaviour
 * (Kartend-3pi4).
 */
class Coordinator : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Coordinator)
public:
  explicit Coordinator(QObject *parent = nullptr);
  ~Coordinator() override;
  void scheduleLayoutUpdate();
  void scheduleViewportUpdate();
  void stopAllTimers();

signals:
  void layoutUpdateRequested();
  void viewportUpdateRequested();

private slots:
  void onLayoutTimerTimeout();
  void onViewportTimerTimeout();

private:
  QTimer *m_layoutTimer;
  QTimer *m_viewportTimer;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper functions for timer management
// ─────────────────────────────────────────────────────────────────────────────

void stopTimers(const QList<QTimer *> &timers);
void disconnectTimers(const QList<QTimer *> &timers);
void stopAndDisconnectTimers(const QList<QTimer *> &timers);
void deleteLaterTimer(QTimer *&timer);

/**
 * @brief Execute a callback after a delay, with object lifetime guard.
 *
 * Uses QPointer to ensure the target object is still valid when the
 * callback executes. Safer than raw QTimer::singleShot for member lambdas.
 *
 * @param delayMs Delay in milliseconds (0 = next event loop iteration)
 * @param target The QObject to guard (typically 'this')
 * @param callback The function to execute if target is still valid
 */
template <typename T, typename Func>
void singleShotGuarded(int delayMs, T *target, Func &&callback) {
  QPointer<T> guard(target);
  // Delay is caller-supplied — this helper exists purely to make the
  // QPointer lifetime guard the default, not to choose the interval itself.
  QTimer::singleShot(delayMs, target, [guard, cb = std::forward<Func>(callback)]() {
    if (guard) {
      cb();
    }
  });
}

/**
 * @brief Execute a callback on next event loop iteration, with object lifetime
 * guard.
 *
 * Convenience overload for the common case of deferring to next event loop.
 */
template <typename T, typename Func> void deferGuarded(T *target, Func &&callback) {
  singleShotGuarded(0, target, std::forward<Func>(callback));
}

} // namespace TimerUtils

#endif