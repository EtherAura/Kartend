// Handles async artwork loading with QtConcurrent, caching, and viewport-aware
// prioritization.
#include "artworkmanager.h"
#include "applicationcontext.h"
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
Q_LOGGING_CATEGORY(lcArtworkManager, "kartend.artworkmanager")
#define debugLog(msg) qCDebug(lcArtworkManager) << msg

namespace {
// Computes immediate and extended viewports based on a scroll area's current
// position
struct Viewports {
  QRect immediate;
  QRect extended;
};

auto computeViewports(const QScrollArea *scrollArea) -> Viewports {
  const QRect viewport = scrollArea->viewport()->rect();
  const QPoint scrollOffset(scrollArea->horizontalScrollBar()->value(),
                            scrollArea->verticalScrollBar()->value());
  Viewports vps;
  vps.immediate = viewport.translated(scrollOffset);
  vps.extended = viewport
                     .adjusted(-viewport.width(), -viewport.height(),
                               viewport.width(), viewport.height())
                     .translated(scrollOffset);
  return vps;
}

// Partitions pending items into immediate/extended/remaining by viewport
auto partitionByViewport(const QList<ArtworkInfo> &localPending, QWidget *grid,
                         const Viewports &vps,
                         const std::function<bool(ItemWidget *)> &isLoaded)
    -> std::tuple<QList<ArtworkInfo>, QList<ArtworkInfo>, QList<ArtworkInfo>> {
  QList<ArtworkInfo> immediateItems;
  QList<ArtworkInfo> extendedItems;
  QList<ArtworkInfo> remainingItems;

  for (const auto &info : localPending) {
    if (info.mediaItem.isNull() || isLoaded(info.mediaItem.data())) {
      continue;
    }
    const QRect widgetRect(info.mediaItem.data()->mapTo(grid, QPoint(0, 0)),
                           info.mediaItem.data()->size());
    if (vps.immediate.intersects(widgetRect)) {
      immediateItems.append(info);
    } else if (vps.extended.intersects(widgetRect)) {
      extendedItems.append(info);
    } else {
      remainingItems.append(info);
    }
  }

  return {immediateItems, extendedItems, remainingItems};
}

// Periodically triggers a deferred persistent cache save when size grows enough
auto maybeTriggerCacheSave(ArtworkManager *self, CacheManager *cacheManager)
    -> void {
  static int updateCount = 0;
  if (++updateCount % UIConstants::Cache::CHECK_INTERVAL != 0) {
    return;
  }
  if (!cacheManager)
    return;
  const qint64 cacheSize = cacheManager->getCacheSize();
  if (cacheSize <= 0) {
    return;
  }
  static qint64 lastSaveSize = 0;
  if (cacheSize <= static_cast<qint64>(
                       lastSaveSize * UIConstants::Cache::SAVE_GROWTH_FACTOR)) {
    return;
  }
  lastSaveSize = cacheSize;
  Q_UNUSED(self)
  // Defer disk cache save to batch multiple cache entries -
  // prevents excessive I/O during rapid artwork loading
  cacheManager->scheduleSaveToDisk(UIConstants::Cache::SAVE_DEFER_MS);
}
} // namespace

