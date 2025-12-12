// Handles async artwork loading with QtConcurrent, caching, and viewport-aware prioritization.
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcArtworkManager, "kartend.artworkmanager")
#define debugLog(msg) qCDebug(lcArtworkManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

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
auto maybeTriggerCacheSave(ArtworkManager *self, CacheManager *cacheManager) -> void {
  static int updateCount = 0;
  if (++updateCount % UIConstants::Cache::CHECK_INTERVAL != 0) {
    return;
  }
  if (!cacheManager) return;
  const qint64 cacheSize = cacheManager->getCacheSize();
  if (cacheSize <= 0) {
    return;
  }
  static qint64 lastSaveSize = 0;
  if (cacheSize <=
      static_cast<qint64>(lastSaveSize *
                          UIConstants::Cache::SAVE_GROWTH_FACTOR)) {
    return;
  }
  lastSaveSize = cacheSize;
  QPointer<ArtworkManager> guard(self);
  // Defer disk cache save to batch multiple cache entries -
  // prevents excessive I/O during rapid artwork loading
  QTimer::singleShot(UIConstants::Cache::SAVE_DEFER_MS, self,
                     [guard, cacheManager]() {
                       if (!guard) {
                         return;
                       }
                       if (!QApplication::closingDown()) {
                         cacheManager->saveToDisk();
                       }
                     });
}
} // namespace

// Scales an image to fit within a square box.
// Accounts for device pixel ratio for crisp HiDPI rendering.
// Does NOT center on a square canvas - the caller handles centering.
static auto scaleCenterToBox(const QImage &img, int targetSize, qreal dpr = 1.0) -> QImage {
  if (img.isNull()) {
    return {};
  }
  // Scale to fit within actual pixel size (targetSize * dpr) for HiDPI crispness
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
  QImage img(path);
  if (img.isNull()) {
    return {};
  }
  // Get device pixel ratio from primary screen for HiDPI scaling
  qreal dpr = 1.0;
  if (QGuiApplication::primaryScreen()) {
    dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
  }
  return scaleCenterToBox(img, UIConstants::Artwork::BOX_SIZE, dpr);
}

