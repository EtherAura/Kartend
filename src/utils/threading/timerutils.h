#ifndef TIMERUTILS_H
#define TIMERUTILS_H

#include <functional>
#include <QEventLoop>
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
 * Thread-affinity: NOT thread-safe. trigger() / cancel() drive an internal
 * QTimer, which Qt requires to be touched only from the thread
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

  void trigger(); // Schedule a debounced trigger
  void cancel();  // Cancel any pending trigger
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

/**
 * @brief Waits, up to a deadline, for an asynchronous operation of the calling
 * thread to finish.
 *
 * Connect the operation's completion signal to finish(), start the operation,
 * then call run(): it processes the thread's events until finish() has been
 * called or the deadline has passed. One BoundedWait serves one operation.
 *
 *   TimerUtils::BoundedWait wait;
 *   QObject::connect(&job, &Job::finished, &wait, &TimerUtils::BoundedWait::finish);
 *   job.start();
 *   if (!wait.run(5000)) {
 *     // the operation did not finish within five seconds
 *   }
 *
 * Unlike a bare nested QEventLoop, run() also works while the application is
 * quitting (Kartend-87x93). QCoreApplication::quit() and exit() mark the
 * thread as quitting, and until the application's own exec() has returned,
 * QEventLoop::exec() refuses to run: it returns -1 at once. Code that
 * continues after a modal dialog which the quit has just closed is in exactly
 * that state. A wait built on exec() alone "times out" there in zero
 * milliseconds, although the operation was never given a chance. run()
 * notices that the loop did not run, or was ended early by a quit, and
 * processes the events itself until the operation finishes or the same
 * deadline passes.
 *
 * Thread-affinity: NOT thread-safe. Create it, call finish() and call run()
 * on one thread.
 *
 * Re-entrancy: run() dispatches every pending event of the thread, user input
 * included, as a nested QEventLoop does. Use it only where that is acceptable.
 */
class BoundedWait : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(BoundedWait)
public:
  explicit BoundedWait(QObject *parent = nullptr);
  ~BoundedWait() override;

  /// Marks the awaited operation as finished and ends a run() in progress.
  void finish();

  /// Processes events until finish() was called or @p timeoutMs has passed.
  /// @return true if the operation finished, false if the deadline came first.
  [[nodiscard]] bool run(int timeoutMs);

  [[nodiscard]] bool isFinished() const { return m_finished; }

private:
  QEventLoop m_loop;
  bool m_finished = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper functions for timer management
// ─────────────────────────────────────────────────────────────────────────────

void stopTimers(const QList<QTimer *> &timers);
void disconnectTimers(const QList<QTimer *> &timers);
void stopAndDisconnectTimers(const QList<QTimer *> &timers);

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

} // namespace TimerUtils

#endif