// Scales an image to fit within a square box.
// Accounts for device pixel ratio for crisp HiDPI rendering.
// Does NOT center on a square canvas - the caller handles centering.
static auto scaleCenterToBox(const QImage &img, int targetSize, qreal dpr = 1.0)
    -> QImage {
  if (img.isNull()) {
    return {};
  }
  // Scale to fit within actual pixel size (targetSize * dpr) for HiDPI
  // crispness
  const int actualSize = qRound(targetSize * dpr);
  QImage scaled = img.scaled(actualSize, actualSize, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
  scaled.setDevicePixelRatio(dpr);
  return scaled;
}

// Loads an image from disk and returns a centered, scaled image
// Uses system DPR for HiDPI support
static auto loadAndProcessImage(const QString &path) -> QImage {
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

// Constructs the artwork manager and sets up timers
ArtworkManager::ArtworkManager(CacheManager *cacheManager, QObject *parent)
    : QObject(parent), m_cacheManager(cacheManager), collections(nullptr),
      currentCollectionIndex(nullptr), stackedWidget(nullptr),
      itemsPage(nullptr), gridContainer(nullptr), m_timerCoordinator(nullptr),
      m_silentLoadTimer(nullptr), m_persistentLoadTimer(nullptr),
      m_cacheTimer(nullptr), m_silentLoadingActive(false),
      m_silentLoadBatchSize(
          UIConstants::Artwork::SILENT_LOAD_BATCH_SIZE_DEFAULT),
      m_lastUserActivity{QDateTime::currentMSecsSinceEpoch()},
      m_lastBatchCompletionTime{0},
      m_cancellationRequested(std::make_shared<std::atomic<bool>>(false)),
      m_continuousSilentLoad(false), m_silentLoadIndex(0),
      m_persistentSilentLoad(false),
      m_adaptiveBatcher(AdaptiveBatcher::Config{
          UIConstants::Artwork::BATCH_HIGH, // initialBatchSize
          2,                                // minBatchSize
          30,                               // maxBatchSize
          50,  // targetTimeMs - Target 50ms per batch for responsive UI
          0.3, // smoothingFactor
          10   // historySize
      }) {
  const int idealThreads = QThread::idealThreadCount();
  const int base =
      idealThreads > 0
          ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
          : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_artworkThreadPool = new QThreadPool();
  m_artworkThreadPool->setMaxThreadCount(
      std::clamp(base, UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                 UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));

  m_timerCoordinator = new TimerUtils::Coordinator(this);

  m_silentLoadTimer = new QTimer(this);
  m_silentLoadTimer->setSingleShot(false);
  m_silentLoadTimer->setInterval(UIConstants::Artwork::SILENT_LOAD_INTERVAL_MS);
  connect(m_silentLoadTimer, &QTimer::timeout, this,
          &ArtworkManager::processContinuousSilentLoad);

  m_cacheTimer = new QTimer(this);
  m_cacheTimer->setObjectName("artCacheTimer");
  m_cacheTimer->setInterval(UIConstants::Cache::SAVE_INTERVAL_MS);
  connect(m_cacheTimer, &QTimer::timeout, this, [this]() {
    if (!QApplication::closingDown() && m_cacheManager) {
      m_cacheManager->scheduleSaveToDisk(
          UIConstants::Cache::QUICK_SAVE_DELAY_MS);
    }
  });
  m_cacheTimer->start();
}

// Destructor stops timers, cancels futures, clears widget state, and releases
// GUI pixmap resources
ArtworkManager::~ArtworkManager() {
  // Set cancellation flag first to signal all in-flight operations to stop
  if (m_cancellationRequested) {
    m_cancellationRequested->store(true, std::memory_order_release);
  }

  // Abandon the thread pool without waiting - process is exiting anyway.
  // ~QThreadPool() would block waiting for running tasks to complete.
  if (m_artworkThreadPool) {
    m_artworkThreadPool->clear();
    // Intentionally NOT deleting - that would block. Let OS clean up.
    m_artworkThreadPool = nullptr;
  }

  TimerUtils::stopAndDisconnectTimers(
      {m_cacheTimer, m_silentLoadTimer, m_persistentLoadTimer});
  if (m_timerCoordinator) {
    m_timerCoordinator->stopAllTimers();
    disconnect(m_timerCoordinator, nullptr, nullptr, nullptr);
  }

  {
    QMutexLocker futureLock(&m_futureMutex);
    for (auto &future : m_futures) {
      if (future.isRunning()) {
        future.cancel();
        // Don't wait for futures - they will complete asynchronously.
        // The cancellation flag ensures they exit quickly without
        // performing expensive operations.
      }
    }
    m_futures.clear();
  }

  // During destruction, just clear the containers without touching widgets
  // The widgets are owned by Qt's parent hierarchy and may already be destroyed
  // Use mutex to ensure any in-flight operations that didn't see cancellation
  // flag complete safely before we clear the containers
  {
    QMutexLocker locker(&m_dataMutex);
    loadedArtwork.clear();
    widgetToArtworkPath.clear();
    pendingArtwork.clear();
    m_silentlyCachedPaths.clear();
    m_silentPendingPaths.clear();
    m_allArtworkPaths.clear();
  }

  // Note: m_cacheTimer, m_silentLoadTimer, and m_timerCoordinator are
  // parented to 'this', so Qt will automatically delete them when the
  // ArtworkManager is destroyed. No explicit deletion needed.
}

// Clears in-memory artwork widget/path/pending/silent cache state (blocks
// widget signals)
void ArtworkManager::clearArtworkWidgetState() {
  QMutexLocker locker(&m_dataMutex);
  // Skip widget signal blocking during app shutdown - widgets may already be
  // destroyed
  if (!QApplication::closingDown()) {
    for (const auto &widget : loadedArtwork) {
      if (widget) {
        widget->blockSignals(true);
      }
    }
  }
  loadedArtwork.clear();
  widgetToArtworkPath.clear();
  pendingArtwork.clear();
  m_silentlyCachedPaths.clear();
  m_silentPendingPaths.clear();
  m_allArtworkPaths.clear();
}

// Clears in-memory artwork state for current context
void ArtworkManager::clearLoadedArtworkState() {
  QMutexLocker locker(&m_dataMutex);
  loadedArtwork.clear();
  m_silentlyCachedPaths.clear();
  m_silentPendingPaths.clear();
  m_allArtworkPaths.clear();
  m_silentLoadIndex = 0;
}

// Appends artwork file paths from a directory using centralized filters and
// dedupes directories
void ArtworkManager::appendArtworkFromDir(const QString &dirPath,
                                          QSet<QString> &processedDirectories) {
  if (dirPath.isEmpty()) {
    return;
  }
  const QString normalized = QDir(dirPath).absolutePath();
  if (processedDirectories.contains(normalized)) {
    return;
  }

  QDir dir(dirPath);
  if (!dir.exists()) {
    processedDirectories.insert(normalized);
    return;
  }

  const QStringList exts = ExtensionUtils::imageFilters();
  dir.setNameFilters(exts);
  const QStringList files = dir.entryList(QDir::Files);
  if (!files.isEmpty()) {
    QMutexLocker locker(&m_dataMutex);
    for (const QString &file : files) {
      m_allArtworkPaths.append(dir.absoluteFilePath(file));
    }
  }
  processedDirectories.insert(normalized);
}

// Sets references used by artwork updates and silent loading
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QStackedWidget *, StackedWidget,
                      stackedWidget)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QWidget *, GridContainer,
                      gridContainer)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QScrollArea *, ItemScrollArea,
                      itemScrollArea)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QList<CollectionConfig> *,
                      Collections, collections)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, int *, CurrentCollectionIndex,
                      currentCollectionIndex)
