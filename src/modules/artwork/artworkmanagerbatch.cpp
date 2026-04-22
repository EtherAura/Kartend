// Sibling translation unit for ArtworkManager.
// Extracted from artworkmanager.cpp during LOC-reduction refactor.
// These remain ArtworkManager members; this is a translation-unit split.
#include "artworkmanager.h"
#include "applicationcontext.h"
#include "artworkmanagerinternal.h"
#include "artworkutils.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "extensionutils.h"
#include "interactionstateholder.h"
#include "propertyutils.h"
#include "setuputils.h"
#include "timerutils.h"
#include "ui/widgets/itemwidget.h"
#include "uiconstants.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <functional>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcArtworkManager)
#define debugLog(msg) qCDebug(lcArtworkManager) << msg


// Processes a batch of artwork items in parallel (with cancellation support)
auto processBatch(const QList<ArtworkInfo> &batch,
                  [[maybe_unused]] bool highPriority,
                  const std::atomic<bool> &cancelled,
                  CacheManager *cacheManager) -> QList<ArtworkInfo::Result> {
  QList<ArtworkInfo::Result> results;
  results.reserve(batch.size());

  for (const ArtworkInfo &info : batch) {
    // Check cancellation at start of each iteration
    if (QApplication::closingDown() ||
        cancelled.load(std::memory_order_relaxed)) {
      break;
    }
    if (info.mediaItem.isNull()) {
      continue;
    }
    bool loadedFromDiskCache = false;
    QImage img;

    // Prefer disk cache when available (worker-thread safe). This avoids
    // decoding the original artwork file and keeps UI thread free of disk I/O.
    if (cacheManager) {
      img = cacheManager->tryLoadArtworkImageFromDiskCache(info.artworkPath);
      loadedFromDiskCache = !img.isNull();
    }

    if (!loadedFromDiskCache) {
      img = loadAndProcessImage(info.artworkPath);
    }

    // Check cancellation again after expensive I/O operation
    // This ensures we exit quickly even if file loading was slow
    if (QApplication::closingDown() ||
        cancelled.load(std::memory_order_relaxed)) {
      break;
    }
    if (img.isNull()) {
      continue;
    }
    results.append(
        ArtworkInfo::Result{.widget = info.mediaItem,
                            .artworkPath = info.artworkPath,
                            .image = img,
                            .loadedFromDiskCache = loadedFromDiskCache});
  }

  return results;
}
void ArtworkManager::collectUncachedAndApplyCached(
    const QList<ArtworkInfo> &items, QList<ArtworkInfo> &uncachedItems) {
  for (const ArtworkInfo &info : items) {
    if (info.mediaItem.isNull()) {
      continue;
    }

    // Verify the widget's current identity still matches the artwork being
    // delivered. Widgets are pooled and recycled across roles (item ↔
    // subcollection ↔ virtual folder); without this check, a queued artwork
    // load for the previous role can clobber the new role's pixmap
    // (bd Kartend-dxz).
    QString widgetBaseName;
    if (info.mediaItem->isSubcollection() ||
        info.mediaItem->isVirtualFolder()) {
      widgetBaseName = info.mediaItem->getItemName();
    } else {
      const QString widgetFilePath = info.mediaItem->getFilePath();
      if (widgetFilePath.isEmpty()) {
        // Widget has no file path (placeholder or reset) - skip
        continue;
      }
      widgetBaseName = QFileInfo(widgetFilePath).completeBaseName();
    }
    if (widgetBaseName.isEmpty()) {
      // Widget identity not yet established - skip
      continue;
    }
    const QString artworkBaseName =
        QFileInfo(info.artworkPath).completeBaseName();
    if (widgetBaseName != artworkBaseName) {
      // Widget has been recycled to a different identity, skip stale artwork
      continue;
    }

    QPixmap cached = ArtworkManager::getCachedPixmap(info.artworkPath);
    if (!cached.isNull()) {
      info.mediaItem->setArtworkPixmap(cached);
      {
        QMutexLocker locker(&m_dataMutex);
        widgetToArtworkPath[info.mediaItem] = info.artworkPath;
        if (!loadedArtwork.contains(info.mediaItem)) {
          loadedArtwork.append(info.mediaItem);
        }
      }
      trackWidget(info.mediaItem);
    } else {
      uncachedItems.append(info);
    }
  }
}

// Dispatches a batch of artwork loading tasks to QtConcurrent
void ArtworkManager::dispatchAndTrackBatch(const QList<ArtworkInfo> &batch,
                                           bool highPriority) {
  if (batch.isEmpty()) {
    return;
  }

  // CacheManager is owned above ArtworkManager and is expected to outlive it.
  // Capture the raw pointer to avoid accessing ArtworkManager state on workers.
  CacheManager *const cacheManager = m_cacheManager;

  // Capture shared cancellation flag for cooperative cancellation.
  // This must remain valid even if ArtworkManager is destroyed while
  // QtConcurrent tasks are still winding down.
  const auto cancelFlag = m_cancellationRequested;
  QPointer<ArtworkManager> self(this);
  QObject *appReceiver = QCoreApplication::instance();

  int batchItemCount = batch.size();

  if (!m_artworkThreadPool) {
    return;
  }
  QFuture<void> future = QtConcurrent::run(
      m_artworkThreadPool, [self, batch, highPriority, cancelFlag,
                            batchItemCount, appReceiver, cacheManager]() {
        if (QApplication::closingDown() || !cancelFlag ||
            cancelFlag->load(std::memory_order_relaxed)) {
          return;
        }

        QElapsedTimer timer;
        timer.start();
        QList<ArtworkInfo::Result> results =
            processBatch(batch, highPriority, *cancelFlag, cacheManager);
        const qint64 elapsedMs = timer.elapsed();
        if (QApplication::closingDown() || !cancelFlag ||
            cancelFlag->load(std::memory_order_relaxed)) {
          return;
        }

        // Post results back to main thread with timing update.
        // Use the application object as the receiver so the queued functor
        // never targets a potentially-deleted ArtworkManager instance.
        if (!appReceiver) {
          return;
        }
        QMetaObject::invokeMethod(
            appReceiver,
            [self, results, highPriority, batchItemCount, elapsedMs,
             cancelFlag]() {
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
                    << "Artwork batch done"
                    << "priority=" << (highPriority ? "high" : "low")
                    << "requested=" << batchItemCount
                    << "produced=" << results.size() << "diskHits=" << diskHits
                    << "elapsedMs=" << elapsedMs;
              }

              self->applyResultsToUi(results, highPriority);
              // Update adaptive batcher with completed batch timing
              // (high-priority only)
              if (highPriority && !results.isEmpty()) {
                self->m_adaptiveBatcher.observeBatch(batchItemCount, elapsedMs);
              }
            },
            Qt::QueuedConnection);
      });

  {
    QMutexLocker futureLock(&m_futureMutex);
    m_futures.append(future);
    pruneFinishedFutures();
  }
}

// Prunes finished futures from the list
void ArtworkManager::pruneFinishedFutures() {
  for (int i = m_futures.size() - 1; i >= 0; --i) {
    if (m_futures[i].isFinished()) {
      m_futures.removeAt(i);
    }
  }
}
