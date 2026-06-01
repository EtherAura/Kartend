// Handles async artwork loading with QtConcurrent, caching, and viewport-aware
// prioritization.
#include "artworkmanager.h"
#include "applicationcontext.h"
#include "artworkloaddispatcher.h"
#include "artworkutils.h"
#include "artworkwidgetregistry.h"
#include "cachemanager.h"
#include "collection/collectionconfig.h"
#include "extensionutils.h"
#include "icachemanager.h"
#include "interactionstateholder.h"
#include "itemartwork.h"
#include "itemwidget.h"
#include "loggingcategories.h"
#include "propertyutils.h"
#include "setuputils.h"
#include "threadpoolutils.h"
#include "timerutils.h"
#include "uiconstants/artwork.h"
#include "uiconstants/cache.h"

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

} // namespace

// Periodically schedules a deferred persistent cache save once the cache has
// grown enough. Per-instance counters (Kartend-r2722: were function-local
// statics shared across every ArtworkManager instance and never reset).
void ArtworkManager::maybeTriggerCacheSave(ICacheManager *cacheManager) {
  if (++m_cacheUpdateCount % UIConstants::Cache::CHECK_INTERVAL != 0) {
    return;
  }
  if (!cacheManager) return;
  const qint64 cacheSize = cacheManager->getCacheSize();
  if (cacheSize <= 0) {
    return;
  }
  if (cacheSize <=
      static_cast<qint64>(m_lastCacheSaveSize * UIConstants::Cache::SAVE_GROWTH_FACTOR)) {
    return;
  }
  m_lastCacheSaveSize = cacheSize;
  // Defer disk cache save to batch multiple cache entries -
  // prevents excessive I/O during rapid artwork loading
  cacheManager->scheduleSaveToDisk(UIConstants::Cache::SAVE_DEFER_MS);
}

// Constructs the artwork manager and sets up timers. Kartend-davi: dispatcher
// creation moves to setupReferences (the dispatcher needs the cache pointer,
// which now comes from m_ctx).
ArtworkManager::ArtworkManager(QObject *parent)
    : QObject(parent), collections(nullptr), currentCollectionIndex(nullptr),
      stackedWidget(nullptr), itemsPage(nullptr), gridContainer(nullptr),
      m_timerCoordinator(nullptr), m_silentLoadingActive(false),
      m_silentLoadBatchSize(UIConstants::Artwork::SILENT_LOAD_BATCH_SIZE_DEFAULT),
      m_lastUserActivity{QDateTime::currentMSecsSinceEpoch()}, m_lastBatchCompletionTime{0},
      m_continuousSilentLoad(false), m_persistentSilentLoad(false),
      m_adaptiveBatcher(AdaptiveBatcher::Config{
          UIConstants::Artwork::BATCH_HIGH, // initialBatchSize
          2,                                // minBatchSize
          30,                               // maxBatchSize
          50,  // targetTimeMs - Target 50ms per batch for responsive UI
          0.3, // smoothingFactor
          10   // historySize
      }) {
  m_widgetRegistry = new ArtworkWidgetRegistry(this);
  // Kartend-davi: construct with no cache up front so cancelAll paths work
  // even before setupReferences binds the cache via ctx.
  m_dispatcher = new ArtworkLoadDispatcher(nullptr, this);
  m_timerCoordinator = new TimerUtils::Coordinator(this);

  m_silentLoadTimer.setSingleShot(false);
  m_silentLoadTimer.setInterval(UIConstants::Artwork::SILENT_LOAD_INTERVAL_MS);
  connect(&m_silentLoadTimer, &QTimer::timeout, this, &ArtworkManager::processContinuousSilentLoad);

  m_cacheTimer.setObjectName("artCacheTimer");
  m_cacheTimer.setInterval(UIConstants::Cache::SAVE_INTERVAL_MS);
  connect(&m_cacheTimer, &QTimer::timeout, this, [this]() {
    if (!QApplication::closingDown()) {
      if (auto *cache = cacheMgr()) {
        cache->scheduleSaveToDisk(UIConstants::Cache::QUICK_SAVE_DELAY_MS);
      }
    }
  });
  m_cacheTimer.start();
}