SETUP_GETTER_DEF_CTX_ONLY(ArtworkManagerSetup, InteractionStateHolder *,
                          InteractionState, interactionState)

void ArtworkManager::setupReferences(const ArtworkManagerSetup &setup) {
  stackedWidget = setup.getStackedWidget();
  itemsPage = setup.getItemsPage();
  gridContainer = setup.getGridContainer();
  m_state = setup.getInteractionState();
  ui.itemScrollArea = setup.getItemScrollArea();
  collections = setup.getCollections();
  currentCollectionIndex = setup.getCurrentCollectionIndex();
}

// Tracks a widget for lifecycle cleanup and de-duplicates tracking via a
// property flag
void ArtworkManager::trackWidget(ItemWidget *widget) {
  if (!widget) {
    return;
  }
  if (!widget->property(PropertyKeys::TrackedByArtwork).toBool()) {
    widget->setProperty(PropertyKeys::TrackedByArtwork, true);
    connect(widget, &QObject::destroyed, this, [this](QObject *obj) {
      if (QApplication::closingDown()) {
        return;
      }
      auto *widgetPtr = qobject_cast<ItemWidget *>(obj);
      if (!widgetPtr) {
        return;
      }
      QMutexLocker locker(&m_dataMutex);
      loadedArtwork.removeAll(widgetPtr);
      widgetToArtworkPath.remove(widgetPtr);
      for (int i = pendingArtwork.size() - 1; i >= 0; --i) {
        if (pendingArtwork[i].mediaItem == widgetPtr) {
          pendingArtwork.removeAt(i);
        }
      }
    });
  }
}

// Determines the appropriate batch size for artwork loading.
// Uses adaptive batching when no custom size specified, with high-priority
// using current adaptive size and low-priority using a reduced adaptive size.
auto determineBatchSize(bool highPriority, int customBatchSize,
                        const AdaptiveBatcher &batcher) -> int {
  if (customBatchSize > 0) {
    return customBatchSize;
  }
  // Use adaptive batch size, with low priority getting half the size
  int adaptiveSize = batcher.currentBatchSize();
  return highPriority ? adaptiveSize : qMax(2, adaptiveSize / 2);
}

// Checks if artwork loading should be skipped due to shutdown or invalid state
auto ArtworkManager::shouldSkipArtworkLoading() -> bool {
  return QApplication::closingDown() || !stackedWidget ||
         stackedWidget->currentWidget() != itemsPage;
}

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

struct PrecacheResult {
  QString artworkPath;
  QImage image;
  bool loadedFromDiskCache = false;
};

