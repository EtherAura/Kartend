#include "scanworkcontroller.h"

#include "uiconstants.h"

#include <algorithm>
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
  if (m_token) {
    m_token->store(true, std::memory_order_release);
  }
  // Drain any tasks waiting in the queue; in-flight tasks check the
  // cancellation token themselves.
  if (m_pool) {
    m_pool->clear();
  }
}

bool ScanWorkController::isCancelled() const {
  return m_token && m_token->load(std::memory_order_acquire);
}

void ScanWorkController::reset() {
  m_token = std::make_shared<std::atomic_bool>(false);
}