// Constructs the artwork manager and sets up timers
ArtworkManager::ArtworkManager(CacheManager *cacheManager, QObject *parent)
    : QObject(parent), m_cacheManager(cacheManager), collections(nullptr), currentCollectionIndex(nullptr),
      stackedWidget(nullptr), itemsPage(nullptr), gridContainer(nullptr),
      m_timerCoordinator(nullptr), m_silentLoadTimer(nullptr),
      m_persistentLoadTimer(nullptr), m_cacheTimer(nullptr),
      m_silentLoadingActive(false),
      m_silentLoadBatchSize(UIConstants::Artwork::SILENT_LOAD_BATCH_SIZE_DEFAULT),
      m_lastUserActivity{QDateTime::currentMSecsSinceEpoch()},
      m_cancellationRequested(std::make_shared<std::atomic<bool>>(false)),
      m_continuousSilentLoad(false), m_silentLoadIndex(0),
      m_persistentSilentLoad(false),
      m_adaptiveBatcher(AdaptiveBatcher::Config{
          UIConstants::Artwork::BATCH_HIGH,  // initialBatchSize
          2,    // minBatchSize
          30,   // maxBatchSize
          50,   // targetTimeMs - Target 50ms per batch for responsive UI
          0.3,  // smoothingFactor
          10    // historySize
      }) {
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
      m_cacheManager->saveToDisk();
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
  // Use mutex to ensure any in-flight operations that didn't see cancellation flag
  // complete safely before we clear the containers
  {
    QMutexLocker locker(&m_dataMutex);
    loadedArtwork.clear();
    widgetToArtworkPath.clear();
    pendingArtwork.clear();
    m_silentlyCachedPaths.clear();
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
  // Skip widget signal blocking during app shutdown - widgets may already be destroyed
  if (!QApplication::closingDown()) {
    for (auto *widget : loadedArtwork) {
      if (widget) {
        widget->blockSignals(true);
      }
    }
  }
  loadedArtwork.clear();
  widgetToArtworkPath.clear();
  pendingArtwork.clear();
  m_silentlyCachedPaths.clear();
  m_allArtworkPaths.clear();
}



// Clears in-memory artwork state for current context
void ArtworkManager::clearLoadedArtworkState() {
  QMutexLocker locker(&m_dataMutex);
  loadedArtwork.clear();
  m_silentlyCachedPaths.clear();
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
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QStackedWidget*, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QWidget*, ItemsPage, itemsPage)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QWidget*, GridContainer, gridContainer)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QScrollArea*, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QList<CollectionConfig>*, Collections, collections)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, int*, CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_CTX_ONLY(ArtworkManagerSetup, InteractionStateHolder*, InteractionState, interactionState)

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
      loadedArtwork.remove(widgetPtr);
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
                  const std::atomic<bool> &cancelled)
    -> QList<ArtworkInfo::Result> {
  QList<ArtworkInfo::Result> results;
  results.reserve(batch.size());

  for (const ArtworkInfo &info : batch) {
    // Check cancellation at start of each iteration
    if (QApplication::closingDown() || cancelled.load(std::memory_order_relaxed)) {
      break;
    }
    if (info.mediaItem.isNull()) {
      continue;
    }
    QImage img = loadAndProcessImage(info.artworkPath);
    
    // Check cancellation again after expensive I/O operation
    // This ensures we exit quickly even if file loading was slow
    if (QApplication::closingDown() || cancelled.load(std::memory_order_relaxed)) {
      break;
    }
    if (img.isNull()) {
      continue;
    }
    results.append(ArtworkInfo::Result{.widget = info.mediaItem,
                                       .artworkPath = info.artworkPath,
                                       .image = img});
  }

  return results;
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
    
    // Virtual folders use folder path for identity, not file path
    // Skip stale-check for virtual folders since their artwork is based on folder name
    if (!widget->isVirtualFolder()) {
      // Verify the widget is still displaying the same file - widget may have been
      // recycled to display a different file while artwork was loading async
      const QString widgetFilePath = widget->getFilePath();
      if (widgetFilePath.isEmpty()) {
        // Widget has no file path (placeholder or reset) - skip stale artwork
        continue;
      }
      
      // Extract base name from both paths for comparison
      const QString widgetBaseName = QFileInfo(widgetFilePath).completeBaseName();
      const QString artworkBaseName = QFileInfo(result.artworkPath).completeBaseName();
      if (widgetBaseName != artworkBaseName) {
        // Widget is now displaying a different file, skip this stale artwork
        continue;
      }
    }
    
    {
      QMutexLocker locker(&m_dataMutex);
      widgetToArtworkPath[widget] = result.artworkPath;
      loadedArtwork.insert(widget);
    }
    trackWidget(widget);
    if (m_cacheManager) {
      m_cacheManager->cacheArtwork(result.artworkPath, pixmap);
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

  const int batchSize = determineBatchSize(highPriority, customBatchSize, m_adaptiveBatcher);
  QList<ArtworkInfo> uncachedItems;
  uncachedItems.reserve(items.size());
  collectUncachedAndApplyCached(items, uncachedItems);
  if (QApplication::closingDown()) {
    return;
  }

  // For small uncached counts with high priority, load synchronously on main
  // thread to avoid QtConcurrent thread dispatch overhead. This provides
  // faster feedback when jumping to a new scroll position.
  constexpr int SYNC_LOAD_THRESHOLD = 6;
  if (highPriority && uncachedItems.size() <= SYNC_LOAD_THRESHOLD) {
    // Dummy cancel flag for synchronous path
    std::atomic<bool> dummyCancelFlag{false};
    QList<ArtworkInfo::Result> results = processBatch(uncachedItems, highPriority, dummyCancelFlag);
    applyResultsToUi(results, highPriority);
    return;
  }

  for (int i = 0; i < uncachedItems.size(); i += batchSize) {
    if (QApplication::closingDown()) {
      break;
    }
    int end = qMin(i + batchSize, uncachedItems.size());
    QList<ArtworkInfo> batch = uncachedItems.mid(i, end - i);
    dispatchAndTrackBatch(batch, highPriority);
  }
}

// Cancels all pending/loaded artwork state (for reload)
void ArtworkManager::cancelAllArtworkLoading() {
  // Set cancellation flag to stop in-flight operations
  if (m_cancellationRequested) {
    m_cancellationRequested->store(true, std::memory_order_relaxed);
  }
  
  {
    QMutexLocker locker(&m_dataMutex);
    loadedArtwork.clear();
    pendingArtwork.clear();
  }
  // Wait 50ms for in-flight QtConcurrent operations to notice the
  // cancellation flag before resetting it for future operations.
  // This delay is chosen to be longer than typical thread scheduling
  // latency but short enough to allow quick successive cancellations.
  QTimer::singleShot(50, this, [this]() {
    if (m_cancellationRequested) {
      m_cancellationRequested->store(false, std::memory_order_relaxed);
    }
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
    if (!existingPath.isEmpty() && existingPath != artworkPath) {
      loadedArtwork.remove(widget);
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
    shouldDefer = (deferAll || gliding || arrowScrolling || userScrolling) && !allowDuringSelection;
  }

  if (shouldDefer) {
    QMutexLocker locker(&m_dataMutex);
    ArtworkInfo info = {.mediaItem = QPointer<ItemWidget>(widget),
                        .artworkPath = artworkPath};
    pendingArtwork.append(info);
    return;
  }

  QPixmap cached = ArtworkManager::getCachedPixmap(artworkPath);
  if (!cached.isNull()) {
    widget->setArtworkPixmap(cached);
    {
      QMutexLocker locker(&m_dataMutex);
      widgetToArtworkPath[widget] = artworkPath;
      loadedArtwork.insert(widget);
    }
    return;
  }

  {
    QMutexLocker locker(&m_dataMutex);
    ArtworkInfo info = {.mediaItem = QPointer<ItemWidget>(widget),
                        .artworkPath = artworkPath};
    pendingArtwork.append(info);
  }

  scheduleViewportUpdate();

  if (!m_silentLoadingActive && isUserIdle()) {
    // Defer silent background loading start to confirm user is actually idle -
    // prevents starting expensive operations during brief interaction pauses
    QTimer::singleShot(UIConstants::Artwork::DEFER_SILENT_LOADING_DELAY_MS, this,
                       [this]() {
                         if (isUserIdle()) {
                           startSilentLoading();
                         }
                       });
  }
}

// Clears all pending artwork entries and loaded state for a widget being
// recycled back to the pool - prevents stale entries from blocking new artwork
void ArtworkManager::clearPendingArtworkForWidget(ItemWidget *widget) {
  if (!widget) {
    return;
  }
  QMutexLocker locker(&m_dataMutex);
  // Remove from loaded tracking so new artwork can be loaded
  loadedArtwork.remove(widget);
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
  QImage centered =
      scaleCenterToBox(originalPixmap.toImage(), UIConstants::Artwork::BOX_SIZE, dpr);
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
  return m_cacheManager->getArtwork(artworkPath);
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
  if (!currentCollectionIndex || *currentCollectionIndex < 0 ||
      !collections ||
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

  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  if (QApplication::closingDown()) {
    return;
  }

  int batchSize = isUserIdle() ? UIConstants::Artwork::PERSISTENT_SILENT_BATCH_IDLE
                               : UIConstants::Artwork::PERSISTENT_SILENT_BATCH_ACTIVE;
  QStringList batch;
  {
    QMutexLocker locker(&m_dataMutex);
    batchSize = qMin(batchSize, m_allArtworkPaths.size() - m_silentLoadIndex);
    for (int i = 0; i < batchSize; ++i) {
      batch.append(m_allArtworkPaths[m_silentLoadIndex++]);
    }
  }

  for (const QString &artworkPath : batch) {
    if (QApplication::closingDown()) {
      break;
    }
    QMutexLocker locker(&m_dataMutex);
    if (m_silentlyCachedPaths.contains(artworkPath)) {
      continue;
    }
    locker.unlock();
    QPixmap pixmap = loadArtworkFromFile(artworkPath);
    if (!pixmap.isNull()) {
      QMutexLocker ilocker(&m_dataMutex);
      m_silentlyCachedPaths.insert(artworkPath);
    }
  }

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
    batchSize = isUserIdle()
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

  for (const QString &artworkPath : batch) {
    if (QApplication::closingDown()) {
      break;
    }
    {
      QMutexLocker locker(&m_dataMutex);
      if (!m_continuousSilentLoad) {
        break;
      }
      if (m_silentlyCachedPaths.contains(artworkPath)) {
        continue;
      }
    }

    QPixmap pixmap = loadArtworkFromFile(artworkPath);
    if (!pixmap.isNull()) {
      QMutexLocker locker(&m_dataMutex);
      m_silentlyCachedPaths.insert(artworkPath);
    }
  }

  if (m_silentLoadTimer) {
    if (isUserIdle()) {
      m_silentLoadTimer->setInterval(UIConstants::Artwork::SILENT_LOAD_INTERVAL_MS);
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

// Updates visible widgets' artwork based on viewport and suppression policy
void ArtworkManager::updateViewportArtwork() {
  if (isArtworkSuppressed()) {
    return;
  }

  QList<ArtworkInfo> localPending;
  {
    QMutexLocker locker(&m_dataMutex);
    if (!ui.itemScrollArea || !gridContainer ||
        !stackedWidget ||
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
    return guard->loadedArtwork.contains(widget);
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

  if (!immediateItems.isEmpty()) {
    loadArtworkParallel(immediateItems, true);
  }
  if (!extendedItems.isEmpty()) {
    loadArtworkParallel(extendedItems, true);
  }

  {
    QMutexLocker locker(&m_dataMutex);
    if (!m_silentLoadingActive) {
      locker.unlock();
      startSilentLoading();
    }
  }

  maybeTriggerCacheSave(this, m_cacheManager);
}

// Build artwork path list for current collection (and descendants if enabled)
void ArtworkManager::buildArtworkPathsList() {
  {
    QMutexLocker locker(&m_dataMutex);
    m_allArtworkPaths.clear();
    m_silentLoadIndex = 0;
  }

  if (!currentCollectionIndex || *currentCollectionIndex < 0 ||
      !collections ||
      *currentCollectionIndex >= collections->size()) {
    return;
  }

  const CollectionConfig &collection = (*collections)[*currentCollectionIndex];
  QSet<QString> processedDirectories;

  appendArtworkFromDir(collection.artworkDirectory, processedDirectories);

  if (collection.showAllSubcollectionItems) {
    for (int i = 0; i < collections->size(); ++i) {
      if ((*collections)[i].parentCollectionIndex == *currentCollectionIndex) {
        appendArtworkFromDir((*collections)[i].artworkDirectory,
                             processedDirectories);
        addSubcollectionArtworkPathsWithDedup(i, processedDirectories);
      }
    }
  }
}

// Recursively add artwork paths from descendant subcollections with
// deduplication
void ArtworkManager::addSubcollectionArtworkPathsWithDedup(
    int parentIndex, QSet<QString> &processedDirectories) {
  if (!collections || parentIndex < 0 ||
      parentIndex >= collections->size()) {
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
    for (auto *widget : loadedArtwork) {
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
    
    // Virtual folders use folder path for identity, not file path
    // Skip stale-check for virtual folders since their artwork is based on folder name
    if (!info.mediaItem->isVirtualFolder()) {
      // Verify the widget is still displaying the same file - widget may have been
      // recycled to display a different file while waiting in queue
      const QString widgetFilePath = info.mediaItem->getFilePath();
      if (widgetFilePath.isEmpty()) {
        // Widget has no file path (placeholder or reset) - skip
        continue;
      }
      
      // Extract base name from both paths for comparison
      const QString widgetBaseName = QFileInfo(widgetFilePath).completeBaseName();
      const QString artworkBaseName = QFileInfo(info.artworkPath).completeBaseName();
      if (widgetBaseName != artworkBaseName) {
        // Widget is now displaying a different file, skip this stale artwork
        continue;
      }
    }
    
    QPixmap cached = ArtworkManager::getCachedPixmap(info.artworkPath);
    if (!cached.isNull()) {
      info.mediaItem->setArtworkPixmap(cached);
      {
        QMutexLocker locker(&m_dataMutex);
        widgetToArtworkPath[info.mediaItem] = info.artworkPath;
        loadedArtwork.insert(info.mediaItem);
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

  // Capture shared cancellation flag for cooperative cancellation.
  // This must remain valid even if ArtworkManager is destroyed while
  // QtConcurrent tasks are still winding down.
  const auto cancelFlag = m_cancellationRequested;
  QPointer<ArtworkManager> self(this);
  QObject *appReceiver = QCoreApplication::instance();
  
  // Start timing for adaptive batching (high-priority only for responsiveness)
  if (highPriority) {
    m_adaptiveBatcher.startBatch();
  }
  int batchItemCount = batch.size();
  
  QFuture<void> future = QtConcurrent::run(
      [self, batch, highPriority, cancelFlag, batchItemCount, appReceiver]() {
    if (QApplication::closingDown() || !cancelFlag ||
        cancelFlag->load(std::memory_order_relaxed)) {
      return;
    }

    QList<ArtworkInfo::Result> results =
        processBatch(batch, highPriority, *cancelFlag);
    if (QApplication::closingDown() || !cancelFlag ||
        cancelFlag->load(std::memory_order_relaxed)) {
      return;
    }

    // Post results back to main thread with timing update.
    // Use the application object as the receiver so the queued functor never
    // targets a potentially-deleted ArtworkManager instance.
    if (!appReceiver) {
      return;
    }
    QMetaObject::invokeMethod(
        appReceiver,
        [self, results, highPriority, batchItemCount, cancelFlag]() {
          if (QApplication::closingDown() || !self || !cancelFlag ||
              cancelFlag->load(std::memory_order_relaxed)) {
            return;
          }
          self->applyResultsToUi(results, highPriority);
          // Update adaptive batcher with completed batch timing (high-priority only)
          if (highPriority && !results.isEmpty()) {
            self->m_adaptiveBatcher.endBatch(batchItemCount);
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