static auto processPrecacheBatch(const QStringList &paths,
                                 const std::atomic<bool> &cancelled,
                                 CacheManager *cacheManager)
    -> QList<PrecacheResult> {
  QList<PrecacheResult> results;
  results.reserve(paths.size());

  for (const QString &artworkPath : paths) {
    if (QApplication::closingDown() ||
        cancelled.load(std::memory_order_relaxed)) {
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

    if (QApplication::closingDown() ||
        cancelled.load(std::memory_order_relaxed)) {
      break;
    }
    if (img.isNull()) {
      continue;
    }

    results.append(PrecacheResult{.artworkPath = artworkPath,
                                  .image = img,
                                  .loadedFromDiskCache = loadedFromDiskCache});
  }

  return results;
}

void ArtworkManager::dispatchAndTrackPrecacheBatch(
    const QStringList &artworkPaths) {
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
  QFuture<void> future = QtConcurrent::run(
      m_artworkThreadPool, [self, artworkPaths, cancelFlag, appReceiver,
                            cacheManager, batchItemCount]() {
        if (QApplication::closingDown() || !cancelFlag ||
            cancelFlag->load(std::memory_order_relaxed)) {
          return;
        }

        QElapsedTimer timer;
        timer.start();
        const QList<PrecacheResult> results =
            processPrecacheBatch(artworkPaths, *cancelFlag, cacheManager);
        const qint64 elapsedMs = timer.elapsed();

        if (QApplication::closingDown() || !cancelFlag ||
            cancelFlag->load(std::memory_order_relaxed)) {
          return;
        }
        if (!appReceiver) {
          return;
        }

        QMetaObject::invokeMethod(
            appReceiver,
            [self, artworkPaths, results, cancelFlag, batchItemCount,
             elapsedMs]() {
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
                    << "requested=" << batchItemCount
                    << "produced=" << results.size() << "diskHits=" << diskHits
                    << "elapsedMs=" << elapsedMs;
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
                    self->m_cacheManager->cacheArtworkInMemoryOnly(
                        r.artworkPath, pixmap);
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
              self->m_lastBatchCompletionTime.store(
                  QDateTime::currentMSecsSinceEpoch());
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
void ArtworkManager::applyResultsToUi(
    const QList<ArtworkInfo::Result> &batchResults, bool highPriority) {
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
    // pixmap (bd Kartend-dxz: subcollection tiles displaying item artwork).
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
    const QString artworkBaseName =
        QFileInfo(result.artworkPath).completeBaseName();
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
      if (highPriority) {
        widget->repaint();
      }
    }
  }
}
// Parallel artwork loading: avoid QPixmapCache insertions and cache only via
// CacheManager. For small uncached item counts, loads synchronously on main
// thread to avoid thread dispatch overhead.
void ArtworkManager::loadArtworkParallel(const QList<ArtworkInfo> &items,
                                         bool highPriority,
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

  const int batchSize =
      determineBatchSize(highPriority, customBatchSize, m_adaptiveBatcher);
  QList<ArtworkInfo> uncachedItems;
  uncachedItems.reserve(items.size());
  collectUncachedAndApplyCached(items, uncachedItems);
  if (QApplication::closingDown()) {
    return;
  }

  qint64 afterCollect =
      qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;
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

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") &&
      perfTimer.elapsed() > 5) {
    qWarning() << "[PerfTrace] loadArtworkParallel: totalMs="
               << perfTimer.elapsed() << "collectMs=" << afterCollect
               << "items=" << items.size()
               << "uncached=" << uncachedItems.size()
               << "batches=" << batchCount;
  }
}

// Cancels all pending/loaded artwork state (for reload)
void ArtworkManager::cancelAllArtworkLoading() {
  // Set cancellation flag to stop in-flight operations.
  // IMPORTANT: regenerate the token after a short delay so in-flight tasks keep
  // observing a permanently-cancelled token. This prevents a slow I/O task from
  // "resurrecting" after we flip the same atomic back to false.
  const auto token = m_cancellationRequested;
  if (token) {
    token->store(true, std::memory_order_relaxed);
  }

  {
    QMutexLocker locker(&m_dataMutex);
    loadedArtwork.clear();
    pendingArtwork.clear();
    m_silentPendingPaths.clear();
  }
  // Wait 50ms for in-flight QtConcurrent operations to notice the
  // cancellation flag before resetting it for future operations.
  // This delay is chosen to be longer than typical thread scheduling
  // latency but short enough to allow quick successive cancellations.
  QTimer::singleShot(50, this, [this]() {
    m_cancellationRequested = std::make_shared<std::atomic<bool>>(false);
  });
}

// Adds pending artwork request, applying deferral logic based on container
// properties
void ArtworkManager::addPendingArtwork(ItemWidget *widget,
                                       const QString &artworkPath) {
  if (!widget || artworkPath.isEmpty()) {
    return;
  }
  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }

  trackWidget(widget);

  // Clear any stale state from previous widget use (e.g., after pool recycling)
  // This ensures the widget can receive new artwork even if it was previously
  // marked as loaded with different artwork
  {
    QMutexLocker locker(&m_dataMutex);
    const QString existingPath = widgetToArtworkPath.value(widget);

    // Skip if widget already has this exact artwork loaded or pending
    if (existingPath == artworkPath) {
      // Already loaded with same path - nothing to do
      if (loadedArtwork.contains(QPointer<ItemWidget>(widget))) {
        return;
      }
      // Already pending with same path - check if already in queue
      for (const auto &info : pendingArtwork) {
        if (info.mediaItem == widget && info.artworkPath == artworkPath) {
          return; // Already queued
        }
      }
    }

    if (!existingPath.isEmpty() && existingPath != artworkPath) {
      loadedArtwork.removeAll(widget);
      widgetToArtworkPath.remove(widget);
      // Remove any stale pending entries for this widget
      for (int i = pendingArtwork.size() - 1; i >= 0; --i) {
        if (pendingArtwork[i].mediaItem == widget) {
          pendingArtwork.removeAt(i);
        }
      }
    }
  }

  bool shouldDefer = false;
  if (m_state) {
    const bool deferAll = m_state->artwork().deferAllArtwork;
    const bool gliding = m_state->glideAnimating();
    const bool arrowScrolling = m_state->arrow().arrowKeyScrolling;
    const bool userScrolling = m_state->scroll().userScrollActive;
    const bool allowDuringSelection = m_state->artwork().allowDuringSelection;
    shouldDefer = (deferAll || gliding || arrowScrolling || userScrolling) &&
                  !allowDuringSelection;
  }

  // Always check cache first - even when deferring, cached artwork should be
  // applied immediately for responsive scrolling
  QPixmap cached = ArtworkManager::getCachedPixmap(artworkPath);
  if (!cached.isNull()) {
    widget->setArtworkPixmap(cached);
    {
      QMutexLocker locker(&m_dataMutex);
      widgetToArtworkPath[widget] = artworkPath;
      if (!loadedArtwork.contains(widget)) {
        loadedArtwork.append(widget);
      }
    }
    return;
  }

  if (shouldDefer) {
    QMutexLocker locker(&m_dataMutex);
    ArtworkInfo info = {.mediaItem = QPointer<ItemWidget>(widget),
                        .artworkPath = artworkPath};
    pendingArtwork.append(info);
    return;
  }

  {
    QMutexLocker locker(&m_dataMutex);
    ArtworkInfo info = {.mediaItem = QPointer<ItemWidget>(widget),
                        .artworkPath = artworkPath};
    pendingArtwork.append(info);
  }

  scheduleViewportUpdate();

  // Background precaching disabled - only load visible viewport items
  // to minimize CPU usage when idle
}

// Clears all pending artwork entries and loaded state for a widget being
// recycled back to the pool - prevents stale entries from blocking new artwork
void ArtworkManager::clearPendingArtworkForWidget(ItemWidget *widget) {
  if (!widget) {
    return;
  }
  QMutexLocker locker(&m_dataMutex);
  // Remove from loaded tracking so new artwork can be loaded
  loadedArtwork.removeAll(widget);
  widgetToArtworkPath.remove(widget);
  // Remove any pending entries for this widget
  for (int i = pendingArtwork.size() - 1; i >= 0; --i) {
    if (pendingArtwork[i].mediaItem == widget) {
      pendingArtwork.removeAt(i);
    }
  }
}

// Creates a centered, scaled artwork pixmap from an input pixmap
// Uses system DPR for HiDPI support
auto ArtworkManager::createProcessedArtwork(const QPixmap &originalPixmap)
    -> QPixmap {
  if (originalPixmap.isNull()) {
    return {};
  }
  // Get device pixel ratio from primary screen for HiDPI scaling
  qreal dpr = 1.0;
  if (QGuiApplication::primaryScreen()) {
    dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
  }
  QImage centered = scaleCenterToBox(originalPixmap.toImage(),
                                     UIConstants::Artwork::BOX_SIZE, dpr);
  if (centered.isNull()) {
    return {};
  }
  QPixmap result = QPixmap::fromImage(centered);
  result.setDevicePixelRatio(dpr);
  return result;
}

auto ArtworkManager::getCachedPixmap(const QString &artworkPath) -> QPixmap {
  if (artworkPath.isEmpty() || !m_cacheManager) {
    return {};
  }
  // Avoid UI-thread disk I/O; disk cache is consulted from worker threads.
  return m_cacheManager->getArtworkFromMemoryOnly(artworkPath);
}

// Schedules a viewport artwork update via coordinator
void ArtworkManager::scheduleViewportUpdate() {
  if (m_timerCoordinator) {
    m_timerCoordinator->scheduleViewportUpdate();
  }
}

auto ArtworkManager::getTimerCoordinator() const -> TimerUtils::Coordinator * {
  return m_timerCoordinator;
}

// Starts early dentry prewarm for a collection BEFORE items are loaded.
// This warms the OS filesystem cache so artwork lookups are fast when widgets
// appear.
void ArtworkManager::startEarlyDentryPrewarm(int collectionIndex) {
  if (!collections || collectionIndex < 0 ||
      collectionIndex >= collections->size()) {
    return;
  }

  const CollectionConfig &collection = (*collections)[collectionIndex];
  if (!collection.showAllSubcollectionItems) {
    return; // Only needed for flattened subcollection views
  }

  // Collect all artwork directories for this collection and descendants
  QSet<QString> allDirs;
  std::function<void(int)> collectDirsRecursive = [&](int parentIdx) {
    for (int i = 0; i < collections->size(); ++i) {
      if ((*collections)[i].parentCollectionIndex == parentIdx) {
        QString artDir = (*collections)[i].artworkDirectory;
        if (!artDir.isEmpty()) {
          allDirs.insert(QDir(artDir).absolutePath());
        }
        collectDirsRecursive(i);
      }
    }
  };

  // Add main collection directory
  if (!collection.artworkDirectory.isEmpty()) {
    allDirs.insert(QDir(collection.artworkDirectory).absolutePath());
  }

  // Collect subcollection directories
  for (int i = 0; i < collections->size(); ++i) {
    if ((*collections)[i].parentCollectionIndex == collectionIndex) {
      QString artDir = (*collections)[i].artworkDirectory;
      if (!artDir.isEmpty()) {
        allDirs.insert(QDir(artDir).absolutePath());
      }
      collectDirsRecursive(i);
    }
  }

  if (allDirs.isEmpty()) {
    return;
  }

  // Start parallel dentry warmup in background thread pool
  QStringList dirList = allDirs.values();
  QThreadPool::globalInstance()->start([dirList]() {
    auto &cache = ArtworkUtils::DirectoryCache::instance();
    cache.prewarmDirectories(dirList);
    cache.processQueuedDirectories();
    if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
      qWarning() << "[PerfTrace] Early dentry prewarm complete: dirs="
                 << dirList.size();
    }
  });

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    qWarning() << "[PerfTrace] Started early dentry prewarm: dirs="
               << allDirs.size();
  }
}

