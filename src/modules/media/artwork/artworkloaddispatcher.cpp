#include "artworkloaddispatcher.h"

#include "cachemanager.h"
#include "extensionutils.h"
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
#include <QTimer>

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

QList<ArtworkInfo::Result> processBatchOnWorker(const QList<ArtworkInfo> &batch,
                                                const std::atomic<bool> &cancelled,
                                                CacheManager *cacheManager, qreal dpr) {
  QList<ArtworkInfo::Result> results;
  results.reserve(batch.size());
  for (const ArtworkInfo &info : batch) {
    if (QApplication::closingDown() || cancelled.load(std::memory_order_relaxed)) {
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
    if (QApplication::closingDown() || cancelled.load(std::memory_order_relaxed)) {
      break;
    }
    if (img.isNull()) {
      continue;
    }
    results.append(ArtworkInfo::Result{.widget = info.mediaItem,
                                       .artworkPath = info.artworkPath,
                                       .image = img,
                                       .loadedFromDiskCache = loadedFromDiskCache});
  }
  return results;
}

QList<ArtworkPrecacheResult> processPrecacheOnWorker(const QStringList &paths,
                                                     const std::atomic<bool> &cancelled,
                                                     CacheManager *cacheManager, qreal dpr) {
  QList<ArtworkPrecacheResult> results;
  results.reserve(paths.size());
  for (const QString &artworkPath : paths) {
    if (QApplication::closingDown() || cancelled.load(std::memory_order_relaxed)) {
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
    if (QApplication::closingDown() || cancelled.load(std::memory_order_relaxed)) {
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

ArtworkLoadDispatcher::ArtworkLoadDispatcher(CacheManager *cacheManager, QObject *parent)
    : QObject(parent), m_cacheManager(cacheManager),
      m_cancellationToken(std::make_shared<std::atomic<bool>>(false)) {
  const int idealThreads = QThread::idealThreadCount();
  const int base = idealThreads > 0 ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
                                    : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_threadPool = new QThreadPool();
  m_threadPool->setMaxThreadCount(std::clamp(base,
                                             UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                                             UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));
}

ArtworkLoadDispatcher::~ArtworkLoadDispatcher() {
  if (m_cancellationToken) {
    m_cancellationToken->store(true, std::memory_order_release);
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

  CacheManager *const cacheManager = m_cacheManager;
  const auto cancelFlag = m_cancellationToken;
  QObject *appReceiver = QCoreApplication::instance();
  const int batchItemCount = batch.size();
  auto handler = std::move(onComplete);
  // Snapshot DPR on the GUI thread — QGuiApplication::primaryScreen() is not
  // safe to call from a worker. Dpr changes are rare; at-most one batch lags.
  const qreal dpr = QGuiApplication::primaryScreen()
                        ? QGuiApplication::primaryScreen()->devicePixelRatio()
                        : qreal{1.0};

  QFuture<void> future = QtConcurrent::run(m_threadPool, [batch = std::move(batch), highPriority,
                                                          cancelFlag, batchItemCount, appReceiver,
                                                          cacheManager, handler, dpr]() {
    if (QApplication::closingDown() || !cancelFlag || cancelFlag->load(std::memory_order_relaxed)) {
      return;
    }
    QElapsedTimer timer;
    timer.start();
    QList<ArtworkInfo::Result> results = processBatchOnWorker(batch, *cancelFlag, cacheManager, dpr);
    const qint64 elapsedMs = timer.elapsed();
    if (QApplication::closingDown() || !cancelFlag || cancelFlag->load(std::memory_order_relaxed)) {
      return;
    }
    if (!appReceiver) {
      return;
    }
    QMetaObject::invokeMethod(
        appReceiver,
        [results, highPriority, batchItemCount, elapsedMs, cancelFlag, handler]() {
          if (QApplication::closingDown() || !cancelFlag ||
              cancelFlag->load(std::memory_order_relaxed)) {
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

  CacheManager *const cacheManager = m_cacheManager;
  const auto cancelFlag = m_cancellationToken;
  QObject *appReceiver = QCoreApplication::instance();
  const int batchItemCount = paths.size();
  auto handler = std::move(onComplete);
  // Snapshot DPR on the GUI thread — see dispatchBatch().
  const qreal dpr = QGuiApplication::primaryScreen()
                        ? QGuiApplication::primaryScreen()->devicePixelRatio()
                        : qreal{1.0};

  QFuture<void> future = QtConcurrent::run(m_threadPool, [paths = std::move(paths), cancelFlag,
                                                          batchItemCount, appReceiver, cacheManager,
                                                          handler, dpr]() {
    if (QApplication::closingDown() || !cancelFlag || cancelFlag->load(std::memory_order_relaxed)) {
      return;
    }
    QElapsedTimer timer;
    timer.start();
    QList<ArtworkPrecacheResult> results =
        processPrecacheOnWorker(paths, *cancelFlag, cacheManager, dpr);
    const qint64 elapsedMs = timer.elapsed();
    if (QApplication::closingDown() || !cancelFlag || cancelFlag->load(std::memory_order_relaxed)) {
      return;
    }
    if (!appReceiver) {
      return;
    }
    QMetaObject::invokeMethod(
        appReceiver,
        [paths, results, batchItemCount, elapsedMs, cancelFlag, handler]() {
          if (QApplication::closingDown() || !cancelFlag ||
              cancelFlag->load(std::memory_order_relaxed)) {
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
  if (m_cancellationToken) {
    m_cancellationToken->store(true, std::memory_order_relaxed);
  }
  // Regenerate the token so future dispatches see a fresh flag while
  // in-flight tasks observe the OLD (permanently-cancelled) flag. Delay
  // chosen to be larger than typical thread-scheduling latency so the
  // existing tasks have a chance to notice cancellation.
  QTimer::singleShot(
      50, this, [this]() { m_cancellationToken = std::make_shared<std::atomic<bool>>(false); });
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
  for (int i = m_futures.size() - 1; i >= 0; --i) {
    if (m_futures[i].isFinished()) {
      m_futures.removeAt(i);
    }
  }
}
