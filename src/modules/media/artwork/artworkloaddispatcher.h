#ifndef ARTWORKLOADDISPATCHER_H
#define ARTWORKLOADDISPATCHER_H

#include "artworkmanager.h"

#include <atomic>
#include <functional>
#include <memory>
#include <QFuture>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

class ICacheManager;
class QThreadPool;

/// Async batch dispatcher for the artwork pipeline. Owns the dedicated
/// thread pool, the in-flight QFuture set, and the cooperative cancellation
/// generation counter. Handles the QtConcurrent worker decode/disk-cache
/// pass; results are delivered to caller-supplied handlers on the main
/// thread.
///
/// Threading invariants:
///   - Worker tasks capture only stable values (cache pointer, a
///     shared_ptr to the current-generation atomic, the generation
///     they were dispatched at, the input batch). They never capture
///     the dispatcher or any host raw — completion is posted to the
///     main thread via QCoreApplication::instance() as receiver, and
///     the handler is invoked there.
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
  /// ctx becomes available.
  void setCacheManager(ICacheManager *cacheManager) { m_cacheManager = cacheManager; }

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

private:
  void pruneFinishedFutures();

  ICacheManager *m_cacheManager;
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
};

#endif // ARTWORKLOADDISPATCHER_H