// Starts silent loading when on items page
void ArtworkManager::startSilentLoading() {
  if (m_silentLoadingActive) {
    return;
  }
  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  preloadArtworkForCollection();
}

// Prepares silent loading list for current collection (and descendants if
// enabled)
void ArtworkManager::preloadArtworkForCollection() {
  if (!currentCollectionIndex || *currentCollectionIndex < 0 || !collections ||
      *currentCollectionIndex >= collections->size()) {
    return;
  }

  m_silentLoadingActive = true;
  m_continuousSilentLoad = true;

  const CollectionConfig &collection = (*collections)[*currentCollectionIndex];
  QString artworkDir = collection.artworkDirectory;

  if (artworkDir.isEmpty()) {
    m_silentLoadingActive = false;
    return;
  }

  buildArtworkPathsList();

  {
    QMutexLocker locker(&m_dataMutex);
    if (m_allArtworkPaths.isEmpty()) {
      m_silentLoadingActive = false;
      return;
    }
  }

  if (m_silentLoadTimer && !m_silentLoadTimer->isActive()) {
    m_silentLoadTimer->start();
  }

  if (!m_persistentLoadTimer) {
    m_persistentLoadTimer = new QTimer(this);
    m_persistentLoadTimer->setSingleShot(false);
    m_persistentLoadTimer->setInterval(
        UIConstants::Artwork::PERSISTENT_SILENT_LOAD_INTERVAL_MS);
    connect(m_persistentLoadTimer, &QTimer::timeout, this,
            &ArtworkManager::processPersistentSilentLoad);
  }

  m_persistentSilentLoad = true;
  m_persistentLoadTimer->start();
}

