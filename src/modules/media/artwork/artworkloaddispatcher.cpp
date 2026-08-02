#include "artworkloaddispatcher.h"

#include <algorithm>

#include "artworkutils.h"
#include "cachemanager.h"
#include "extensionutils.h"
#include "icachemanager.h"
#include "loggingcategories.h"
#include "threadpoolutils.h"
#include "uiconstants/artwork.h"
#include "uiconstants/concurrency.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QScreen>
#include <QSize>
#include <QtConcurrent>
#include <QThread>
#include <QThreadPool>

#include <QLoggingCategory>
#include <QReadWriteLock>
Q_DECLARE_LOGGING_CATEGORY(lcArtworkManager)

/// Shared, self-owned forwarding surface between the dispatcher and its
/// worker tasks (Kartend-xoftg). Workers co-own this handle via shared_ptr
/// capture (mirroring the generation-counter pattern) and route every
/// disk-cache read through tryLoadFromDiskCache(); ~ArtworkLoadDispatcher
/// invalidates the inner pointer before CacheManager can be destroyed later
/// in ApplicationManager teardown, so a task abandoned by the bounded pool
/// drain (threadpoolutils.h abandon-on-timeout contract) makes a guarded
/// no-op call instead of dereferencing a freed cache.
///
/// Two layers of protection:
///   - The atomic pointer is nulled first, so no NEW disk-cache read can
///     begin after invalidate() starts (one cheap atomic load per item).
///   - The read-write lock pins the cache for the duration of each read;
///     invalidate() then waits (bounded) for the write lock, so an
///     in-flight read that already entered the cache is drained rather
///     than raced. Only a read wedged inside the cache beyond the quiesce
///     budget retains the (loudly logged) residual risk.
class ArtworkDispatcherCacheHandle {
public:
  explicit ArtworkDispatcherCacheHandle(ICacheManager *cacheManager)
      : m_cacheManager(cacheManager) {}

  /// Worker-side disk-cache read. Cost on the hot path is one tryLockForRead
  /// (uncontended atomic CAS) plus one atomic load per item — negligible next
  /// to the disk I/O it guards.
  QImage tryLoadFromDiskCache(const QString &artworkPath) {
    if (!m_pinLock.tryLockForRead()) {
      // invalidate() holds (or is waiting on) the write lock: the cache is
      // going away. Skip the disk cache; the caller falls back to decoding
      // the original artwork file, which is owned by nobody we can outlive.
      return {};
    }
    QImage img;
    if (ICacheManager *cm = m_cacheManager.load(std::memory_order_acquire)) {
      img = cm->tryLoadArtworkImageFromDiskCache(artworkPath);
    }
    m_pinLock.unlock();
    return img;
  }

  /// Startup wiring only (Kartend-davi): rebinding while reads are in
  /// flight would expose the previous pointer to the same lifetime hazard
  /// invalidate() exists to close.
  void setCacheManager(ICacheManager *cacheManager) {
    m_cacheManager.store(cacheManager, std::memory_order_release);
  }

  /// Cuts workers off from the cache and waits (bounded) for any read that
  /// already entered it to exit. Called from ~ArtworkLoadDispatcher, after
  /// the pool drain attempt: if the pool drained, the write lock is free
  /// and this is instant; if the pool was abandoned, the budget gives a
  /// wedged in-cache read a last chance to finish before the cache dies.
  void invalidate(int quiesceBudgetMs) {
    m_cacheManager.store(nullptr, std::memory_order_release);
    if (m_pinLock.tryLockForWrite(quiesceBudgetMs)) {
      m_pinLock.unlock();
    } else {
      qCWarning(lcArtworkManager)
          << "ArtworkDispatcherCacheHandle: abandoned artwork task still inside the disk cache"
          << quiesceBudgetMs << "ms after invalidation; cache teardown may race the in-flight read";
    }
  }

private:
  std::atomic<ICacheManager *> m_cacheManager;
  QReadWriteLock m_pinLock;
};

