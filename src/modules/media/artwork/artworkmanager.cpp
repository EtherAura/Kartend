// Handles async artwork loading with QtConcurrent, caching, and viewport-aware
// prioritization.
#include "artworkmanager.h"
#include "applicationcontext.h"
#include "artworkutils.h"
#include "artworkwidgetregistry.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "extensionutils.h"
#include "interactionstateholder.h"
#include "itemartwork.h"
#include "loggingcategories.h"
#include "propertyutils.h"
#include "setuputils.h"
#include "threadpoolutils.h"
#include "timerutils.h"
#include "itemwidget.h"
#include "uiconstants.h"

#include <algorithm>
#include <functional>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
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
      m_continuousSilentLoad(false), m_persistentSilentLoad(false),
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

  m_widgetRegistry = new ArtworkWidgetRegistry(this);
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
  // Set cancellation flag first to signal all in-flight operations to stop.
  // QtConcurrent tasks capture this flag by shared_ptr value (not 'this'),
  // so they remain safe to run after destruction begins; they early-return
  // when the flag is observed.
  if (m_cancellationRequested) {
    m_cancellationRequested->store(true, std::memory_order_release);
  }

  // Bounded-wait teardown: give in-flight tasks a chance to notice
  // cancellation before falling through to the abandon-the-pool fallback.
  // Matches CacheManager's policy and centralizes the precondition (tasks
  // must be cooperatively-cancellable, never capture 'this' raw) at the
  // helper's documentation.
  constexpr int kArtworkPoolDrainMs = 2000;
  if (!ThreadPoolUtils::shutdownWithBudget(m_artworkThreadPool, kArtworkPoolDrainMs)) {
    qCWarning(lcArtworkManager) << "ArtworkManager: artwork thread pool did not drain in"
                                << kArtworkPoolDrainMs
                                << "ms during shutdown; abandoning pool to avoid blocking exit";
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

  // During destruction, just clear the containers without touching widgets.
  // The widgets are owned by Qt's parent hierarchy and may already be
  // destroyed. The registry's internal mutex ensures any in-flight operations
  // that didn't see the cancellation flag complete safely before we clear.
  m_widgetRegistry->clearAll();
  m_pathCatalog.clearAll();

  // Note: m_cacheTimer, m_silentLoadTimer, and m_timerCoordinator are
  // parented to 'this', so Qt will automatically delete them when the
  // ArtworkManager is destroyed. No explicit deletion needed.
}

// Clears in-memory artwork widget/path/pending/silent cache state (blocks
// widget signals)
void ArtworkManager::clearArtworkWidgetState() {
  // Skip widget signal blocking during app shutdown — the widgets may already
  // be destroyed and reaching into them would race with their destructors.
  if (QApplication::closingDown()) {
    m_widgetRegistry->clearAll();
  } else {
    m_widgetRegistry->blockSignalsAndClearAll();
  }
  m_pathCatalog.clearAll();
}

// Clears in-memory artwork state for current context
void ArtworkManager::clearLoadedArtworkState() {
  m_widgetRegistry->clearLoadedOnly();
  m_pathCatalog.clearAll();
}

// Initializes persistent cache from disk
void ArtworkManager::initializeCache() {
  if (m_cacheManager) {
    m_cacheManager->initialize();
  }
}