// Stops silent loading and clears pending state
void ArtworkManager::stopSilentLoading() {
  m_silentLoadingActive = false;
  m_continuousSilentLoad = false;

  if (m_silentLoadTimer && m_silentLoadTimer->isActive()) {
    m_silentLoadTimer->stop();
  }
  if (m_persistentLoadTimer && m_persistentLoadTimer->isActive()) {
    m_persistentLoadTimer->stop();
  }

  m_persistentSilentLoad = false;

  QMutexLocker locker(&m_dataMutex);
  pendingArtwork.clear();
  m_allArtworkPaths.clear();
  m_silentPendingPaths.clear();
  m_silentLoadIndex = 0;
}

// Performs persistent low-frequency caching over the artwork list
void ArtworkManager::processPersistentSilentLoad() {
  {
    QMutexLocker locker(&m_dataMutex);
    if (!m_persistentSilentLoad ||
        m_silentLoadIndex >= m_allArtworkPaths.size()) {
      if (m_persistentLoadTimer) {
        m_persistentLoadTimer->stop();
      }
      m_persistentSilentLoad = false;
      return;
    }
  }

  // Cooldown: wait a minimum time after the last batch completed
  // This gives the CPU actual idle time between batches
  {
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 lastCompletion = m_lastBatchCompletionTime.load();
    if (lastCompletion > 0 &&
        (currentTime - lastCompletion) <
            UIConstants::Artwork::SILENT_LOAD_COOLDOWN_MS) {
      return; // Still in cooldown period
    }
  }

  // Throttle: skip this tick if too many batches are already in-flight
  // This prevents CPU saturation during background precaching
  {
    QMutexLocker locker(&m_futureMutex);
    int runningCount = 0;
    for (const auto &f : m_futures) {
      if (f.isRunning()) {
        ++runningCount;
      }
    }
    constexpr int kMaxConcurrentSilentBatches = 2;
    if (runningCount >= kMaxConcurrentSilentBatches) {
      return; // Wait for existing batches to complete
    }
  }

  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  if (QApplication::closingDown()) {
    return;
  }

  int batchSize = isUserIdle()
                      ? UIConstants::Artwork::PERSISTENT_SILENT_BATCH_IDLE
                      : UIConstants::Artwork::PERSISTENT_SILENT_BATCH_ACTIVE;
  QStringList batch;
  {
    QMutexLocker locker(&m_dataMutex);
    batchSize = qMin(batchSize, m_allArtworkPaths.size() - m_silentLoadIndex);
    for (int i = 0; i < batchSize; ++i) {
      batch.append(m_allArtworkPaths[m_silentLoadIndex++]);
    }
  }

  QStringList toPrecache;
  {
    QMutexLocker locker(&m_dataMutex);
    toPrecache.reserve(batch.size());
    for (const QString &artworkPath : batch) {
      if (m_silentlyCachedPaths.contains(artworkPath) ||
          m_silentPendingPaths.contains(artworkPath)) {
        continue;
      }
      m_silentPendingPaths.insert(artworkPath);
      toPrecache.append(artworkPath);
    }
  }
  dispatchAndTrackPrecacheBatch(toPrecache);

  {
    QMutexLocker locker(&m_dataMutex);
    if (!m_silentLoadingActive && isUserIdle() &&
        m_silentLoadIndex < m_allArtworkPaths.size()) {
      m_silentLoadingActive = true;
      m_continuousSilentLoad = true;
      if (m_silentLoadTimer && !m_silentLoadTimer->isActive()) {
        m_silentLoadTimer->start();
      }
    }
  }
}

// Performs continuous caching bursts based on user idleness
void ArtworkManager::processContinuousSilentLoad() {
  if (m_persistentSilentLoad) {
    return;
  }

  // Cooldown: wait a minimum time after the last batch completed
  // This gives the CPU actual idle time between batches
  {
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 lastCompletion = m_lastBatchCompletionTime.load();
    if (lastCompletion > 0 &&
        (currentTime - lastCompletion) <
            UIConstants::Artwork::SILENT_LOAD_COOLDOWN_MS) {
      return; // Still in cooldown period
    }
  }

  // Throttle: skip this tick if too many batches are already in-flight
  // This prevents CPU saturation during background precaching
  {
    QMutexLocker locker(&m_futureMutex);
    int runningCount = 0;
    for (const auto &f : m_futures) {
      if (f.isRunning()) {
        ++runningCount;
      }
    }
    constexpr int kMaxConcurrentSilentBatches = 2;
    if (runningCount >= kMaxConcurrentSilentBatches) {
      return; // Wait for existing batches to complete
    }
  }

  {
    QMutexLocker locker(&m_dataMutex);
    if (!m_continuousSilentLoad ||
        m_silentLoadIndex >= m_allArtworkPaths.size()) {
      locker.unlock();
      stopSilentLoading();
      return;
    }
  }

  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  if (QApplication::closingDown()) {
    return;
  }

  int batchSize;
  {
    QMutexLocker locker(&m_dataMutex);
    batchSize =
        isUserIdle()
            ? m_silentLoadBatchSize
            : qMax(1, m_silentLoadBatchSize /
                          UIConstants::Artwork::SILENT_LOAD_THROTTLE_DIVISOR);
    batchSize = qMin(batchSize, m_allArtworkPaths.size() - m_silentLoadIndex);
  }

  QStringList batch;
  {
    QMutexLocker locker(&m_dataMutex);
    batch = m_allArtworkPaths.mid(m_silentLoadIndex, batchSize);
    m_silentLoadIndex += batchSize;
  }

  QStringList toPrecache;
  {
    QMutexLocker locker(&m_dataMutex);
    if (!m_continuousSilentLoad) {
      return;
    }
    toPrecache.reserve(batch.size());
    for (const QString &artworkPath : batch) {
      if (m_silentlyCachedPaths.contains(artworkPath) ||
          m_silentPendingPaths.contains(artworkPath)) {
        continue;
      }
      m_silentPendingPaths.insert(artworkPath);
      toPrecache.append(artworkPath);
    }
  }
  dispatchAndTrackPrecacheBatch(toPrecache);

  if (m_silentLoadTimer) {
    if (isUserIdle()) {
      m_silentLoadTimer->setInterval(
          UIConstants::Artwork::SILENT_LOAD_INTERVAL_MS);
    } else {
      m_silentLoadTimer->setInterval(UIConstants::Timing::LONG_DELAY_MS);
    }
  }
}

