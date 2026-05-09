// Sibling translation unit for ArtworkManager.
// Owns the disk-decode helper plus the precache/parallel-loading pipeline:
//   - loadAndProcessImage (free function, declared in artworkmanagerinternal.h)
//   - dispatchAndTrackPrecacheBatch (member)
//   - applyResultsToUi (member)
//   - loadArtworkParallel (member)
// Extracted from artworkmanager.cpp during the Tier 4 LOC sweep
// to bring artworkmanager.cpp under 500 LOC.
#include "artworkmanager.h"
#include "artworkmanagerinternal.h"
#include "cachemanager.h"
#include "loggingcategories.h"
#include "ui/widgets/itemwidget.h"
#include "uiconstants.h"

#include <atomic>
#include <memory>
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
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <QtConcurrent>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcArtworkManager)

// ─────────────────────────────────────────────────────────────────────────────
// Free helper: disk decode (declared in artworkmanagerinternal.h)
// ─────────────────────────────────────────────────────────────────────────────

auto loadAndProcessImage(const QString &path) -> QImage {
  if (path.isEmpty() || !QFile::exists(path)) {
    return {};
  }

  // Decode-at-size to avoid loading full-resolution artwork unnecessarily.
  // This is a major CPU+RAM win for large PNG/JPG cover sets.
  qreal dpr = 1.0;
  if (QGuiApplication::primaryScreen()) {
    dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
  }
  const int actualSize = qRound(UIConstants::Artwork::BOX_SIZE * dpr);

  QImageReader reader(path);
  reader.setAutoTransform(true);
  reader.setAllocationLimit(UIConstants::Artwork::MAX_DECODE_MB);
  // Preserve aspect ratio while decoding near the target size.
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

// ─────────────────────────────────────────────────────────────────────────────
// File-local helpers used only by the precache pipeline below
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Determines the appropriate batch size for artwork loading.
// Uses adaptive batching when no custom size specified, with high-priority
// using current adaptive size and low-priority using a reduced adaptive size.
auto determineBatchSize(bool highPriority, int customBatchSize, const AdaptiveBatcher &batcher)
    -> int {
  if (customBatchSize > 0) {
    return customBatchSize;
  }
  // Use adaptive batch size, with low priority getting half the size
  int adaptiveSize = batcher.currentBatchSize();
  return highPriority ? adaptiveSize : qMax(2, adaptiveSize / 2);
}

struct PrecacheResult {
  QString artworkPath;
  QImage image;
  bool loadedFromDiskCache = false;
};

auto processPrecacheBatch(const QStringList &paths, const std::atomic<bool> &cancelled,
                          CacheManager *cacheManager) -> QList<PrecacheResult> {
  QList<PrecacheResult> results;
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
      img = loadAndProcessImage(artworkPath);
    }

    if (QApplication::closingDown() || cancelled.load(std::memory_order_relaxed)) {
      break;
    }
    if (img.isNull()) {
      continue;
    }

    results.append(PrecacheResult{
        .artworkPath = artworkPath, .image = img, .loadedFromDiskCache = loadedFromDiskCache});
  }

  return results;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ArtworkManager precache members
// ─────────────────────────────────────────────────────────────────────────────

void ArtworkManager::dispatchAndTrackPrecacheBatch(const QStringList &artworkPaths) {
  if (artworkPaths.isEmpty()) {
    return;
  }

  CacheManager *const cacheManager = m_cacheManager;
  const auto cancelFlag = m_cancellationRequested;
  QPointer<ArtworkManager> self(this);
  QObject *appReceiver = QCoreApplication::instance();
  const int batchItemCount = artworkPaths.size();

  if (!m_artworkThreadPool) {
    return;
  }
  QFuture<void> future = QtConcurrent::run(m_artworkThreadPool, [self, artworkPaths, cancelFlag,
                                                                 appReceiver, cacheManager,
                                                                 batchItemCount]() {
    if (QApplication::closingDown() || !cancelFlag || cancelFlag->load(std::memory_order_relaxed)) {
      return;
    }

    QElapsedTimer timer;
    timer.start();
    const QList<PrecacheResult> results =
        processPrecacheBatch(artworkPaths, *cancelFlag, cacheManager);
    const qint64 elapsedMs = timer.elapsed();

    if (QApplication::closingDown() || !cancelFlag || cancelFlag->load(std::memory_order_relaxed)) {
      return;
    }
    if (!appReceiver) {
      return;
    }

    QMetaObject::invokeMethod(
        appReceiver,
        [self, artworkPaths, results, cancelFlag, batchItemCount, elapsedMs]() {
          if (QApplication::closingDown() || !self || !cancelFlag ||
              cancelFlag->load(std::memory_order_relaxed)) {
            return;
          }

          if (lcArtworkManager().isDebugEnabled()) {
            int diskHits = 0;
            for (const auto &r : results) {
              if (r.loadedFromDiskCache) {
                ++diskHits;
              }
            }
            qCDebug(lcArtworkManager)
                << "Artwork precache batch done"
                << "requested=" << batchItemCount << "produced=" << results.size()
                << "diskHits=" << diskHits << "elapsedMs=" << elapsedMs;
          }

          // Always clear pending entries, even if decode failed, so future
          // silent load passes can retry.
          {
            QMutexLocker locker(&self->m_dataMutex);
            for (const QString &p : artworkPaths) {
              self->m_silentPendingPaths.remove(p);
            }
          }

          for (const auto &r : results) {
            if (r.image.isNull() || r.artworkPath.isEmpty()) {
              continue;
            }
            QPixmap pixmap = QPixmap::fromImage(r.image);
            if (pixmap.isNull()) {
              continue;
            }
            pixmap.setDevicePixelRatio(r.image.devicePixelRatio());

            if (self->m_cacheManager) {
              if (r.loadedFromDiskCache) {
                self->m_cacheManager->cacheArtworkInMemoryOnly(r.artworkPath, pixmap);
              } else {
                self->m_cacheManager->cacheArtwork(r.artworkPath, pixmap);
              }
            }
            {
              QMutexLocker locker(&self->m_dataMutex);
              self->m_silentlyCachedPaths.insert(r.artworkPath);
            }
          }

          // Record batch completion for cooldown enforcement
          self->m_lastBatchCompletionTime.store(QDateTime::currentMSecsSinceEpoch());
        },
        Qt::QueuedConnection);
  });

  {
    QMutexLocker futureLock(&m_futureMutex);
    m_futures.append(future);
    pruneFinishedFutures();
  }
}