// Loads artwork, processes it, and caches only in CacheManager to avoid
// duplicate in-memory residency
auto ArtworkManager::loadArtworkFromFile(const QString &artworkPath) -> QPixmap {
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
auto ArtworkManager::findArtworkForFile(const QString &fileName, const QString &artworkDirectory)
    -> QString {
  return ArtworkUtils::findArtworkForFile(fileName, artworkDirectory);
}

// Sets references used by artwork updates and silent loading
SETUP_GETTER_DEF_UI_SAME(ArtworkManagerSetup, QStackedWidget *, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_UI_SAME(ArtworkManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_UI_SAME(ArtworkManagerSetup, QWidget *, GridContainer, gridContainer)
SETUP_GETTER_DEF_UI_SAME(ArtworkManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_COL_SAME(ArtworkManagerSetup, QList<CollectionConfig> *, Collections, collections)
SETUP_GETTER_DEF_COL_SAME(ArtworkManagerSetup, int *, CurrentCollectionIndex,
                          currentCollectionIndex)
SETUP_GETTER_DEF_MGR_CTX_ONLY(ArtworkManagerSetup, InteractionStateHolder *, InteractionState,
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

  m_widgetRegistry->clearLoadedAndPending();
  m_pathCatalog.clearSilentPendingOnly();
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
  return m_widgetRegistry->hasArtworkFor(widget);
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

  if (!ui.itemScrollArea || !gridContainer || !stackedWidget ||
      stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  QList<ArtworkInfo> localPending = m_widgetRegistry->takePending();
  if (localPending.isEmpty()) {
    return;
  }

  updateUserActivity();

  const Viewports vps = computeViewports(ui.itemScrollArea);
  ArtworkWidgetRegistry *registry = m_widgetRegistry;
  auto isLoaded = [registry](ItemWidget *widget) -> bool {
    return registry->isLoaded(widget);
  };
  QList<ArtworkInfo> immediateItems;
  QList<ArtworkInfo> extendedItems;
  QList<ArtworkInfo> remainingItems;
  std::tie(immediateItems, extendedItems, remainingItems) =
      partitionByViewport(localPending, gridContainer, vps, isLoaded);

  m_widgetRegistry->setPending(std::move(remainingItems));

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

  m_widgetRegistry->blockSignalsAndClearAll();
  // drop any per-item artwork-type overrides when widgets are torn down — a
  // fresh collection or post-search rebuild should start every item back on
  // its primary artwork.
  m_widgetRegistry->clearArtworkTypeOverrides();
  m_pathCatalog.clearAll();

  m_silentLoadingActive = false;
  m_continuousSilentLoad = false;
  m_persistentSilentLoad = false;
}

// ─── Per-item artwork-type override ─────────────────────────

QString ArtworkManager::artworkTypeOverrideFor(const QString &fullPath) const {
  return m_widgetRegistry->artworkTypeOverrideFor(fullPath);
}

void ArtworkManager::clearArtworkTypeOverrides() {
  m_widgetRegistry->clearArtworkTypeOverrides();
}

void ArtworkManager::cycleArtworkType(ItemWidget *widget, const QString &fullPath,
                                      int collectionIndex) {
  if (!widget || fullPath.isEmpty() || !collections) {
    return;
  }
  if (collectionIndex < 0 || collectionIndex >= collections->size()) {
    return;
  }
  const QString artworkDir = (*collections)[collectionIndex].artworkDirectory;
  if (artworkDir.isEmpty()) {
    return;
  }

  // Build the cycle list: legacy/primary (empty-string id) + every standard
  // type whose subdirectory has a matching file. Custom types are not
  // included yet — they only resolve via per-item DB overrides which would
  // require an async query and a manual link the user has already created
  // ('s sidebar gallery is the discoverability path for those).
  const QString fileName = QFileInfo(fullPath).fileName();
  const QString baseName = QFileInfo(fullPath).completeBaseName();
  QStringList available;
  if (!ArtworkUtils::findArtworkForFile(fileName, artworkDir).isEmpty()) {
    available.append(QString());
  }
  for (const QString &type : ItemArtworkStore::standardTypes()) {
    if (!ItemArtworkStore::findStandardArtwork(baseName, artworkDir, type).isEmpty()) {
      available.append(type);
    }
  }

  if (available.size() < 2) {
    return;
  }

  const QString currentType = m_widgetRegistry->artworkTypeOverrideFor(fullPath);
  const QString nextType = ArtworkUtils::nextArtworkType(currentType, available);

  QString newArtworkPath;
  if (nextType.isEmpty()) {
    newArtworkPath = ArtworkUtils::findArtworkForFile(fileName, artworkDir);
  } else {
    newArtworkPath = ItemArtworkStore::findStandardArtwork(baseName, artworkDir, nextType);
  }
  if (newArtworkPath.isEmpty()) {
    return;
  }

  m_widgetRegistry->setArtworkTypeOverride(fullPath, nextType);
  addPendingArtwork(widget, newArtworkPath);
}

namespace {
// Scales an image to fit within a square box, accounting for device pixel
// ratio so the result is crisp on HiDPI displays. Does NOT center the image
// onto a square canvas — the caller does that if needed.
auto scaleCenterToBox(const QImage &img, int targetSize, qreal dpr = 1.0) -> QImage {
  if (img.isNull()) {
    return {};
  }
  const int actualSize = qRound(targetSize * dpr);
  QImage scaled = img.scaled(actualSize, actualSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  scaled.setDevicePixelRatio(dpr);
  return scaled;
}
} // namespace

// Adds a pending artwork request, applying deferral logic based on container
// state (active scroll / glide / arrow scroll). Cached artwork is applied
// immediately even when deferred so freshly-recycled tiles never flash empty.
void ArtworkManager::addPendingArtwork(ItemWidget *widget, const QString &artworkPath) {
  if (!widget || artworkPath.isEmpty()) {
    return;
  }
  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }

  m_widgetRegistry->track(widget);

  // Clear any stale state from previous widget use (e.g. after pool
  // recycling). This is what lets a tile receive new artwork even when it
  // was previously marked loaded with a different path.
  const QString existingPath = m_widgetRegistry->pathFor(widget);
  if (existingPath == artworkPath) {
    if (m_widgetRegistry->isLoaded(widget)) {
      return;
    }
    if (m_widgetRegistry->isPendingFor(widget, artworkPath)) {
      return;
    }
  }
  if (!existingPath.isEmpty() && existingPath != artworkPath) {
    m_widgetRegistry->removeAllEntriesFor(widget);
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

  // Always check cache first — even when deferring, cached artwork should be
  // applied immediately so scrolling stays responsive.
  QPixmap cached = ArtworkManager::getCachedPixmap(artworkPath);
  if (!cached.isNull()) {
    widget->setArtworkPixmap(cached);
    m_widgetRegistry->markLoaded(widget, artworkPath);
    return;
  }

  m_widgetRegistry->enqueuePending(
      ArtworkInfo{.mediaItem = QPointer<ItemWidget>(widget), .artworkPath = artworkPath});

  if (!shouldDefer) {
    scheduleViewportUpdate();
  }
}

// Clears all pending artwork entries and loaded state for a widget being
// recycled back to the pool — prevents stale entries from blocking new
// artwork.
void ArtworkManager::clearPendingArtworkForWidget(ItemWidget *widget) {
  m_widgetRegistry->removeAllEntriesFor(widget);
}

// Creates a centered, scaled artwork pixmap from an input pixmap, using the
// system DPR for HiDPI correctness.
auto ArtworkManager::createProcessedArtwork(const QPixmap &originalPixmap) -> QPixmap {
  if (originalPixmap.isNull()) {
    return {};
  }
  qreal dpr = 1.0;
  if (QGuiApplication::primaryScreen()) {
    dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
  }
  QImage centered = scaleCenterToBox(originalPixmap.toImage(), UIConstants::Artwork::BOX_SIZE, dpr);
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
  // Avoid UI-thread disk I/O; the disk cache is consulted from worker threads.
  return m_cacheManager->getArtworkFromMemoryOnly(artworkPath);
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
