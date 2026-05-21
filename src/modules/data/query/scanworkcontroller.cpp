#include "scanworkcontroller.h"

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
  // Abandon the thread pool without waiting — process is exiting anyway.
  // ~QThreadPool() blocks; let the OS clean up. requestCancel() may still
  // be called from another thread (DatabaseManager teardown does this from
  // the main thread while QueryManager's worker is still draining), so the
  // pool pointer stays alive — just orphaned.
  if (m_pool) {
    m_pool->clear();
  }
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