// Records last user activity time
void ArtworkManager::updateUserActivity() {
  m_lastUserActivity.store(QDateTime::currentMSecsSinceEpoch());
}

// Returns whether user is idle
auto ArtworkManager::isUserIdle() const -> bool {
  qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
  return (currentTime - m_lastUserActivity.load()) >=
         UIConstants::Timing::USER_IDLE_THRESHOLD_MS;
}

// Checks if a widget already has artwork loaded or pending
auto ArtworkManager::hasArtworkForWidget(ItemWidget *widget) const -> bool {
  if (!widget) {
    return false;
  }
  // Check if widget has loaded/pending artwork in our tracking map
  return widgetToArtworkPath.contains(widget);
}

// Updates visible widgets' artwork based on viewport and suppression policy
void ArtworkManager::updateViewportArtwork() {
  QElapsedTimer perfTimer;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    perfTimer.start();
  }

  if (isArtworkSuppressed()) {
    if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
      qWarning() << "[PerfTrace] updateViewportArtwork: SUPPRESSED";
    }
    return;
  }

  QList<ArtworkInfo> localPending;
  {
    QMutexLocker locker(&m_dataMutex);
    if (!ui.itemScrollArea || !gridContainer || !stackedWidget ||
        stackedWidget->currentWidget() != itemsPage ||
        pendingArtwork.isEmpty()) {
      return;
    }
    localPending = pendingArtwork;
  }

  updateUserActivity();

  const Viewports vps = computeViewports(ui.itemScrollArea);
  QPointer<ArtworkManager> guard(this);
  auto isLoaded = [guard](ItemWidget *widget) -> bool {
    if (!widget || !guard) {
      return false;
    }
    QMutexLocker locker(&guard->m_dataMutex);
    return guard->loadedArtwork.contains(QPointer<ItemWidget>(widget));
  };
  QList<ArtworkInfo> immediateItems;
  QList<ArtworkInfo> extendedItems;
  QList<ArtworkInfo> remainingItems;
  std::tie(immediateItems, extendedItems, remainingItems) =
      partitionByViewport(localPending, gridContainer, vps, isLoaded);

  {
    QMutexLocker locker(&m_dataMutex);
    pendingArtwork = remainingItems;
  }

  qint64 afterPartition =
      qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;

  if (!immediateItems.isEmpty()) {
    loadArtworkParallel(immediateItems, true);
  }
  if (!extendedItems.isEmpty()) {
    loadArtworkParallel(extendedItems, true);
  }

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    qWarning() << "[PerfTrace] updateViewportArtwork: totalMs="
               << perfTimer.elapsed() << "partitionMs=" << afterPartition
               << "pending=" << localPending.size()
               << "immediate=" << immediateItems.size()
               << "extended=" << extendedItems.size();
  }

  // Background precaching disabled - only load visible viewport items
  // to minimize CPU usage when idle

  maybeTriggerCacheSave(this, m_cacheManager);
}

