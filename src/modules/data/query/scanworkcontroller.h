#ifndef SCANWORKCONTROLLER_H
#define SCANWORKCONTROLLER_H

#include <atomic>
#include <memory>

class QRunnable;
class QThreadPool;

/// Owns QueryManager's scan thread pool and the per-scan cooperative
/// cancellation token. Scans dispatch directory walks onto the pool; tasks
/// capture the cancellation token by shared_ptr value so they can observe
/// the flag even after the controller resets to a fresh token mid-scan.
///
/// requestCancel() flips the current token AND drains the pool's queue —
/// any task that hasn't started yet is dropped, any in-flight task observes
/// the flag and returns early. reset() swaps in a brand-new token, leaving
/// the OLD token permanently cancelled so a slow worker won't "resurrect"
/// when the same atomic gets flipped back to false.
///
/// Destructor matches the existing QueryManager policy: abandon the pool
/// rather than ~QThreadPool's blocking wait, so DatabaseManager teardown
/// doesn't stall on a long-running scan. The OS reclaims the leaked pool.
class ScanWorkController {
public:
  ScanWorkController();
  ~ScanWorkController();

  ScanWorkController(const ScanWorkController &) = delete;
  ScanWorkController &operator=(const ScanWorkController &) = delete;

  /// Schedule @p runnable on the pool. Pass-through wrapper around
  /// QThreadPool::start so callers don't need to know whether the pool is
  /// still alive.
  void start(QRunnable *runnable);

  /// Drop everything pending in the queue. Used by requestCancel() and by
  /// the deferred-cleanup hook in QueryManager's dtor.
  void clearQueue();

  /// Maximum thread count of the underlying pool (>= 1).
  [[nodiscard]] int maxThreadCount() const;

  /// Set the cancellation flag and drain pending tasks. In-flight tasks
  /// observe the flag via captured shared_ptr.
  void requestCancel();

  /// True iff the current token is set. Worker tasks check this via the
  /// shared_ptr they captured at scheduling time.
  [[nodiscard]] bool isCancelled() const;

  /// Swap in a fresh cancellation token. Old token stays at "cancelled"
  /// forever so any still-in-flight tasks that captured it keep bailing.
  void reset();

  /// Live cancellation token — captured by long-running tasks (directory
  /// walks, batch operations) so they can poll without taking a reference
  /// to this controller.
  [[nodiscard]] std::shared_ptr<std::atomic_bool> token() const { return m_token; }

private:
  std::shared_ptr<std::atomic_bool> m_token;
  // Raw pointer; intentionally leaked at shutdown (~QThreadPool blocks).
  QThreadPool *m_pool;
};

#endif // SCANWORKCONTROLLER_H