ICacheManager *ArtworkManager::cacheMgr() const {
  return m_ctx ? m_ctx->cacheManager() : nullptr;
}

// Destructor stops timers, cancels in-flight dispatch, and clears widget state.
ArtworkManager::~ArtworkManager() {
  // Kartend-cl86n: the off-thread catalog build task captures &m_pathCatalog,
  // so it must not outlive this manager. Wait for any in-flight build before
  // our members tear down. No-op when no build was ever kicked / already done.
  m_catalogBuildWatcher.waitForFinished();

  // Tell the dispatcher to stop accepting new work; its destructor (run when
  // Qt's parent-child cleanup tears it down below) will drain the pool.
  if (m_dispatcher) {
    m_dispatcher->cancelAll();
  }

  TimerUtils::stopAndDisconnectTimers({&m_cacheTimer, &m_silentLoadTimer, &m_persistentLoadTimer});
  if (m_timerCoordinator) {
    m_timerCoordinator->stopAllTimers();
    disconnect(m_timerCoordinator, nullptr, nullptr, nullptr);
  }

  m_widgetRegistry->clearAll();
  m_pathCatalog.clearAll();

  // m_cacheTimer / m_silentLoadTimer / m_timerCoordinator / m_widgetRegistry
  // / m_dispatcher are all parented to this, so Qt deletes them after we
  // return. The dispatcher's destructor blocks on a bounded pool drain so no
  // worker callback fires after this manager is gone.
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
  if (auto *cache = cacheMgr()) {
    cache->initialize();
  }
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
  // Kartend-davi: ctx must be stashed before the dispatcher is built — the
  // dispatcher reads the cache pointer through it.
  m_ctx = setup.ctx;
  stackedWidget = setup.getStackedWidget();
  itemsPage = setup.getItemsPage();
  gridContainer = setup.getGridContainer();
  m_state = setup.getInteractionState();
  ui.itemScrollArea = setup.getItemScrollArea();
  collections = setup.getCollections();
  currentCollectionIndex = setup.getCurrentCollectionIndex();

  if (m_dispatcher) {
    m_dispatcher->setCacheManager(cacheMgr());
  }
}

// Checks if artwork loading should be skipped due to shutdown or invalid state
auto ArtworkManager::shouldSkipArtworkLoading() -> bool {
  return QApplication::closingDown() || !stackedWidget ||
         stackedWidget->currentWidget() != itemsPage;
}
// Cancels all pending/loaded artwork state (for reload)
void ArtworkManager::cancelAllArtworkLoading() {
  m_dispatcher->cancelAll();
  m_widgetRegistry->clearLoadedAndPending();
  m_pathCatalog.clearSilentPendingOnly();
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
  auto isLoaded = [registry](ItemWidget *widget) -> bool { return registry->isLoaded(widget); };
  QList<ArtworkInfo> immediateItems;
  QList<ArtworkInfo> extendedItems;
  QList<ArtworkInfo> remainingItems;
  std::tie(immediateItems, extendedItems, remainingItems) =
      partitionByViewport(localPending, gridContainer, vps, isLoaded);

  // Kartend-b8qe.3: drop remainingItems (out-of-extended-window) instead of
  // storing them back as pending. Items scrolled out of the extended preload
  // window during this update get cancelled; if the user scrolls back into
  // them, configureArtworkForWidget will re-enqueue via addPendingArtwork on
  // the next widget configure pass. Trading a small redundant enqueue on
  // back-scroll for an unbounded queue under long rapid forward-scroll.
  const int droppedOutOfWindow = remainingItems.size();
  Q_UNUSED(droppedOutOfWindow)

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
                       << "extended=" << extendedItems.size()
                       << "droppedOutOfWindow=" << droppedOutOfWindow;
  // Background precaching disabled - only load visible viewport items
  // to minimize CPU usage when idle

  maybeTriggerCacheSave(cacheMgr());
}

