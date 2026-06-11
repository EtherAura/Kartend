// Regression test for Kartend-t2my8: requestCancel() must NOT drop queued
// runnables via QThreadPool::clear(). Scan accounting increments
// ScanCompletionQueue::inFlight at enqueue time and decrements only at the
// end of DirectoryScanTask::run(); a cleared (never-run) task strands the
// count and wedges the post-cancel drain loop in scanParallel /
// stageRecursiveScan forever — surfacing as the Kartend-8fnlp teardown
// abort. The contract asserted here: every runnable handed to start()
// eventually runs, even when requestCancel() lands while it is still queued.

#include <QElapsedTimer>
#include <QMutex>
#include <QRunnable>
#include <QSemaphore>
#include <QTest>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <memory>

#include "scanworkcontroller.h"

namespace {

/// Counts down a completion latch when run; optionally blocks on a gate
/// semaphore first so the pool's threads can be held busy deterministically.
class CountingTask final : public QRunnable {
public:
  CountingTask(std::atomic_int &ranCount, QSemaphore *gate) : m_ranCount(ranCount), m_gate(gate) {
    setAutoDelete(true);
  }

  void run() override {
    if (m_gate != nullptr) {
      m_gate->acquire();
    }
    m_ranCount.fetch_add(1, std::memory_order_acq_rel);
  }

private:
  std::atomic_int &m_ranCount;
  QSemaphore *m_gate = nullptr;
};

} // namespace

class TestScanWorkController : public QObject {
  Q_OBJECT
private slots:
  void testQueuedTasksStillRunAfterCancel();
  void testCancelFlipsCurrentTokenAndResetIsolates();
};

void TestScanWorkController::testQueuedTasksStillRunAfterCancel() {
  ScanWorkController controller;
  const int threads = controller.maxThreadCount();
  QVERIFY(threads >= 1);

  std::atomic_int ranCount{0};
  QSemaphore gate; // 0 permits: blockers hold every pool thread.

  // Occupy every pool thread with a gated task.
  for (int i = 0; i < threads; ++i) {
    controller.start(new CountingTask(ranCount, &gate));
  }

  // Queue more tasks than the pool has threads; these sit in the pool's
  // queue exactly like DirectoryScanTasks counted into inFlight at enqueue.
  const int queued = threads * 2;
  for (int i = 0; i < queued; ++i) {
    controller.start(new CountingTask(ranCount, nullptr));
  }

  // Cancel while the queued tasks have not started. Under the pre-fix
  // behaviour (m_pool->clear() inside requestCancel) the queued tasks are
  // deleted without running and the total below comes up short.
  controller.requestCancel();
  QVERIFY(controller.isCancelled());

  // Release the blockers; everything must eventually run.
  gate.release(threads);

  const int expected = threads + queued;
  QTRY_COMPARE_WITH_TIMEOUT(ranCount.load(std::memory_order_acquire), expected, 10000);
}

void TestScanWorkController::testCancelFlipsCurrentTokenAndResetIsolates() {
  ScanWorkController controller;

  const auto firstToken = controller.token();
  QVERIFY(firstToken != nullptr);
  QVERIFY(!controller.isCancelled());

  controller.requestCancel();
  QVERIFY(controller.isCancelled());
  QVERIFY(firstToken->load(std::memory_order_acquire));

  // reset() swaps in a fresh token and permanently cancels the old one, so
  // a slow worker holding the old shared_ptr keeps observing "cancelled".
  controller.reset();
  QVERIFY(!controller.isCancelled());
  QVERIFY(firstToken->load(std::memory_order_acquire));

  const auto secondToken = controller.token();
  QVERIFY(secondToken != nullptr);
  QVERIFY(secondToken != firstToken);
  QVERIFY(!secondToken->load(std::memory_order_acquire));
}

QTEST_GUILESS_MAIN(TestScanWorkController)

#include "test_scanworkcontroller.moc"
