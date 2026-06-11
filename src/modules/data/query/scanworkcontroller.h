#ifndef SCANWORKCONTROLLER_H
#define SCANWORKCONTROLLER_H

#include <atomic>
#include <memory>
#include <QMutex>

class QRunnable;
class QThreadPool;

/// Owns QueryManager's scan thread pool and the per-scan cooperative
/// cancellation token. Scans dispatch directory walks onto the pool; tasks
/// capture the cancellation token by shared_ptr value so they can observe
/// the flag even after the controller resets to a fresh token mid-scan.
///
/// requestCancel() flips the current token; every task — queued or
/// in-flight — observes the flag and returns early. Queued tasks are
/// deliberately NOT dropped via QThreadPool::clear(): scan accounting
/// (ScanCompletionQueue::inFlight) is incremented at enqueue time and only
/// decremented at the end of DirectoryScanTask::run(), so a cleared task
/// would strand the count and wedge the post-cancel drain loop forever
/// (Kartend-t2my8). Letting cancelled tasks run costs one fast token check
/// each and keeps the handshake balanced. reset() swaps in a brand-new
/// token, leaving the OLD token permanently cancelled so a slow worker
/// won't "resurrect" when the same atomic gets flipped back to false.
///
/// Threading: the cancellation-token shared_ptr is read/written across
/// threads (requestCancel on the GUI thread, reset/token/start on the
/// scan worker), so m_token is guarded by m_tokenMutex. The atomic_bool
/// pointed to by the shared_ptr is itself lock-free; the mutex protects
/// only the shared_ptr's control block.
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

  /// Maximum thread count of the underlying pool (>= 1).
  [[nodiscard]] int maxThreadCount() const;

  /// Set the cancellation flag. All tasks (queued and in-flight) observe it
  /// via their captured shared_ptr and fast-exit; the queue is intentionally
  /// not cleared (see class comment).
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
  [[nodiscard]] std::shared_ptr<std::atomic_bool> token() const;

private:
  mutable QMutex m_tokenMutex;
  std::shared_ptr<std::atomic_bool> m_token;
  // Raw pointer; intentionally leaked at shutdown (~QThreadPool blocks).
  QThreadPool *m_pool;
};

#endif // SCANWORKCONTROLLER_H
