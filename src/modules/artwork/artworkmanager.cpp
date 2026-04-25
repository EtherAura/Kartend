// Handles async artwork loading with QtConcurrent, caching, and viewport-aware
// prioritization.
#include "artworkmanager.h"
#include "applicationcontext.h"
#include "artworkutils.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "extensionutils.h"
#include "interactionstateholder.h"
#include "loggingcategories.h"
#include "propertyutils.h"
#include "setuputils.h"
#include "timerutils.h"
#include "ui/widgets/itemwidget.h"
#include "uiconstants.h"

#include <algorithm>
#include <functional>
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
#include <QtConcurrent>
#include <QTextStream>
#include <QThread>
#include <QTimer>

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
  vps.extended =
      viewport.adjusted(-viewport.width(), -viewport.height(), viewport.width(), viewport.height())
          .translated(scrollOffset);
  return vps;
}

// Partitions pending items into immediate/extended/remaining by viewport
auto partitionByViewport(const QList<ArtworkInfo> &localPending, QWidget *grid,
                         const Viewports &vps, const std::function<bool(ItemWidget *)> &isLoaded)
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
  if (cacheSize <= static_cast<qint64>(lastSaveSize * UIConstants::Cache::SAVE_GROWTH_FACTOR)) {
    return;
  }
  lastSaveSize = cacheSize;
  Q_UNUSED(self)
  // Defer disk cache save to batch multiple cache entries -
  // prevents excessive I/O during rapid artwork loading
  cacheManager->scheduleSaveToDisk(UIConstants::Cache::SAVE_DEFER_MS);
}
} // namespace

// Constructs the artwork manager and sets up timers
ArtworkManager::ArtworkManager(CacheManager *cacheManager, QObject *parent)
    : QObject(parent), m_cacheManager(cacheManager), collections(nullptr),
      currentCollectionIndex(nullptr), stackedWidget(nullptr), itemsPage(nullptr),
      gridContainer(nullptr), m_timerCoordinator(nullptr), m_silentLoadTimer(nullptr),
      m_persistentLoadTimer(nullptr), m_cacheTimer(nullptr), m_silentLoadingActive(false),
      m_silentLoadBatchSize(UIConstants::Artwork::SILENT_LOAD_BATCH_SIZE_DEFAULT),
      m_lastUserActivity{QDateTime::currentMSecsSinceEpoch()}, m_lastBatchCompletionTime{0},
      m_cancellationRequested(std::make_shared<std::atomic<bool>>(false)),
      m_continuousSilentLoad(false), m_silentLoadIndex(0), m_persistentSilentLoad(false),
      m_adaptiveBatcher(AdaptiveBatcher::Config{
          UIConstants::Artwork::BATCH_HIGH, // initialBatchSize
          2,                                // minBatchSize
          30,                               // maxBatchSize
          50,  // targetTimeMs - Target 50ms per batch for responsive UI
          0.3, // smoothingFactor
          10   // historySize
      }) {
  const int idealThreads = QThread::idealThreadCount();
  const int base = idealThreads > 0 ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
                                    : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_artworkThreadPool = new QThreadPool();
  m_artworkThreadPool->setMaxThreadCount(
      std::clamp(base, UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                 UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));

  m_timerCoordinator = new TimerUtils::Coordinator(this);

  m_silentLoadTimer = new QTimer(this);
  m_silentLoadTimer->setSingleShot(false);
  m_silentLoadTimer->setInterval(UIConstants::Artwork::SILENT_LOAD_INTERVAL_MS);
  connect(m_silentLoadTimer, &QTimer::timeout, this, &ArtworkManager::processContinuousSilentLoad);

  m_cacheTimer = new QTimer(this);
  m_cacheTimer->setObjectName("artCacheTimer");
  m_cacheTimer->setInterval(UIConstants::Cache::SAVE_INTERVAL_MS);
  connect(m_cacheTimer, &QTimer::timeout, this, [this]() {
    if (!QApplication::closingDown() && m_cacheManager) {
      m_cacheManager->scheduleSaveToDisk(UIConstants::Cache::QUICK_SAVE_DELAY_MS);
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

  TimerUtils::stopAndDisconnectTimers({m_cacheTimer, m_silentLoadTimer, m_persistentLoadTimer});
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
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QStackedWidget *, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QWidget *, GridContainer, gridContainer)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, QList<CollectionConfig> *, Collections, collections)
SETUP_GETTER_DEF_SAME(ArtworkManagerSetup, int *, CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_CTX_ONLY(ArtworkManagerSetup, InteractionStateHolder *, InteractionState,
                          interactionState)

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
// Checks if artwork loading should be skipped due to shutdown or invalid state
auto ArtworkManager::shouldSkipArtworkLoading() -> bool {
  return QApplication::closingDown() || !stackedWidget ||
         stackedWidget->currentWidget() != itemsPage;
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
  QTimer::singleShot(
      50, this, [this]() { m_cancellationRequested = std::make_shared<std::atomic<bool>>(false); });
}

// Adds pending artwork request, applying deferral logic based on container
// properties
void ArtworkManager::scheduleViewportUpdate() {
  if (m_timerCoordinator) {
    m_timerCoordinator->scheduleViewportUpdate();
  }
}

auto ArtworkManager::getTimerCoordinator() const -> TimerUtils::Coordinator * {
  return m_timerCoordinator;
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
    qCDebug(lcPerfTrace) << "updateViewportArtwork: SUPPRESSED";
    return;
  }

  QList<ArtworkInfo> localPending;
  {
    QMutexLocker locker(&m_dataMutex);
    if (!ui.itemScrollArea || !gridContainer || !stackedWidget ||
        stackedWidget->currentWidget() != itemsPage || pendingArtwork.isEmpty()) {
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

  qint64 afterPartition = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;

  if (!immediateItems.isEmpty()) {
    loadArtworkParallel(immediateItems, true);
  }
  if (!extendedItems.isEmpty()) {
    loadArtworkParallel(extendedItems, true);
  }

  qCDebug(lcPerfTrace) << "updateViewportArtwork: totalMs=" << perfTimer.elapsed()
                       << "partitionMs=" << afterPartition << "pending=" << localPending.size()
                       << "immediate=" << immediateItems.size()
                       << "extended=" << extendedItems.size();
  // Background precaching disabled - only load visible viewport items
  // to minimize CPU usage when idle

  maybeTriggerCacheSave(this, m_cacheManager);
}

// Build artwork path list for current collection (and descendants if enabled)
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