// Applies processed artwork results to UI widgets on the GUI thread.
void ArtworkManager::applyResultsToUi(const QList<ArtworkInfo::Result> &batchResults) {
  for (const auto &result : batchResults) {
    if (result.widget.isNull() || result.image.isNull()) {
      continue;
    }
    QPixmap pixmap = QPixmap::fromImage(result.image);
    if (pixmap.isNull()) {
      continue;
    }
    // Ensure DPR is preserved from the source image for HiDPI rendering
    pixmap.setDevicePixelRatio(result.image.devicePixelRatio());
    ItemWidget *const widget = result.widget.data();
    if (!widget) {
      continue;
    }

    // Verify the widget's current identity still matches the artwork being
    // delivered. Widgets are pooled and recycled across roles (item ↔
    // subcollection ↔ virtual folder); without this check, an in-flight
    // artwork load queued for the previous role can clobber the new role's
    // pixmap (bd: subcollection tiles displaying item artwork).
    //   - Items:           identity = file path basename
    //   - Subcollections:  identity = subcollection name (== itemName)
    //   - Virtual folders: identity = folder display name (== itemName)
    // All three lookups go through ArtworkUtils::findArtworkForFile() which
    // matches on basename, so basename equality is the correct stale check.
    QString widgetBaseName;
    if (widget->isSubcollection() || widget->isVirtualFolder()) {
      widgetBaseName = widget->getItemName();
    } else {
      const QString widgetFilePath = widget->getFilePath();
      if (widgetFilePath.isEmpty()) {
        // Widget has no file path (placeholder or reset) - skip stale artwork
        continue;
      }
      widgetBaseName = QFileInfo(widgetFilePath).completeBaseName();
    }
    if (widgetBaseName.isEmpty()) {
      // Widget identity not yet established - skip stale artwork
      continue;
    }
    const QString artworkBaseName = QFileInfo(result.artworkPath).completeBaseName();
    if (widgetBaseName != artworkBaseName) {
      // Widget has been recycled to a different identity, skip stale artwork
      continue;
    }

    {
      QMutexLocker locker(&m_dataMutex);
      widgetToArtworkPath[widget] = result.artworkPath;
      if (!loadedArtwork.contains(widget)) {
        loadedArtwork.append(widget);
      }
    }
    trackWidget(widget);
    if (m_cacheManager) {
      if (result.loadedFromDiskCache) {
        // Avoid re-writing an already-persisted cache entry.
        m_cacheManager->cacheArtworkInMemoryOnly(result.artworkPath, pixmap);
      } else {
        m_cacheManager->cacheArtwork(result.artworkPath, pixmap);
      }
    }
    if (!QApplication::closingDown()) {
      widget->setArtworkPixmap(pixmap);
      widget->update();
    }
  }
}

// Parallel artwork loading: avoid QPixmapCache insertions and cache only via
// CacheManager. For small uncached item counts, loads synchronously on main
// thread to avoid thread dispatch overhead.
void ArtworkManager::loadArtworkParallel(const QList<ArtworkInfo> &items, bool highPriority,
                                         int customBatchSize) {
  if (QApplication::closingDown()) {
    return;
  }
  if (items.isEmpty() || shouldSkipArtworkLoading()) {
    return;
  }

  QElapsedTimer perfTimer;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    perfTimer.start();
  }

  const int batchSize = determineBatchSize(highPriority, customBatchSize, m_adaptiveBatcher);
  QList<ArtworkInfo> uncachedItems;
  uncachedItems.reserve(items.size());
  collectUncachedAndApplyCached(items, uncachedItems);
  if (QApplication::closingDown()) {
    return;
  }

  qint64 afterCollect = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;
  int batchCount = 0;

  for (int i = 0; i < uncachedItems.size(); i += batchSize) {
    if (QApplication::closingDown()) {
      break;
    }
    int end = qMin(i + batchSize, uncachedItems.size());
    QList<ArtworkInfo> batch = uncachedItems.mid(i, end - i);
    dispatchAndTrackBatch(batch, highPriority);
    ++batchCount;
  }

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") && perfTimer.elapsed() > 5) {
    qCDebug(lcPerfTrace) << "loadArtworkParallel: totalMs=" << perfTimer.elapsed()
                         << "collectMs=" << afterCollect << "items=" << items.size()
                         << "uncached=" << uncachedItems.size() << "batches=" << batchCount;
  }
}
