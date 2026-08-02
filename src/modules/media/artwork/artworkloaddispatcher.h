#ifndef ARTWORKLOADDISPATCHER_H
#define ARTWORKLOADDISPATCHER_H

#include "artworkmanager.h"

#include <atomic>
#include <functional>
#include <memory>
#include <QDeadlineTimer>
#include <QFuture>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

class ArtworkDispatcherCacheHandle;
class ICacheManager;
class QThreadPool;

/// Async batch dispatcher for the artwork pipeline. Owns the dedicated
/// thread pool, the in-flight QFuture set, and the cooperative cancellation
/// generation counter. Handles the QtConcurrent worker decode/disk-cache
/// pass; results are delivered to caller-supplied handlers on the main
/// thread.
///
/// Threading invariants:
///   - Worker tasks capture no raw pointers to externally-owned objects:
///     a shared_ptr to the cache handle (whose inner ICacheManager* the
///     destructor invalidates — Kartend-xoftg), a shared_ptr to the
///     current-generation atomic, the generation they were dispatched
///     at, and the input batch. They never capture the dispatcher or
///     any host raw — completion is posted to the main thread via
///     QCoreApplication::instance() as receiver, and the handler is
///     invoked there. This satisfies the threadpoolutils.h
///     abandon-on-timeout contract: a task abandoned by the bounded
///     pool drain only ever touches shared state it co-owns.
///   - cancelAll() atomically increments the generation counter. New
///     dispatches read the current generation at dispatch time and
///     capture it; in-flight tasks compare `counter->load() !=
///     capturedGeneration` and bail when they no longer match. The
///     counter approach replaces a prior token-swap design that had a
///     50ms race window where freshly-dispatched batches could capture
///     a still-cancelled token and stall (Kartend-uxo0).
///   - The destructor increments the generation (cancelling everything
///     in-flight) and drains the pool with a bounded budget so no
///     callback can fire after destruction.
class ArtworkLoadDispatcher : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ArtworkLoadDispatcher)

public:
  using BatchHandler = std::function<void(const QList<ArtworkInfo::Result> &results,
                                          int requestedCount, qint64 elapsedMs, bool highPriority)>;
  using PrecacheBatchHandler = std::function<void(const QStringList &requestedPaths,
                                                  const QList<ArtworkPrecacheResult> &results,
                                                  int requestedCount, qint64 elapsedMs)>;

  explicit ArtworkLoadDispatcher(ICacheManager *cacheManager, QObject *parent = nullptr);
  ~ArtworkLoadDispatcher() override;

  /// Kartend-davi: ArtworkManager constructs the dispatcher before the
  /// app context is wired (so cancellation paths stay non-null even before
  /// setupReferences runs); the cache pointer is then bound here once
  /// ctx becomes available. Rebinding is startup wiring — it must not race
  /// in-flight dispatches that hold the previous pointer.
  void setCacheManager(ICacheManager *cacheManager);

  /// Dispatch a UI-batch decode. @p onComplete runs on the main thread once
  /// every result is decoded (or the batch is cancelled / app is closing).
  void dispatchBatch(QList<ArtworkInfo> batch, bool highPriority, BatchHandler onComplete);

  /// Dispatch a precache decode (silent-load path). @p onComplete runs on
  /// the main thread.
  void dispatchPrecacheBatch(QStringList paths, PrecacheBatchHandler onComplete);

  /// Atomically increment the cancellation generation. All in-flight tasks
  /// captured a prior generation and will observe the mismatch on their
  /// next check; future dispatches read the new generation and proceed
  /// without delay. Safe to call repeatedly; back-to-back invocations cost
  /// O(1) and don't introduce any race window.
  void cancelAll();

  /// Number of currently-running futures. Used by silent-load throttle.
  [[nodiscard]] int runningFutureCount() const;

  /// Standalone teardown budgets: the bounded pool drain and the cache-handle
  /// quiesce that follows it. The destructor's shutdown() call uses their sum,
  /// preserving the pre-shared-deadline behavior for a dispatcher torn down
  /// outside the ArtworkManager destructor chain.
  static constexpr int kPoolDrainBudgetMs = 2000;
  static constexpr int kCacheQuiesceBudgetMs = 500;

  /// Bounded teardown: bump the cancellation generation, drain the worker
  /// pool, cut workers off from the disk cache, and cancel tracked futures.
  /// Each stage waits at most min(its own budget, @p deadline's remaining
  /// time), so a caller tearing down several bounded stages (ArtworkManager's
  /// destructor) can thread ONE shared deadline through the whole chain
  /// instead of letting sequential waits stack against the same stall.
  /// Idempotent — the destructor calls it with the full standalone budgets,
  /// which no-ops when a caller already shut the dispatcher down. After
  /// shutdown the pool pointer is null, so new dispatches are rejected.
  void shutdown(QDeadlineTimer deadline);

private:
  void pruneFinishedFutures();

  /// Shared forwarding surface for worker disk-cache reads (Kartend-xoftg).
  /// Workers co-own it via shared_ptr capture; the destructor invalidates
  /// the inner ICacheManager* before CacheManager can be destroyed later in
  /// ApplicationManager teardown, so a task abandoned by the bounded pool
  /// drain makes a guarded no-op call instead of dereferencing a freed
  /// cache. See the class definition in artworkloaddispatcher.cpp.
  std::shared_ptr<ArtworkDispatcherCacheHandle> m_cacheHandle;
  QThreadPool *m_threadPool = nullptr;
  mutable QMutex m_futureMutex;
  QList<QFuture<void>> m_futures;
  /// Monotonic cancellation generation. shared_ptr so in-flight tasks
  /// can keep observing the counter even if the dispatcher is destroyed
  /// (the destructor increments the counter to signal cancellation, then
  /// drains the pool; any task that outlives the drain budget bails on
  /// its next check because the counter no longer matches its captured
  /// value).
  std::shared_ptr<std::atomic<quint64>> m_currentGeneration;
  /// Kartend-wztmg: longest edge (device px) the most recent tile batch
  /// decided it needed. Batches derive this per item from the widget's
  /// measured label size; precache, which runs ahead of the viewport and has
  /// no widget to measure, reads it so its (path-keyed, therefore shared)
  /// cache entries match what the tiles will ask for. 0 until the first batch
  /// runs, which means "fall back to the flat BOX_SIZE".
  ///
  /// shared_ptr for the same reason as m_currentGeneration: an in-flight
  /// worker may outlive the dispatcher's pool drain and still touch it.
  std::shared_ptr<std::atomic<int>> m_lastDecodePx;
  /// shutdown() ran (whether invoked by a caller or the destructor). Keeps
  /// the destructor's own shutdown call a no-op after an early explicit one.
  bool m_shutdownDone = false;
};

#endif // ARTWORKLOADDISPATCHER_H