namespace {

// Kartend-wztmg: slack over the tile's measured size, so a modest relayout
// (a window nudge, a sidebar toggle) recomposes from the cached image instead
// of forcing a re-decode of the whole viewport.
constexpr qreal kDecodeHeadroom = 1.25;
// Never decode below this: a pathological layout reporting a few logical
// pixels shouldn't poison the path-keyed cache with a thumbnail-sized entry.
constexpr int kMinDecodePx = 128;
// Slack before an oversized cache hit is worth rescaling. Entries written by
// an older build (or a wider grid) are common; only rewrite when the saving
// is real.
constexpr qreal kOversizeTolerance = 1.35;

// Longest edge, in device pixels, worth decoding for a tile whose artwork
// label measured `targetLabelSize` logical px at `dpr`.
//
// Kartend-wztmg: this used to be a flat BOX_SIZE * dpr for every tile. On a
// 200px grid at 1.4x scaling that decoded 560px art to draw it at 280 — 4x
// the pixels, held for the process lifetime by the path-keyed memory cache
// (heaptrack: 173M retained across 140 images, 1.24M apiece). BOX_SIZE stays
// the ceiling, so this can only ever decode less than before, never more.
//
// An invalid/empty size means the widget had not been laid out at dispatch;
// fall back to the old behaviour rather than guess.
auto artworkDecodePx(const QSize &targetLabelSize, qreal dpr) -> int {
  const int boxPx = qRound(UIConstants::Artwork::BOX_SIZE * dpr);
  if (!targetLabelSize.isValid() || targetLabelSize.isEmpty()) {
    return boxPx;
  }
  const int longestEdge = std::max(targetLabelSize.width(), targetLabelSize.height());
  const int wanted = qRound(longestEdge * dpr * kDecodeHeadroom);
  return std::clamp(wanted, kMinDecodePx, boxPx);
}

// Decode-at-target-size: avoids loading full-resolution artwork unnecessarily,
// a major CPU+RAM win for large cover sets. DPR-aware so HiDPI displays get
// crisp output. Worker-thread safe — touches only QImageReader; the DPR
// argument must be snapshotted on the GUI thread by the caller because
// QGuiApplication::primaryScreen() is documented as a GUI-thread accessor.
auto loadAndProcessImage(const QString &path, qreal dpr, int targetPx) -> QImage {
  if (path.isEmpty() || !QFile::exists(path)) {
    return {};
  }
  // Extension guard: a non-image file (e.g. a scraped .pdf manual) routed
  // into QImageReader reaches Qt's PDFium-backed PDF plugin, which calls
  // abort() on some inputs — taking down the whole process. This is the
  // worker-thread decode path; keep it strictly images-only.
  if (!ExtensionUtils::isDecodableImagePath(path)) {
    return {};
  }
  const int actualSize = targetPx;

  QImageReader reader(path);
  reader.setAutoTransform(true);
  reader.setAllocationLimit(UIConstants::Artwork::MAX_DECODE_MB);
  const QSize originalSize = reader.size();
  if (originalSize.isValid()) {
    QSize scaled = originalSize;
    scaled.scale(actualSize, actualSize, Qt::KeepAspectRatio);
    reader.setScaledSize(scaled);
  } else {
    reader.setScaledSize(QSize(actualSize, actualSize));
  }
  QImage img = reader.read();
  if (img.isNull()) {
    return {};
  }
  img.setDevicePixelRatio(dpr);
  return img;
}

// True when the dispatcher has been asked to cancel since this task was
// dispatched. The captured generation freezes "what was current at
// dispatch time"; the counter holds the live value. Any cancelAll() or
// destruction bumps the counter, which makes this predicate flip to
// true on the worker's next call.
inline bool isCancelledForGeneration(const std::atomic<quint64> &counter, quint64 capturedGen) {
  return counter.load(std::memory_order_acquire) != capturedGen;
}

QList<ArtworkInfo::Result>
processBatchOnWorker(const QList<ArtworkInfo> &batch, const std::atomic<quint64> &generationCounter,
                     quint64 capturedGeneration,
                     const std::shared_ptr<ArtworkDispatcherCacheHandle> &cacheHandle,
                     qreal fallbackDpr, std::atomic<int> &lastDecodePx) {
  // Worker-only contract: QImage is reentrant, QPixmap is not. Any code path
  // that reaches here on the GUI thread would risk a future maintainer
  // constructing QPixmap below, which Qt explicitly forbids off the main
  // thread.
  Q_ASSERT_X(QThread::currentThread() != QCoreApplication::instance()->thread(),
             "processBatchOnWorker",
             "Artwork decode must run on a worker thread, not the GUI thread");
  QList<ArtworkInfo::Result> results;
  results.reserve(batch.size());
  for (const ArtworkInfo &info : batch) {
    if (QApplication::closingDown() ||
        isCancelledForGeneration(generationCounter, capturedGeneration)) {
      break;
    }
    if (info.mediaItem.isNull()) {
      continue;
    }
    // Per-item DPR: the widget's own screen, snapshotted on the GUI thread at
    // dispatch (mixed-DPI setups). 0.0 means the producer didn't snapshot one
    // — fall back to the batch-level primary-screen value. Identical on
    // single-screen setups.
    const qreal dpr = info.dpr > 0.0 ? info.dpr : fallbackDpr;
    // Disk-cache reads go through the shared handle, never a raw cache
    // pointer — the handle is invalidated in ~ArtworkLoadDispatcher so a
    // task abandoned by the bounded pool drain no-ops here (Kartend-xoftg).
    const int decodePx = artworkDecodePx(info.targetLabelSize, dpr);
    if (decodePx > 0) {
      lastDecodePx.store(decodePx, std::memory_order_relaxed);
    }
    QImage img = cacheHandle ? cacheHandle->tryLoadFromDiskCache(info.artworkPath) : QImage{};
    bool loadedFromDiskCache = !img.isNull();
    if (loadedFromDiskCache) {
      // Kartend-wztmg: the disk cache is keyed by artwork path alone, so an
      // entry can predate this build (written at the old flat BOX_SIZE) or
      // belong to a differently-sized grid. Reconcile it against what this
      // tile actually needs, in whichever direction it is wrong.
      const int haveEdge = std::max(img.width(), img.height());
      if (haveEdge < decodePx) {
        // Too small to compose a crisp card — re-decode from the original
        // rather than upscale. Falls through to the fresh-decode branch,
        // which also rewrites the cache entry at the size we want.
        img = QImage();
        loadedFromDiskCache = false;
      } else if (haveEdge > qRound(decodePx * kOversizeTolerance)) {
        // Oversized (the common case on an existing cache): shrink before it
        // reaches the memory cache, which would otherwise retain the full
        // original for the process lifetime. Scaling here keeps the disk
        // entry untouched and costs far less than the disk read it follows.
        img = img.scaled(decodePx, decodePx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      }
    }
    if (loadedFromDiskCache) {
      // Disk-cached PNGs decode with a default (1.0) DPR. Tag them with the
      // same DPR a fresh decode gets so the pixmaps and memory-cache entries
      // built from this image are consistently tagged either way.
      img.setDevicePixelRatio(dpr);
    } else {
      img = loadAndProcessImage(info.artworkPath, dpr, decodePx);
    }
    if (QApplication::closingDown() ||
        isCancelledForGeneration(generationCounter, capturedGeneration)) {
      break;
    }
    if (img.isNull()) {
      continue;
    }
    // Kartend-63wg: compose the finished, corner-masked card here on the worker
    // when the widget's render spec was captured at dispatch (laid-out widget).
    // The GUI then just sets it instead of re-scaling + re-compositing per tile.
    // The raw `img` is still delivered (and cached by path) for the composite
    // fallback the GUI uses if the tile resized since dispatch.
    QImage composed;
    if (info.targetLabelSize.isValid() && !info.targetLabelSize.isEmpty()) {
      composed = ArtworkUtils::composeArtworkCard(img, info.targetLabelSize.width(),
                                                  info.targetLabelSize.height(), dpr,
                                                  info.cornerRadius, info.backgroundColor);
    }
    results.append(
        ArtworkInfo::Result{.widget = info.mediaItem,
                            .artworkPath = info.artworkPath,
                            .artworkBaseName = QFileInfo(info.artworkPath).completeBaseName(),
                            .widgetIdentity = info.widgetIdentity,
                            .image = img,
                            .composedCard = composed,
                            .composedForSize = composed.isNull() ? QSize() : info.targetLabelSize,
                            .loadedFromDiskCache = loadedFromDiskCache});
  }
  return results;
}

QList<ArtworkPrecacheResult>
processPrecacheOnWorker(const QStringList &paths, const std::atomic<quint64> &generationCounter,
                        quint64 capturedGeneration,
                        const std::shared_ptr<ArtworkDispatcherCacheHandle> &cacheHandle, qreal dpr,
                        const std::atomic<int> &lastDecodePx) {
  // Worker-only contract — see processBatchOnWorker.
  Q_ASSERT_X(QThread::currentThread() != QCoreApplication::instance()->thread(),
             "processPrecacheOnWorker",
             "Artwork precache decode must run on a worker thread, not the GUI thread");
  QList<ArtworkPrecacheResult> results;
  results.reserve(paths.size());
  for (const QString &artworkPath : paths) {
    if (QApplication::closingDown() ||
        isCancelledForGeneration(generationCounter, capturedGeneration)) {
      break;
    }
    // Shared-handle disk-cache read — see processBatchOnWorker (Kartend-xoftg).
    // Kartend-wztmg: precache runs ahead of the viewport, so there is no
    // widget to measure. Reuse the size the last real batch settled on — the
    // grid it is precaching for — and fall back to the historical flat
    // BOX_SIZE only before any batch has run. Matching the batch size matters
    // for more than memory: the cache is path-keyed, so a precache entry
    // written at a different size is what the tile would later pick up.
    const int rememberedPx = lastDecodePx.load(std::memory_order_relaxed);
    const int decodePx =
        rememberedPx > 0 ? rememberedPx : qRound(UIConstants::Artwork::BOX_SIZE * dpr);
    QImage img = cacheHandle ? cacheHandle->tryLoadFromDiskCache(artworkPath) : QImage{};
    bool loadedFromDiskCache = !img.isNull();
    if (loadedFromDiskCache &&
        std::max(img.width(), img.height()) > qRound(decodePx * kOversizeTolerance)) {
      // Oversized entry from an older build or a wider grid — shrink before
      // it lands in the memory cache. Unlike the batch path there is no
      // undersize re-decode here: precache is speculative, and a slightly
      // small entry is repaired by the batch path when the tile scrolls in.
      img = img.scaled(decodePx, decodePx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (loadedFromDiskCache) {
      // Consistent DPR tagging for disk-cache hits — see processBatchOnWorker.
      img.setDevicePixelRatio(dpr);
    } else {
      img = loadAndProcessImage(artworkPath, dpr, decodePx);
    }
    if (QApplication::closingDown() ||
        isCancelledForGeneration(generationCounter, capturedGeneration)) {
      break;
    }
    if (img.isNull()) {
      continue;
    }
    results.append(ArtworkPrecacheResult{
        .artworkPath = artworkPath, .image = img, .loadedFromDiskCache = loadedFromDiskCache});
  }
  return results;
}

} // namespace

ArtworkLoadDispatcher::ArtworkLoadDispatcher(ICacheManager *cacheManager, QObject *parent)
    : QObject(parent), m_cacheHandle(std::make_shared<ArtworkDispatcherCacheHandle>(cacheManager)),
      m_currentGeneration(std::make_shared<std::atomic<quint64>>(0)),
      m_lastDecodePx(std::make_shared<std::atomic<int>>(0)) {
  const int idealThreads = QThread::idealThreadCount();
  const int base = idealThreads > 0 ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
                                    : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_threadPool = new QThreadPool();
  m_threadPool->setMaxThreadCount(std::clamp(base,
                                             UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                                             UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));
}

ArtworkLoadDispatcher::~ArtworkLoadDispatcher() {
  // Full standalone budget: pool drain + cache quiesce, sequential. A caller
  // that already invoked shutdown() with a shared teardown deadline (the
  // ArtworkManager destructor chain) makes this a no-op.
  shutdown(QDeadlineTimer(kPoolDrainBudgetMs + kCacheQuiesceBudgetMs));
}

void ArtworkLoadDispatcher::shutdown(QDeadlineTimer deadline) {
  if (m_shutdownDone) {
    return;
  }
  m_shutdownDone = true;

  // Bump the generation so every in-flight task observes its captured
  // generation no longer matches and bails on the next check.
  if (m_currentGeneration) {
    m_currentGeneration->fetch_add(1, std::memory_order_acq_rel);
  }

  // Each stage waits at most min(its own budget, the caller's remaining
  // deadline), so sequential bounded teardown stages can't each stack their
  // full budget against the same stalled worker.
  const auto stageBudgetMs = [&deadline](int ownBudgetMs) {
    if (deadline.isForever()) {
      return ownBudgetMs;
    }
    return static_cast<int>(
        qMin<qint64>(ownBudgetMs, qMax<qint64>(qint64{0}, deadline.remainingTime())));
  };

  // Bounded-wait teardown so a slow decode doesn't block process exit.
  // Worker tasks observe the cancellation flag via shared_ptr capture, so
  // they remain safe to run to completion even after we abandon the pool.
  const int poolDrainMs = stageBudgetMs(kPoolDrainBudgetMs);
  if (!ThreadPoolUtils::shutdownWithBudget(m_threadPool, poolDrainMs)) {
    qCWarning(lcArtworkManager) << "ArtworkLoadDispatcher: thread pool did not drain in"
                                << poolDrainMs
                                << "ms during shutdown; abandoning pool to avoid blocking exit";
  }

  // Kartend-xoftg: cut abandoned tasks off from the cache BEFORE teardown
  // proceeds — CacheManager is destroyed later in ApplicationManager
  // teardown, and an abandoned task's next disk-cache read must be a guarded
  // no-op rather than a use-after-free. When the pool drained above, no
  // worker exists and this is instant; on the abandon path the small quiesce
  // budget drains a read that already entered the cache.
  if (m_cacheHandle) {
    m_cacheHandle->invalidate(stageBudgetMs(kCacheQuiesceBudgetMs));
  }

  QMutexLocker futureLock(&m_futureMutex);
  for (auto &future : m_futures) {
    if (future.isRunning()) {
      future.cancel();
    }
  }
  m_futures.clear();
}

void ArtworkLoadDispatcher::setCacheManager(ICacheManager *cacheManager) {
  if (m_cacheHandle) {
    m_cacheHandle->setCacheManager(cacheManager);
  }
}

void ArtworkLoadDispatcher::dispatchBatch(QList<ArtworkInfo> batch, bool highPriority,
                                          BatchHandler onComplete) {
  if (batch.isEmpty() || !m_threadPool) {
    return;
  }

  // Workers capture the shared handle, never a raw cache pointer — the
  // dtor invalidates the handle so abandoned tasks no-op (Kartend-xoftg).
  const auto cacheHandle = m_cacheHandle;
  const auto generationCounter = m_currentGeneration;
  const auto lastDecodePx = m_lastDecodePx;
  // Snapshot the live generation at dispatch time. cancelAll() bumps the
  // counter atomically; this captures the value *now*, so this task only
  // bails if a *future* cancelAll() (or destruction) moves the counter
  // past it. New dispatches after a cancelAll see the post-bump value
  // and run to completion without the prior 50ms-timer race (Kartend-uxo0).
  const quint64 generation =
      generationCounter ? generationCounter->load(std::memory_order_acquire) : quint64{0};
  QObject *appReceiver = QCoreApplication::instance();
  const int batchItemCount = batch.size();
  auto handler = std::move(onComplete);
  // Fallback DPR for batch items that carry no per-widget snapshot
  // (ArtworkInfo::dpr == 0). Snapshot it on the GUI thread —
  // QGuiApplication::primaryScreen() is not safe to call from a worker.
  // Dpr changes are rare; at-most one batch lags.
  const qreal fallbackDpr = QGuiApplication::primaryScreen()
                                ? QGuiApplication::primaryScreen()->devicePixelRatio()
                                : qreal{1.0};

  QFuture<void> future =
      QtConcurrent::run(m_threadPool, [batch = std::move(batch), highPriority, generationCounter,
                                       generation, batchItemCount, appReceiver, cacheHandle,
                                       handler, fallbackDpr, lastDecodePx]() {
        if (QApplication::closingDown() || !generationCounter || !lastDecodePx ||
            isCancelledForGeneration(*generationCounter, generation)) {
          return;
        }
        QElapsedTimer timer;
        timer.start();
        QList<ArtworkInfo::Result> results = processBatchOnWorker(
            batch, *generationCounter, generation, cacheHandle, fallbackDpr, *lastDecodePx);
        const qint64 elapsedMs = timer.elapsed();
        if (QApplication::closingDown() || !generationCounter ||
            isCancelledForGeneration(*generationCounter, generation)) {
          return;
        }
        if (!appReceiver) {
          return;
        }
        QMetaObject::invokeMethod(
            appReceiver,
            [results, highPriority, batchItemCount, elapsedMs, generationCounter, generation,
             handler]() {
              if (QApplication::closingDown() || !generationCounter ||
                  isCancelledForGeneration(*generationCounter, generation)) {
                return;
              }
              if (handler) {
                handler(results, batchItemCount, elapsedMs, highPriority);
              }
            },
            Qt::QueuedConnection);
      });

  QMutexLocker futureLock(&m_futureMutex);
  m_futures.append(future);
  pruneFinishedFutures();
}

void ArtworkLoadDispatcher::dispatchPrecacheBatch(QStringList paths,
                                                  PrecacheBatchHandler onComplete) {
  if (paths.isEmpty() || !m_threadPool) {
    return;
  }

  // Shared handle, not a raw cache pointer — see dispatchBatch (Kartend-xoftg).
  const auto cacheHandle = m_cacheHandle;
  const auto generationCounter = m_currentGeneration;
  const auto lastDecodePx = m_lastDecodePx;
  const quint64 generation =
      generationCounter ? generationCounter->load(std::memory_order_acquire) : quint64{0};
  QObject *appReceiver = QCoreApplication::instance();
  const int batchItemCount = paths.size();
  auto handler = std::move(onComplete);
  // Snapshot DPR on the GUI thread — see dispatchBatch().
  const qreal dpr = QGuiApplication::primaryScreen()
                        ? QGuiApplication::primaryScreen()->devicePixelRatio()
                        : qreal{1.0};

  QFuture<void> future = QtConcurrent::run(
      m_threadPool, [paths = std::move(paths), generationCounter, generation, batchItemCount,
                     appReceiver, cacheHandle, handler, dpr, lastDecodePx]() {
        if (QApplication::closingDown() || !generationCounter || !lastDecodePx ||
            isCancelledForGeneration(*generationCounter, generation)) {
          return;
        }
        QElapsedTimer timer;
        timer.start();
        QList<ArtworkPrecacheResult> results = processPrecacheOnWorker(
            paths, *generationCounter, generation, cacheHandle, dpr, *lastDecodePx);
        const qint64 elapsedMs = timer.elapsed();
        if (QApplication::closingDown() || !generationCounter ||
            isCancelledForGeneration(*generationCounter, generation)) {
          return;
        }
        if (!appReceiver) {
          return;
        }
        QMetaObject::invokeMethod(
            appReceiver,
            [paths, results, batchItemCount, elapsedMs, generationCounter, generation, handler]() {
              if (QApplication::closingDown() || !generationCounter ||
                  isCancelledForGeneration(*generationCounter, generation)) {
                return;
              }
              if (handler) {
                handler(paths, results, batchItemCount, elapsedMs);
              }
            },
            Qt::QueuedConnection);
      });

  QMutexLocker futureLock(&m_futureMutex);
  m_futures.append(future);
  pruneFinishedFutures();
}

void ArtworkLoadDispatcher::cancelAll() {
  // Atomic bump of the generation counter. In-flight tasks captured a
  // smaller generation value at dispatch time and will observe the
  // mismatch on their next check (no race window — dispatches after this
  // call read the post-bump value and run normally). Replaces a prior
  // token-swap design where a 50ms QTimer regenerated the cancellation
  // shared_ptr; back-to-back cancelAll calls within that window left
  // freshly-dispatched batches holding a still-cancelled token and
  // stalled the UI artwork pipeline (Kartend-uxo0).
  if (m_currentGeneration) {
    m_currentGeneration->fetch_add(1, std::memory_order_acq_rel);
  }
}

int ArtworkLoadDispatcher::runningFutureCount() const {
  QMutexLocker locker(&m_futureMutex);
  int n = 0;
  for (const auto &f : m_futures) {
    if (f.isRunning()) {
      ++n;
    }
  }
  return n;
}

void ArtworkLoadDispatcher::pruneFinishedFutures() {
  // Kartend-gro2: std::erase_if avoids the O(N) shift per removeAt that the
  // reverse-loop variant paid (removeAt still memmove's the tail down).
  m_futures.removeIf([](const QFuture<void> &f) { return f.isFinished(); });
}
