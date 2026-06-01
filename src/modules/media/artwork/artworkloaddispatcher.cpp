#include "artworkloaddispatcher.h"

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
Q_DECLARE_LOGGING_CATEGORY(lcArtworkManager)

namespace {

// Decode-at-target-size: avoids loading full-resolution artwork unnecessarily,
// a major CPU+RAM win for large cover sets. DPR-aware so HiDPI displays get
// crisp output. Worker-thread safe — touches only QImageReader; the DPR
// argument must be snapshotted on the GUI thread by the caller because
// QGuiApplication::primaryScreen() is documented as a GUI-thread accessor.
auto loadAndProcessImage(const QString &path, qreal dpr) -> QImage {
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
  const int actualSize = qRound(UIConstants::Artwork::BOX_SIZE * dpr);

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

QList<ArtworkInfo::Result> processBatchOnWorker(const QList<ArtworkInfo> &batch,
                                                const std::atomic<quint64> &generationCounter,
                                                quint64 capturedGeneration,
                                                ICacheManager *cacheManager, qreal dpr) {
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
    bool loadedFromDiskCache = false;
    QImage img;
    if (cacheManager) {
      img = cacheManager->tryLoadArtworkImageFromDiskCache(info.artworkPath);
      loadedFromDiskCache = !img.isNull();
    }
    if (!loadedFromDiskCache) {
      img = loadAndProcessImage(info.artworkPath, dpr);
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

QList<ArtworkPrecacheResult> processPrecacheOnWorker(const QStringList &paths,
                                                     const std::atomic<quint64> &generationCounter,
                                                     quint64 capturedGeneration,
                                                     ICacheManager *cacheManager, qreal dpr) {
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
    bool loadedFromDiskCache = false;
    QImage img;
    if (cacheManager) {
      img = cacheManager->tryLoadArtworkImageFromDiskCache(artworkPath);
      loadedFromDiskCache = !img.isNull();
    }
    if (!loadedFromDiskCache) {
      img = loadAndProcessImage(artworkPath, dpr);
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
    : QObject(parent), m_cacheManager(cacheManager),
      m_currentGeneration(std::make_shared<std::atomic<quint64>>(0)) {
  const int idealThreads = QThread::idealThreadCount();
  const int base = idealThreads > 0 ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
                                    : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_threadPool = new QThreadPool();
  m_threadPool->setMaxThreadCount(std::clamp(base,
                                             UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                                             UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));
}

ArtworkLoadDispatcher::~ArtworkLoadDispatcher() {
  // Bump the generation so every in-flight task observes its captured
  // generation no longer matches and bails on the next check.
  if (m_currentGeneration) {
    m_currentGeneration->fetch_add(1, std::memory_order_acq_rel);
  }

  // Bounded-wait teardown so a slow decode doesn't block process exit.
  // Worker tasks observe the cancellation flag via shared_ptr capture, so
  // they remain safe to run to completion even after we abandon the pool.
  constexpr int kArtworkPoolDrainMs = 2000;
  if (!ThreadPoolUtils::shutdownWithBudget(m_threadPool, kArtworkPoolDrainMs)) {
    qCWarning(lcArtworkManager) << "ArtworkLoadDispatcher: thread pool did not drain in"
                                << kArtworkPoolDrainMs
                                << "ms during shutdown; abandoning pool to avoid blocking exit";
  }

  QMutexLocker futureLock(&m_futureMutex);
  for (auto &future : m_futures) {
    if (future.isRunning()) {
      future.cancel();
    }
  }
  m_futures.clear();
}

void ArtworkLoadDispatcher::dispatchBatch(QList<ArtworkInfo> batch, bool highPriority,
                                          BatchHandler onComplete) {
  if (batch.isEmpty() || !m_threadPool) {
    return;
  }

  ICacheManager *const cacheManager = m_cacheManager;
  const auto generationCounter = m_currentGeneration;
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
  // Snapshot DPR on the GUI thread — QGuiApplication::primaryScreen() is not
  // safe to call from a worker. Dpr changes are rare; at-most one batch lags.
  const qreal dpr = QGuiApplication::primaryScreen()
                        ? QGuiApplication::primaryScreen()->devicePixelRatio()
                        : qreal{1.0};

  QFuture<void> future = QtConcurrent::run(
      m_threadPool, [batch = std::move(batch), highPriority, generationCounter, generation,
                     batchItemCount, appReceiver, cacheManager, handler, dpr]() {
        if (QApplication::closingDown() || !generationCounter ||
            isCancelledForGeneration(*generationCounter, generation)) {
          return;
        }
        QElapsedTimer timer;
        timer.start();
        QList<ArtworkInfo::Result> results =
            processBatchOnWorker(batch, *generationCounter, generation, cacheManager, dpr);
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

  ICacheManager *const cacheManager = m_cacheManager;
  const auto generationCounter = m_currentGeneration;
  const quint64 generation =
      generationCounter ? generationCounter->load(std::memory_order_acquire) : quint64{0};
  QObject *appReceiver = QCoreApplication::instance();
  const int batchItemCount = paths.size();
  auto handler = std::move(onComplete);
  // Snapshot DPR on the GUI thread — see dispatchBatch().
  const qreal dpr = QGuiApplication::primaryScreen()
                        ? QGuiApplication::primaryScreen()->devicePixelRatio()
                        : qreal{1.0};

  QFuture<void> future =
      QtConcurrent::run(m_threadPool, [paths = std::move(paths), generationCounter, generation,
                                       batchItemCount, appReceiver, cacheManager, handler, dpr]() {
        if (QApplication::closingDown() || !generationCounter ||
            isCancelledForGeneration(*generationCounter, generation)) {
          return;
        }
        QElapsedTimer timer;
        timer.start();
        QList<ArtworkPrecacheResult> results =
            processPrecacheOnWorker(paths, *generationCounter, generation, cacheManager, dpr);
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
