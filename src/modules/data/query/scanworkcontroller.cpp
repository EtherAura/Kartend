#include "scanworkcontroller.h"

#include "threadpoolutils.h"
#include "uiconstants/concurrency.h"

#include <algorithm>
#include <QMutexLocker>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

ScanWorkController::ScanWorkController() : m_token(std::make_shared<std::atomic_bool>(false)) {
  const int idealThreads = QThread::idealThreadCount();
  const int base = idealThreads > 0 ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
                                    : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_pool = new QThreadPool();
  m_pool->setMaxThreadCount(std::clamp(base, UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                                       UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));
}

ScanWorkController::~ScanWorkController() {
  // The owning DatabaseManager teardown flips our cancel flag via a synchronous
  // cross-thread requestCancelScan() that RETURNS before it quit()s/wait()s the
  // scan thread (databasemanager.cpp:188, Kartend-mkm4u). So by the time this
  // dtor runs on the worker thread the token is already set, in-flight tasks are
  // exiting, and — because that requestCancelScan() completed before the
  // quit()/wait() which triggers this destruction — no requestCancel() runs
  // concurrently with the drain below. We can therefore reclaim the pool with a
  // bounded wait instead of leaking it on every destruction (the old behaviour):
  // that matters for the test harness, which builds/destroys thousands of
  // DatabaseManagers per QApplication (two ScanWorkControllers each) and would
  // otherwise accumulate leaked idle pools. The requestCancel() here is
  // idempotent and also covers any direct-construction path with no external
  // cancel. shutdownWithBudget never use-after-frees (it leaks the pool, with a
  // warning, and nulls m_pool on the rare timeout) so a late m_pool access still
  // fails safe on the `if (m_pool)` guards (Kartend-ppl9r).
  constexpr int kScanPoolDrainMs = 2000;
  requestCancel();
  ThreadPoolUtils::shutdownWithBudget(m_pool, kScanPoolDrainMs);
}

void ScanWorkController::start(QRunnable *runnable) {
  if (!m_pool || !runnable) {
    return;
  }
  m_pool->start(runnable);
}

void ScanWorkController::clearQueue() {
  if (m_pool) {
    m_pool->clear();
  }
}

int ScanWorkController::maxThreadCount() const {
  return m_pool ? std::max(1, m_pool->maxThreadCount()) : 1;
}

void ScanWorkController::requestCancel() {
  // Snapshot the shared_ptr under the mutex, then operate on the
  // atomic_bool without holding the lock — the atomic itself is lock-free
  // and the shared_ptr keeps the control block alive across reset().
  std::shared_ptr<std::atomic_bool> tok;
  {
    QMutexLocker locker(&m_tokenMutex);
    tok = m_token;
  }
  if (tok) {
    tok->store(true, std::memory_order_release);
  }
  // Drain any tasks waiting in the queue; in-flight tasks check the
  // cancellation token themselves. QThreadPool::clear is thread-safe.
  if (m_pool) {
    m_pool->clear();
  }
}

bool ScanWorkController::isCancelled() const {
  std::shared_ptr<std::atomic_bool> tok;
  {
    QMutexLocker locker(&m_tokenMutex);
    tok = m_token;
  }
  return tok && tok->load(std::memory_order_acquire);
}

void ScanWorkController::reset() {
  // Permanently cancel the OLD token before swapping in a fresh one, so
  // any worker still holding a shared_ptr to it observes isCancelled()
  // and bails out. Matches ArtworkLoadDispatcher::cancelAll().
  QMutexLocker locker(&m_tokenMutex);
  if (m_token) {
    m_token->store(true, std::memory_order_release);
  }
  m_token = std::make_shared<std::atomic_bool>(false);
}

std::shared_ptr<std::atomic_bool> ScanWorkController::token() const {
  QMutexLocker locker(&m_tokenMutex);
  return m_token;
}
