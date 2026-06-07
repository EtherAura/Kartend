#include <QRegularExpression>
#include <QRunnable>
#include <QSemaphore>
#include <QThreadPool>
#include <QtTest/QtTest>

#include "adaptivebatcher.h"
#include "threadpoolutils.h"

class TestThreadingUtils : public QObject {
  Q_OBJECT
private slots:
  // AdaptiveBatcher — pure timing -> batch-size adaptation.
  void batcher_zeroItems_leavesSizeUnchanged();
  void batcher_fastBatches_growTowardMax();
  void batcher_slowBatches_shrinkTowardMin();
  void batcher_respectsCustomBounds();
  void batcher_reset_restoresInitialState();
  void batcher_historyBoundedToConfig();

  // ThreadPoolUtils::shutdownWithBudget — the UAF-avoiding leak branch.
  void shutdown_nullPool_returnsTrue();
  void shutdown_drainsIdlePool_deletesAndNulls();
  void shutdown_budgetExceeded_warnsAndNullsRatherThanDeleting();
};

// ── AdaptiveBatcher ──────────────────────────────────────────────────────────

void TestThreadingUtils::batcher_zeroItems_leavesSizeUnchanged() {
  AdaptiveBatcher b; // defaults(): initial 10
  QCOMPARE(b.currentBatchSize(), 10);
  QCOMPARE(b.observeBatch(0, 100), 10); // non-positive item counts are no-ops
  QCOMPARE(b.observeBatch(-5, 100), 10);
  QCOMPARE(b.currentBatchSize(), 10);
}

void TestThreadingUtils::batcher_fastBatches_growTowardMax() {
  AdaptiveBatcher b; // target 50ms, max 50
  // ~1ms/item -> optimal 50; grows at most ~50%/batch, never shrinks, never
  // exceeds max, and converges to the ceiling.
  int prev = b.currentBatchSize();
  for (int i = 0; i < 20; ++i) {
    const int now = b.observeBatch(prev, prev); // elapsed == items => 1ms/item
    QVERIFY2(now >= prev, "a fast batch must not shrink the batch size");
    QVERIFY2(now <= 50, "batch size must never exceed maxBatchSize");
    prev = now;
  }
  QCOMPARE(b.currentBatchSize(), 50);
}

void TestThreadingUtils::batcher_slowBatches_shrinkTowardMin() {
  AdaptiveBatcher b; // target 50ms, min 2
  int prev = b.currentBatchSize();
  for (int i = 0; i < 20; ++i) {
    const int now = b.observeBatch(prev, static_cast<qint64>(prev) * 50); // 50ms/item
    QVERIFY2(now <= prev, "a slow batch must not grow the batch size");
    QVERIFY2(now >= 2, "batch size must never drop below minBatchSize");
    prev = now;
  }
  QCOMPARE(b.currentBatchSize(), 2);
}

void TestThreadingUtils::batcher_respectsCustomBounds() {
  AdaptiveBatcher b(AdaptiveBatcher::Config{20, 5, 30, 50, 0.3, 10}); // min 5, max 30
  for (int i = 0; i < 20; ++i) {
    b.observeBatch(b.currentBatchSize(), 1); // tiny time/item -> wants huge -> clamps to max
  }
  QCOMPARE(b.currentBatchSize(), 30);
  for (int i = 0; i < 20; ++i) {
    b.observeBatch(b.currentBatchSize(),
                   static_cast<qint64>(b.currentBatchSize()) * 1000); // very slow
  }
  QCOMPARE(b.currentBatchSize(), 5);
}

void TestThreadingUtils::batcher_reset_restoresInitialState() {
  AdaptiveBatcher b;
  b.observeBatch(10, 10);
  QVERIFY(b.currentBatchSize() != 10 || b.avgTimePerItem() != 0.0); // state moved
  b.reset();
  QCOMPARE(b.currentBatchSize(), 10);
  QCOMPARE(b.avgTimePerItem(), 0.0);
  QCOMPARE(b.stats().samplesCollected, 0);
}

void TestThreadingUtils::batcher_historyBoundedToConfig() {
  AdaptiveBatcher b; // historySize 10
  for (int i = 0; i < 25; ++i) {
    b.observeBatch(5, 10);
  }
  QCOMPARE(b.stats().samplesCollected, 10);
}

// ── ThreadPoolUtils::shutdownWithBudget ──────────────────────────────────────

void TestThreadingUtils::shutdown_nullPool_returnsTrue() {
  QThreadPool *pool = nullptr;
  QVERIFY(ThreadPoolUtils::shutdownWithBudget(pool, 100));
  QCOMPARE(pool, nullptr);
}

void TestThreadingUtils::shutdown_drainsIdlePool_deletesAndNulls() {
  auto *pool = new QThreadPool;
  QSemaphore done;
  pool->start(QRunnable::create([&done]() { done.release(); }));
  QVERIFY2(done.tryAcquire(1, 5000), "the task should have run");
  QThreadPool *p = pool;
  // Ample budget: the now-idle pool drains, is deleted, and the pointer nulled.
  QVERIFY(ThreadPoolUtils::shutdownWithBudget(p, 5000));
  QCOMPARE(p, nullptr);
}

void TestThreadingUtils::shutdown_budgetExceeded_warnsAndNullsRatherThanDeleting() {
  auto *pool = new QThreadPool;
  pool->setMaxThreadCount(1);
  QSemaphore started;
  QSemaphore block;
  pool->start(QRunnable::create([&started, &block]() {
    started.release();
    block.acquire(); // hold the worker busy well past the drain budget
  }));
  QVERIFY2(started.tryAcquire(1, 5000), "the blocking task should be running, not just queued");

  QThreadPool *leaked = pool; // keep a handle to clean up the intentional leak
  QThreadPool *p = pool;
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression("did not drain within"));
  // Budget far too small for the blocked task: must report failure, leak the
  // pool (no delete), and null the pointer so late access fails fast.
  QVERIFY(!ThreadPoolUtils::shutdownWithBudget(p, 50));
  QCOMPARE(p, nullptr);

  // Release the task and reclaim the deliberately-leaked pool so the sanitizer
  // build stays clean — the production leak is the safe choice, but the test
  // owns the pool and can tear it down once the task is no longer in flight.
  block.release();
  leaked->waitForDone();
  delete leaked;
}

QTEST_GUILESS_MAIN(TestThreadingUtils)
#include "test_threadingutils.moc"