// Build artwork path list for current collection (and descendants if enabled)
void ArtworkManager::buildArtworkPathsList() {
  {
    QMutexLocker locker(&m_dataMutex);
    m_allArtworkPaths.clear();
    m_silentLoadIndex = 0;
  }

  if (!currentCollectionIndex || *currentCollectionIndex < 0 || !collections ||
      *currentCollectionIndex >= collections->size()) {
    return;
  }

  const CollectionConfig &collection = (*collections)[*currentCollectionIndex];

  // PHASE 1: Collect all directory paths first (fast, no I/O)
  QSet<QString> processedDirectories;
  std::function<void(int)> collectDirsRecursive = [&](int parentIdx) {
    for (int i = 0; i < collections->size(); ++i) {
      if ((*collections)[i].parentCollectionIndex == parentIdx) {
        QString artDir = (*collections)[i].artworkDirectory;
        if (!artDir.isEmpty()) {
          processedDirectories.insert(QDir(artDir).absolutePath());
        }
        collectDirsRecursive(i);
      }
    }
  };

  // Add main collection directory
  if (!collection.artworkDirectory.isEmpty()) {
    processedDirectories.insert(
        QDir(collection.artworkDirectory).absolutePath());
  }

  if (collection.showAllSubcollectionItems) {
    // Collect all subcollection directories
    for (int i = 0; i < collections->size(); ++i) {
      if ((*collections)[i].parentCollectionIndex == *currentCollectionIndex) {
        QString artDir = (*collections)[i].artworkDirectory;
        if (!artDir.isEmpty()) {
          processedDirectories.insert(QDir(artDir).absolutePath());
        }
        collectDirsRecursive(i);
      }
    }

    // PHASE 2: Start dentry warmup IMMEDIATELY in background (parallel)
    QStringList allDirs = processedDirectories.values();
    QThreadPool::globalInstance()->start([allDirs]() {
      auto &cache = ArtworkUtils::DirectoryCache::instance();
      cache.prewarmDirectories(allDirs);
      cache.processQueuedDirectories();
      if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
        qWarning() << "[PerfTrace] Background dentry warmup complete: dirs="
                   << allDirs.size();
      }
    });

    // PHASE 3: Build artwork paths list in parallel
    // Use QtConcurrent to scan all directories simultaneously
    QMutex pathsMutex;
    QtConcurrent::blockingMap(
        allDirs, [this, &pathsMutex](const QString &dirPath) {
          QDir dir(dirPath);
          if (!dir.exists()) {
            return;
          }
          const QStringList exts = ExtensionUtils::imageFilters();
          dir.setNameFilters(exts);
          const QStringList files = dir.entryList(QDir::Files);
          if (!files.isEmpty()) {
            QStringList fullPaths;
            fullPaths.reserve(files.size());
            for (const QString &file : files) {
              fullPaths.append(dir.absoluteFilePath(file));
            }
            QMutexLocker locker(&pathsMutex);
            QMutexLocker dataLocker(&m_dataMutex);
            m_allArtworkPaths.append(fullPaths);
          }
        });
  } else {
    // Single collection - just scan the one directory
    appendArtworkFromDir(collection.artworkDirectory, processedDirectories);
  }
}

// Recursively add artwork paths from descendant subcollections with
// deduplication
void ArtworkManager::addSubcollectionArtworkPathsWithDedup(
    int parentIndex, QSet<QString> &processedDirectories) {
  if (!collections || parentIndex < 0 || parentIndex >= collections->size()) {
    return;
  }

  for (int i = 0; i < collections->size(); ++i) {
    if ((*collections)[i].parentCollectionIndex == parentIndex) {
      appendArtworkFromDir((*collections)[i].artworkDirectory,
                           processedDirectories);
      addSubcollectionArtworkPathsWithDedup(i, processedDirectories);
    }
  }
}

// Initializes persistent cache from disk
void ArtworkManager::initializeCache() {
  if (m_cacheManager) {
    m_cacheManager->initialize();
  }
}

// Loads artwork, processes it, and caches only in CacheManager to avoid
// duplicate in-memory residency
auto ArtworkManager::loadArtworkFromFile(const QString &artworkPath)
    -> QPixmap {
  QPixmap cached = getCachedPixmap(artworkPath);
  if (!cached.isNull()) {
    return cached;
  }

  if (!QFile::exists(artworkPath)) {
    return {};
  }

  QPixmap pixmap(artworkPath);
  if (pixmap.isNull()) {
    return {};
  }

  QPixmap processedPixmap = ArtworkManager::createProcessedArtwork(pixmap);
  if (!processedPixmap.isNull() && m_cacheManager) {
    m_cacheManager->cacheArtwork(artworkPath, processedPixmap);
  }

  return processedPixmap;
}

// Delegates to ArtworkUtils::findArtworkForFile for artwork path resolution
auto ArtworkManager::findArtworkForFile(const QString &fileName,
                                        const QString &artworkDirectory)
    -> QString {
  return ArtworkUtils::findArtworkForFile(fileName, artworkDirectory);
}

// Clears widget references and in-memory state related to artwork loading
void ArtworkManager::clearWidgetReferences() {
  if (m_silentLoadTimer) {
    m_silentLoadTimer->stop();
  }
  if (m_persistentLoadTimer) {
    m_persistentLoadTimer->stop();
  }

  {
    QMutexLocker locker(&m_dataMutex);
    for (const auto &widget : loadedArtwork) {
      if (widget) {
        widget->blockSignals(true);
      }
    }

    loadedArtwork.clear();
    widgetToArtworkPath.clear();
    pendingArtwork.clear();
    m_silentlyCachedPaths.clear();
    m_allArtworkPaths.clear();

    m_silentLoadIndex = 0;
    m_silentLoadingActive = false;
    m_continuousSilentLoad = false;
    m_persistentSilentLoad = false;
  }
}

// Checks if artwork loading should be suppressed (e.g. during fast scrolling)
auto ArtworkManager::isArtworkSuppressed() const -> bool {
  if (!m_state) {
    return false;
  }
  const bool gliding = m_state->glideAnimating();
  const bool arrowScrolling = m_state->arrow().arrowKeyScrolling;
  const bool allowDuringSelection = m_state->artwork().allowDuringSelection;
  return (gliding || arrowScrolling) && !allowDuringSelection;
}

// Filters items that are already cached and applies them immediately
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
