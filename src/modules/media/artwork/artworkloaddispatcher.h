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

class CacheManager;
class QThreadPool;

/// Async batch dispatcher for the artwork pipeline. Owns the dedicated
/// thread pool, the in-flight QFuture set, and the cooperative cancellation
/// token. Handles the QtConcurrent worker decode/disk-cache pass; results
/// are delivered to caller-supplied handlers on the main thread.
///
/// Threading invariants:
///   - Worker tasks capture only stable values (cache pointer, the
///     cancellation shared_ptr, the input batch). They never capture
///     the dispatcher or any host raw — completion is posted to the
///     main thread via QCoreApplication::instance() as receiver, and
///     the handler is invoked there.
///   - cancelAll() flips the current token, then schedules a 50ms
///     regeneration. In-flight tasks observe the OLD (cancelled)
///     token forever; new dispatches capture the NEW token. This
///     prevents a slow I/O task from "resurrecting" after a same-atomic
///     reset.
///   - The destructor drains the pool with a bounded budget before
///     returning so no callback can fire after destruction.
class ArtworkLoadDispatcher : public QObject {
  Q_OBJECT

public:
  using BatchHandler = std::function<void(const QList<ArtworkInfo::Result> &results,
                                          int requestedCount, qint64 elapsedMs, bool highPriority)>;
  using PrecacheBatchHandler = std::function<void(const QStringList &requestedPaths,
                                                  const QList<ArtworkPrecacheResult> &results,
                                                  int requestedCount, qint64 elapsedMs)>;

  explicit ArtworkLoadDispatcher(CacheManager *cacheManager, QObject *parent = nullptr);
  ~ArtworkLoadDispatcher() override;

  /// Dispatch a UI-batch decode. @p onComplete runs on the main thread once
  /// every result is decoded (or the batch is cancelled / app is closing).
  void dispatchBatch(QList<ArtworkInfo> batch, bool highPriority, BatchHandler onComplete);

  /// Dispatch a precache decode (silent-load path). @p onComplete runs on
  /// the main thread.
  void dispatchPrecacheBatch(QStringList paths, PrecacheBatchHandler onComplete);

  /// Flip the cancellation token, then schedule a 50ms regen so the next
  /// dispatch sees a fresh token but in-flight tasks remain cancelled.
  void cancelAll();

  /// Number of currently-running futures. Used by silent-load throttle.
  [[nodiscard]] int runningFutureCount() const;

private:
  void pruneFinishedFutures();

  CacheManager *m_cacheManager;
  QThreadPool *m_threadPool = nullptr;
  mutable QMutex m_futureMutex;
  QList<QFuture<void>> m_futures;
  std::shared_ptr<std::atomic<bool>> m_cancellationToken;
};

#endif // ARTWORKLOADDISPATCHER_H