// Build artwork path list for current collection (and descendants if enabled)
void ArtworkManager::clearWidgetReferences() {
  m_silentLoadTimer.stop();
  m_persistentLoadTimer.stop();

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

  // Capture the widget's identity *now* so applyResultsToUi can detect
  // cross-collection recycling that shares a basename (Kartend-j0lb.8).
  // Item widgets carry the absolute file path; subcollection / virtual-folder
  // widgets carry their item name (the only identifier they have).
  QString widgetIdentity;
  if (widget->isSubcollection() || widget->isVirtualFolder()) {
    widgetIdentity = widget->getItemName();
  } else {
    widgetIdentity = widget->getFilePath();
  }

  // Kartend-63wg: snapshot the tile's render spec so the worker can produce the
  // finished card off-thread. An unsized label (not laid out yet) yields an
  // empty size and the worker skips compositing (GUI composites on delivery).
  const ItemWidget::ArtworkRenderSpec spec = widget->artworkRenderSpec();
  m_widgetRegistry->enqueuePending(ArtworkInfo{.mediaItem = QPointer<ItemWidget>(widget),
                                               .artworkPath = artworkPath,
                                               .widgetIdentity = widgetIdentity,
                                               .targetLabelSize = spec.labelSize,
                                               .cornerRadius = spec.cornerRadius,
                                               .backgroundColor = spec.background});

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
  auto *cache = cacheMgr();
  if (artworkPath.isEmpty() || !cache) {
    return {};
  }
  // Avoid UI-thread disk I/O; the disk cache is consulted from worker threads.
  return cache->getArtworkFromMemoryOnly(artworkPath);
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

// ─── Batch-processing pipeline ────────────────────────────────
// Filters items that are already cached and applies them immediately,
// returning the rest for worker dispatch.

namespace {
// Picks the per-batch decode size: caller override wins, else the adaptive
// batcher's current size, halved for low-priority work to leave headroom for
// fast-arriving foreground requests.
auto determineBatchSize(bool highPriority, int customBatchSize, const AdaptiveBatcher &batcher)
    -> int {
  if (customBatchSize > 0) {
    return customBatchSize;
  }
  const int adaptive = batcher.currentBatchSize();
  return highPriority ? adaptive : qMax(2, adaptive / 2);
}
} // namespace

void ArtworkManager::collectUncachedAndApplyCached(const QList<ArtworkInfo> &items,
                                                   QList<ArtworkInfo> &uncachedItems) {
  for (const ArtworkInfo &info : items) {
    if (info.mediaItem.isNull()) {
      continue;
    }

    // Verify the widget's current identity still matches the artwork being
    // delivered. Widgets are pooled and recycled across roles (item ↔
    // subcollection ↔ virtual folder); without this check, a queued artwork
    // load for the previous role can clobber the new role's pixmap.
    QString widgetBaseName;
    if (info.mediaItem->isSubcollection() || info.mediaItem->isVirtualFolder()) {
      widgetBaseName = info.mediaItem->getItemName();
    } else {
      const QString widgetFilePath = info.mediaItem->getFilePath();
      if (widgetFilePath.isEmpty()) {
        continue;
      }
      widgetBaseName = QFileInfo(widgetFilePath).completeBaseName();
    }
    if (widgetBaseName.isEmpty()) {
      continue;
    }
    const QString artworkBaseName = QFileInfo(info.artworkPath).completeBaseName();
    if (widgetBaseName != artworkBaseName) {
      continue;
    }

    QPixmap cached = ArtworkManager::getCachedPixmap(info.artworkPath);
    if (!cached.isNull()) {
      info.mediaItem->setArtworkPixmap(cached);
      m_widgetRegistry->markLoaded(info.mediaItem, info.artworkPath);
      m_widgetRegistry->track(info.mediaItem);
    } else {
      uncachedItems.append(info);
    }
  }
}

// Applies decoded artwork results to UI widgets on the GUI thread (run from
// the dispatcher's main-thread completion handler).
void ArtworkManager::applyResultsToUi(const QList<ArtworkInfo::Result> &batchResults) {
  // Per-batch GUI-thread cost — runs on the dispatcher's main-thread
  // callback. Suspect for vertical-grid scroll jerkiness (Kartend-9q8d):
  // multiple batches can complete in quick succession, each iterating its
  // results to do QPixmap::fromImage + setArtworkPixmap + update().
  // Cumulative GUI-thread time across N batches in a single frame is what
  // the user perceives as a hitch.
  //
  // Round 7 (Kartend-9q8d): isArtworkSuppressed() already short-circuits
  // updateViewportArtwork from dispatching new batches during scroll, but
  // batches dispatched BEFORE the scroll started will still complete async
  // on the worker pool and call back here mid-scroll. Each callback does
  // setArtworkPixmap + update() per item — cheap individually but a steady
  // trickle of micro-paints during a wheel storm. Re-queue the results into
  // m_widgetRegistry's pending list so the next post-scroll
  // updateViewportArtwork picks them up instead, eliminating the trickle.
  if (isArtworkSuppressed()) {
    // Re-queue each result through enqueuePending so its same-(widget,path)
    // coalesce and kMaxPending cap apply. The previous takePending() + append +
    // setPending() wrote straight into the registry, bypassing both — a
    // sustained scroll storm could then push m_pending well past the cap with
    // duplicate (widget,path) entries (Kartend-ghmyu).
    for (const auto &r : batchResults) {
      if (r.widget.isNull()) continue;
      ArtworkInfo info;
      info.mediaItem = r.widget;
      info.artworkPath = r.artworkPath;
      info.widgetIdentity = r.widgetIdentity;
      m_widgetRegistry->enqueuePending(info);
    }
    return;
  }
  QElapsedTimer perfTimer;
  const bool perfTrace = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE");
  if (perfTrace) perfTimer.start();

  // Cap the synchronous work per event-loop tick (Kartend-d3qo). A
  // dispatcher batch of ~30 items used to apply every QPixmap::fromImage
  // + setArtworkPixmap + widget->update() in one tick, and multiple
  // batches landing in quick succession compounded into a visible hitch
  // during scroll. Process up to kMaxPerTick items synchronously and
  // re-queue the rest via QMetaObject::invokeMethod (QueuedConnection)
  // so the next tick can paint between the two halves. The cap counts
  // total iterations (applied + skipped) so a batch full of stale
  // widgets still progresses without a giant 30-item sweep.
  constexpr int kMaxPerTick = 8;
  int applied = 0;
  int skipped = 0;
  int processed = 0;
  for (const auto &result : batchResults) {
    if (processed >= kMaxPerTick) {
      break;
    }
    ++processed;
    if (result.widget.isNull() || result.image.isNull()) {
      ++skipped;
      continue;
    }
    QPixmap pixmap = QPixmap::fromImage(result.image);
    if (pixmap.isNull()) {
      ++skipped;
      continue;
    }
    pixmap.setDevicePixelRatio(result.image.devicePixelRatio());
    ItemWidget *const widget = result.widget.data();
    if (!widget) {
      ++skipped;
      continue;
    }

    // Widgets are pooled / recycled across item / subcollection / virtual
    // folder roles. Without this stale-identity check, an in-flight artwork
    // load queued for the previous role would clobber the new role's pixmap.
    //
    // The strict path (Kartend-j0lb.8): compare the widget's *current* identity
    // against the snapshot captured at dispatch. For item widgets that's the
    // absolute media path, which differs across collections even when two
    // items share a basename (the cross-collection same-basename leak the
    // basename-only check missed during all-collections search). Falls back
    // to the basename comparison when the dispatched ArtworkInfo predates
    // this field — keeps any caller that constructs ArtworkInfo without
    // populating widgetIdentity from a hard skip.
    QString widgetIdentity;
    if (widget->isSubcollection() || widget->isVirtualFolder()) {
      widgetIdentity = widget->getItemName();
    } else {
      widgetIdentity = widget->getFilePath();
    }
    if (widgetIdentity.isEmpty()) {
      ++skipped;
      continue;
    }
    if (!result.widgetIdentity.isEmpty()) {
      if (widgetIdentity != result.widgetIdentity) {
        ++skipped;
        continue;
      }
    } else {
      // Legacy fallback: compare basenames. ArtworkLoadDispatcher precomputes
      // result.artworkBaseName off the GUI thread (artworkloaddispatcher.cpp).
      const QString widgetBaseName = (widget->isSubcollection() || widget->isVirtualFolder())
                                         ? widgetIdentity
                                         : QFileInfo(widgetIdentity).completeBaseName();
      if (widgetBaseName != result.artworkBaseName) {
        ++skipped;
        continue;
      }
    }

    m_widgetRegistry->markLoaded(widget, result.artworkPath);
    m_widgetRegistry->track(widget);
    if (auto *cache = cacheMgr()) {
      if (result.loadedFromDiskCache) {
        cache->cacheArtworkInMemoryOnly(result.artworkPath, pixmap);
      } else {
        cache->cacheArtwork(result.artworkPath, pixmap);
      }
    }
    if (!QApplication::closingDown()) {
      // Kartend-63wg: if the worker composed the final card and the tile is
      // still the size it was composed for, set it straight through (no GUI
      // scale/composite). Otherwise (worker skipped it, or the tile resized
      // mid-flight) fall back to compositing the raw pixmap on the GUI thread.
      if (!result.composedCard.isNull() &&
          result.composedForSize == widget->artworkRenderSpec().labelSize) {
        QPixmap card = QPixmap::fromImage(result.composedCard);
        card.setDevicePixelRatio(result.composedCard.devicePixelRatio());
        widget->setComposedArtwork(card);
      } else {
        widget->setArtworkPixmap(pixmap);
      }
      widget->update();
      ++applied;
    } else {
      ++skipped;
    }
  }

  if (processed < batchResults.size()) {
    // Re-queue the unprocessed tail; the next event-loop tick picks it
    // up. mid() copies the tail into a new QList so the original batch
    // can be released as soon as this call returns.
    QList<ArtworkInfo::Result> remainder = batchResults.mid(processed);
    // Capture a QPointer (mirroring loadArtworkParallel's `self`) so the queued
    // re-dispatch becomes a no-op if this manager is destroyed before the next
    // tick, instead of dereferencing a dangling `this` (Kartend-zl1g).
    QPointer<ArtworkManager> self(this);
    QMetaObject::invokeMethod(
        this,
        [self, remainder]() {
          if (self) {
            self->applyResultsToUi(remainder);
          }
        },
        Qt::QueuedConnection);
  }

  if (perfTrace) {
    qCDebug(lcPerfTrace).nospace()
        << "applyResultsToUi: totalMs=" << perfTimer.elapsed() << " applied=" << applied
        << " skipped=" << skipped << " processed=" << processed << " size=" << batchResults.size();
  }
}

// Parallel artwork loading: split @p items into already-cached (applied
// in-line) and uncached (dispatched to the worker pool in adaptive-sized
// batches).
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

  const qint64 afterCollect =
      qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? perfTimer.elapsed() : 0;
  int batchCount = 0;

  QPointer<ArtworkManager> self(this);
  for (int i = 0; i < uncachedItems.size(); i += batchSize) {
    if (QApplication::closingDown()) {
      break;
    }
    const int end = qMin(i + batchSize, uncachedItems.size());
    QList<ArtworkInfo> batch = uncachedItems.mid(i, end - i);
    m_dispatcher->dispatchBatch(std::move(batch), highPriority,
                                [self](const QList<ArtworkInfo::Result> &results,
                                       int batchItemCount, qint64 elapsedMs, bool wasHighPriority) {
                                  if (!self) {
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
                                        << "priority=" << (wasHighPriority ? "high" : "low")
                                        << "requested=" << batchItemCount
                                        << "produced=" << results.size() << "diskHits=" << diskHits
                                        << "elapsedMs=" << elapsedMs;
                                  }
                                  self->applyResultsToUi(results);
                                  if (wasHighPriority && !results.isEmpty()) {
                                    self->m_adaptiveBatcher.observeBatch(batchItemCount, elapsedMs);
                                  }
                                });
    ++batchCount;
  }

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") && perfTimer.elapsed() > 5) {
    qCDebug(lcPerfTrace) << "loadArtworkParallel: totalMs=" << perfTimer.elapsed()
                         << "collectMs=" << afterCollect << "items=" << items.size()
                         << "uncached=" << uncachedItems.size() << "batches=" << batchCount;
  }
}
