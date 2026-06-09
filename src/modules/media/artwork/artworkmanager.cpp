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
#include "viewportartworkscheduler.h"

#include <algorithm>
#include <memory>
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

// Constructs the artwork manager and sets up timers. Kartend-davi: dispatcher
// creation moves to setupReferences (the dispatcher needs the cache pointer,
// which now comes from m_ctx).
ArtworkManager::ArtworkManager(QObject *parent)
    : QObject(parent), collections(nullptr), currentCollectionIndex(nullptr),
      stackedWidget(nullptr), itemsPage(nullptr), gridContainer(nullptr),
      m_timerCoordinator(nullptr), m_silentLoadingActive(false),
      m_silentLoadBatchSize(UIConstants::Artwork::SILENT_LOAD_BATCH_SIZE_DEFAULT),
      m_lastUserActivity{QDateTime::currentMSecsSinceEpoch()}, m_lastBatchCompletionTime{0},
      m_continuousSilentLoad(false), m_persistentSilentLoad(false) {
  m_widgetRegistry = new ArtworkWidgetRegistry(this);
  // Kartend-davi: construct with no cache up front so cancelAll paths work
  // even before setupReferences binds the cache via ctx.
  m_dispatcher = new ArtworkLoadDispatcher(nullptr, this);
  m_timerCoordinator = new TimerUtils::Coordinator(this);
  // The viewport load pipeline reaches m_dispatcher / m_widgetRegistry and the
  // suppression / activity / cache hooks back through this manager via
  // friendship, so it can be constructed as soon as those collaborators exist.
  m_viewportScheduler = std::make_unique<ViewportArtworkScheduler>(this);

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

// Kartend-7s2mv: the persistent-cache disk load is owned by ApplicationManager,
// which kicks CacheManager::initialize() onto a background QtConcurrent thread
// during initialize() so startup never blocks on the 50MB+ timestamps parse.
// This method used to also call cache->initialize() synchronously on the main
// thread, which duplicated the parse and raced the background future. It is now
// intentionally a no-op kept as an explicit hook (and to preserve the
// setupReferences-before-this ordering in setupArtworkManager); CacheManager's
// own run-once guard makes any stray call here harmless.
void ArtworkManager::initializeCache() {}

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

// Updates visible widgets' artwork based on viewport and suppression policy.
// The pipeline lives in ViewportArtworkScheduler; this remains the public
// IArtworkManager entry point.
void ArtworkManager::updateViewportArtwork() {
  m_viewportScheduler->updateViewportArtwork();
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

// Parallel artwork loading. The pipeline lives in ViewportArtworkScheduler;
// this remains the public entry point used by the viewport update path and by
// tests.
void ArtworkManager::loadArtworkParallel(const QList<ArtworkInfo> &items, bool highPriority,
                                         int customBatchSize) {
  m_viewportScheduler->loadArtworkParallel(items, highPriority, customBatchSize);
}
