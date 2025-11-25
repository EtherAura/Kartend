// IWYU-adjusted includes
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionconfig.h"
#include "extensionutils.h"
#include "propertyutils.h"
#include "timerutils.h"
#include "ui/widgets/itemwidget.h"
#include "uiconstants.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>

#include <QtCore/qobjectdefs.h>
#include <qbytearray.h>
#include <qchar.h>
#include <qdatetime.h>
#include <qiodevice.h>
#include <qjsonvalue.h>
#include <qmetatype.h>
#include <qminmax.h>
#include <qnamespace.h>
#include <qpair.h>
#include <qpoint.h>
#include <qstringalgorithms.h>
#include <qtconcurrentrun.h>
#include <qtpreprocessorsupport.h>
#include <qtypeinfo.h>
#include <qvariant.h>
#include <qwidget.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include <iterator>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

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
                         const std::function<bool(MediaItemWidget *)> &isLoaded)
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
  if (++updateCount % UIConstants::PERSISTENT_CACHE_CHECK_INTERVAL != 0) {
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
                          UIConstants::PERSISTENT_CACHE_SAVE_GROWTH_FACTOR)) {
    return;
  }
  lastSaveSize = cacheSize;
  QPointer<ArtworkManager> guard(self);
  QTimer::singleShot(UIConstants::PERSISTENT_CACHE_SAVE_DEFER_MS, self,
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

// Scales an image to fit within a square box and centers it on transparent
// background
static auto scaleCenterToBox(const QImage &img, int targetSize) -> QImage {
  if (img.isNull()) {
    return {};
  }
  QImage scaled = img.scaled(targetSize, targetSize, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
  if (scaled.width() == targetSize && scaled.height() == targetSize) {
    return scaled;
  }
  QImage centered(targetSize, targetSize, QImage::Format_ARGB32_Premultiplied);
  centered.fill(Qt::transparent);
  QPainter painter(&centered);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  const int offsetX = (targetSize - scaled.width()) / 2;
  const int offsetY = (targetSize - scaled.height()) / 2;
  painter.drawImage(offsetX, offsetY, scaled);
  return centered;
}

// Loads an image from disk and returns a centered, scaled image
static auto loadAndProcessImage(const QString &path) -> QImage {
  if (path.isEmpty() || !QFile::exists(path)) {
    return {};
  }
  QImage img(path);
  if (img.isNull()) {
    return {};
  }
  return scaleCenterToBox(img, UIConstants::ARTWORK_BOX);
}

// Constructs the artwork manager and sets up timers
ArtworkManager::ArtworkManager(CacheManager *cacheManager, QObject *parent)
    : QObject(parent), m_cacheManager(cacheManager), collections(nullptr), currentCollectionIndex(nullptr),
      stackedWidget(nullptr), itemsPage(nullptr), gridContainer(nullptr),
      m_timerCoordinator(nullptr), m_silentLoadTimer(nullptr),
      m_persistentLoadTimer(nullptr), m_cacheTimer(nullptr),
      m_silentLoadingActive(false),
      m_silentLoadBatchSize(UIConstants::SILENT_LOAD_BATCH_SIZE_DEFAULT),
      m_lastUserActivity{QDateTime::currentMSecsSinceEpoch()},
      m_continuousSilentLoad(false), m_silentLoadIndex(0),
      m_persistentSilentLoad(false) {
  m_timerCoordinator = new TimerUtils::Coordinator(this);

  m_silentLoadTimer = new QTimer(this);
  m_silentLoadTimer->setSingleShot(false);
  m_silentLoadTimer->setInterval(UIConstants::SILENT_LOAD_INTERVAL);
  connect(m_silentLoadTimer, &QTimer::timeout, this,
          &ArtworkManager::processContinuousSilentLoad);

  m_cacheTimer = new QTimer(this);
  m_cacheTimer->setObjectName("artCacheTimer");
  m_cacheTimer->setInterval(UIConstants::PERSISTENT_CACHE_SAVE_INTERVAL_MS);
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
  TimerUtils::stopAndDisconnectTimers(
      {m_cacheTimer, m_silentLoadTimer, m_persistentLoadTimer});
  if (m_timerCoordinator != nullptr) {
    m_timerCoordinator->stopAllTimers();
    disconnect(m_timerCoordinator, nullptr, nullptr, nullptr);
  }

  {
    QMutexLocker futureLock(&m_futureMutex);
    for (auto &future : m_futures) {
      if (future.isRunning()) {
        future.cancel();
        future.waitForFinished();
      }
    }
    m_futures.clear();
  }

  clearArtworkWidgetState();

  if (m_cacheTimer != nullptr) {
    m_cacheTimer->deleteLater();
    m_cacheTimer = nullptr;
  }
}

// Clears in-memory artwork widget/path/pending/silent cache state (blocks
// widget signals)
void ArtworkManager::clearArtworkWidgetState() {
  for (auto *widget : loadedArtwork) {
    if (widget != nullptr) {
      widget->blockSignals(true);
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
void ArtworkManager::setupReferences(const ArtworkManagerSetup &setup) {
  stackedWidget = setup.stackedWidget;
  itemsPage = setup.itemsPage;
  gridContainer = setup.gridContainer;
  ui.itemScrollArea = setup.itemScrollArea;
  collections = setup.collections;
  currentCollectionIndex = setup.currentCollectionIndex;
}

// Tracks a widget for lifecycle cleanup and de-duplicates tracking via a
// property flag
void ArtworkManager::trackWidget(MediaItemWidget *widget) {
  if (widget == nullptr) {
    return;
  }
  if (!widget->property(PropertyKeys::TrackedByArtwork).toBool()) {
    widget->setProperty(PropertyKeys::TrackedByArtwork, true);
    connect(widget, &QObject::destroyed, this, [this](QObject *obj) {
      if (QApplication::closingDown()) {
        return;
      }
      auto *widgetPtr = qobject_cast<MediaItemWidget *>(obj);
      if (widgetPtr == nullptr) {
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

// Determines the appropriate batch size for artwork loading
auto determineBatchSize(bool highPriority, int customBatchSize) -> int {
  if (customBatchSize > 0) {
    return customBatchSize;
  }
  return highPriority ? UIConstants::ARTWORK_BATCH_HIGH
                      : UIConstants::ARTWORK_BATCH_LOW;
}

// Checks if artwork loading should be skipped due to shutdown or invalid state
auto ArtworkManager::shouldSkipArtworkLoading() -> bool {
  return QApplication::closingDown() || stackedWidget == nullptr ||
         stackedWidget->currentWidget() != itemsPage;
}

// Processes a batch of artwork items in parallel
auto processBatch(const QList<ArtworkInfo> &batch, bool highPriority)
    -> QList<ArtworkInfo::Result> {
  QList<ArtworkInfo::Result> results;
  results.reserve(batch.size());

  for (const ArtworkInfo &info : batch) {
    if (QApplication::closingDown()) {
      break;
    }
    if (info.mediaItem.isNull()) {
      continue;
    }
    QImage img = loadAndProcessImage(info.artworkPath);
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
    MediaItemWidget *const widget = result.widget.data();
    if (widget == nullptr) {
      continue;
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
// CacheManager
void ArtworkManager::loadArtworkParallel(const QList<ArtworkInfo> &items,
                                         bool highPriority,
                                         int customBatchSize) {
  if (QApplication::closingDown()) {
    return;
  }
  if (items.isEmpty() || shouldSkipArtworkLoading()) {
    return;
  }

  const int batchSize = determineBatchSize(highPriority, customBatchSize);
  QList<ArtworkInfo> uncachedItems;
  uncachedItems.reserve(items.size());
  collectUncachedAndApplyCached(items, uncachedItems);
  if (QApplication::closingDown()) {
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
  QMutexLocker locker(&m_dataMutex);
  loadedArtwork.clear();
  pendingArtwork.clear();
}

// Adds pending artwork request, applying deferral logic based on container
// properties
void ArtworkManager::addPendingArtwork(MediaItemWidget *widget,
                                       const QString &artworkPath) {
  if (widget == nullptr || artworkPath.isEmpty()) {
    return;
  }
  if (stackedWidget == nullptr || stackedWidget->currentWidget() != itemsPage) {
    return;
  }

  trackWidget(widget);

  bool shouldDefer = false;
  if (gridContainer != nullptr) {
    const bool deferAll =
        gridContainer->property(PropertyKeys::DeferAllArtwork).toBool();
    const bool gliding =
        gridContainer->property(PropertyKeys::GlideAnimating).toBool();
    const bool arrowScrolling =
        gridContainer->property(PropertyKeys::ArrowKeyScrolling).toBool();
    const bool allowDuringSelection = ui.itemScrollArea != nullptr ? ui.itemScrollArea->property(PropertyKeys::AllowArtworkDuringSelection).toBool() : false;
    shouldDefer = (deferAll || gliding || arrowScrolling) && !allowDuringSelection;
  }

  if (shouldDefer) {
    QMutexLocker locker(&m_dataMutex);
    ArtworkInfo info = {.mediaItem = QPointer<MediaItemWidget>(widget),
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
    ArtworkInfo info = {.mediaItem = QPointer<MediaItemWidget>(widget),
                        .artworkPath = artworkPath};
    pendingArtwork.append(info);
  }

  scheduleViewportUpdate();

  if (!m_silentLoadingActive && isUserIdle()) {
    QTimer::singleShot(UIConstants::DEFER_SILENT_LOADING_DELAY_MS, this,
                       [this]() {
                         if (isUserIdle()) {
                           startSilentLoading();
                         }
                       });
  }
}

// Creates a centered, scaled artwork pixmap from an input pixmap
auto ArtworkManager::createProcessedArtwork(const QPixmap &originalPixmap)
    -> QPixmap {
  if (originalPixmap.isNull()) {
    return {};
  }
  QImage centered =
      scaleCenterToBox(originalPixmap.toImage(), UIConstants::ARTWORK_BOX);
  return centered.isNull() ? QPixmap() : QPixmap::fromImage(centered);
}

auto ArtworkManager::getCachedPixmap(const QString &artworkPath) -> QPixmap {
  if (artworkPath.isEmpty() || !m_cacheManager) {
    return {};
  }
  return m_cacheManager->getArtwork(artworkPath);
}

// Schedules a viewport artwork update via coordinator
void ArtworkManager::scheduleViewportUpdate() {
  if (m_timerCoordinator != nullptr) {
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
  if (stackedWidget == nullptr || stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  preloadArtworkForCollection();
}

// Prepares silent loading list for current collection (and descendants if
// enabled)
void ArtworkManager::preloadArtworkForCollection() {
  if (currentCollectionIndex == nullptr || *currentCollectionIndex < 0 ||
      collections == nullptr ||
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

  if (m_silentLoadTimer != nullptr && !m_silentLoadTimer->isActive()) {
    m_silentLoadTimer->start();
  }

  if (m_persistentLoadTimer == nullptr) {
    m_persistentLoadTimer = new QTimer(this);
    m_persistentLoadTimer->setSingleShot(false);
    m_persistentLoadTimer->setInterval(
        UIConstants::PERSISTENT_SILENT_LOAD_INTERVAL_MS);
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

  if (m_silentLoadTimer != nullptr && m_silentLoadTimer->isActive()) {
    m_silentLoadTimer->stop();
  }
  if (m_persistentLoadTimer != nullptr && m_persistentLoadTimer->isActive()) {
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
      if (m_persistentLoadTimer != nullptr) {
        m_persistentLoadTimer->stop();
      }
      m_persistentSilentLoad = false;
      return;
    }
  }

  if (stackedWidget == nullptr || stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  if (QApplication::closingDown()) {
    return;
  }

  int batchSize = isUserIdle() ? UIConstants::PERSISTENT_SILENT_BATCH_IDLE
                               : UIConstants::PERSISTENT_SILENT_BATCH_ACTIVE;
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
      if (m_silentLoadTimer != nullptr && !m_silentLoadTimer->isActive()) {
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

  if (stackedWidget == nullptr || stackedWidget->currentWidget() != itemsPage) {
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
                                  UIConstants::SILENT_LOAD_THROTTLE_DIVISOR);
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

  if (m_silentLoadTimer != nullptr) {
    if (isUserIdle()) {
      m_silentLoadTimer->setInterval(UIConstants::SILENT_LOAD_INTERVAL);
    } else {
      m_silentLoadTimer->setInterval(UIConstants::LONG_TIMER_DELAY);
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
         UIConstants::USER_IDLE_THRESHOLD_MS;
}

// Updates visible widgets' artwork based on viewport and suppression policy
void ArtworkManager::updateViewportArtwork() {
  if (isArtworkSuppressed()) {
    return;
  }

  QList<ArtworkInfo> localPending;
  {
    QMutexLocker locker(&m_dataMutex);
    if (ui.itemScrollArea == nullptr || gridContainer == nullptr ||
        stackedWidget == nullptr ||
        stackedWidget->currentWidget() != itemsPage ||
        pendingArtwork.isEmpty()) {
      return;
    }
    localPending = pendingArtwork;
  }

  updateUserActivity();

  const Viewports vps = computeViewports(ui.itemScrollArea);
  QPointer<ArtworkManager> guard(this);
  auto isLoaded = [guard](MediaItemWidget *widget) -> bool {
    if (widget == nullptr || !guard) {
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

  if (currentCollectionIndex == nullptr || *currentCollectionIndex < 0 ||
      collections == nullptr ||
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
  if (collections == nullptr || parentIndex < 0 ||
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

// Finds artwork in the given directory for a file by trying baseName and
// fileName with lower/upper-case extensions
auto ArtworkManager::findArtworkForFile(const QString &fileName,
                                        const QString &artworkDirectory)
    -> QString {
  if (fileName.isEmpty() || artworkDirectory.isEmpty()) {
    return {};
  }

  const QString baseName = QFileInfo(fileName).completeBaseName();
  const QString fullName = QFileInfo(fileName).fileName();

  QDir artworkDir(artworkDirectory);
  if (!artworkDir.exists()) {
    return {};
  }

  const QStringList &bases = ExtensionUtils::imageBaseExtensions();

  for (const QString &ext : bases) {
    const QString &lower = ext;
    const QString upper = ext.toUpper();

    QString path = artworkDir.absoluteFilePath(baseName + "." + lower);
    if (QFile::exists(path)) {
      return path;
    }
    path = artworkDir.absoluteFilePath(baseName + "." + upper);
    if (QFile::exists(path)) {
      return path;
    }
  }
  for (const QString &ext : bases) {
    const QString &lower = ext;
    const QString upper = ext.toUpper();

    QString path = artworkDir.absoluteFilePath(fullName + "." + lower);
    if (QFile::exists(path)) {
      return path;
    }
    path = artworkDir.absoluteFilePath(fullName + "." + upper);
    if (QFile::exists(path)) {
      return path;
    }
  }
  return {};
}

// Clears widget references and in-memory state related to artwork loading
void ArtworkManager::clearWidgetReferences() {
  if (m_silentLoadTimer != nullptr) {
    m_silentLoadTimer->stop();
  }
  if (m_persistentLoadTimer != nullptr) {
    m_persistentLoadTimer->stop();
  }

  {
    QMutexLocker locker(&m_dataMutex);
    for (auto *widget : loadedArtwork) {
      if (widget != nullptr) {
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
  if (gridContainer == nullptr) {
    return false;
  }
  const bool gliding =
      gridContainer->property(PropertyKeys::GlideAnimating).toBool();
  const bool arrowScrolling =
      gridContainer->property(PropertyKeys::ArrowKeyScrolling).toBool();
  const bool allowDuringSelection = ui.itemScrollArea != nullptr ? ui.itemScrollArea->property(PropertyKeys::AllowArtworkDuringSelection).toBool() : false;
  return (gliding || arrowScrolling) && !allowDuringSelection;
}

// Filters items that are already cached and applies them immediately
void ArtworkManager::collectUncachedAndApplyCached(
    const QList<ArtworkInfo> &items, QList<ArtworkInfo> &uncachedItems) {
  for (const ArtworkInfo &info : items) {
    if (info.mediaItem.isNull()) {
      continue;
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

  QFuture<void> future = QtConcurrent::run([this, batch, highPriority]() {
    if (QApplication::closingDown()) {
      return;
    }
    QList<ArtworkInfo::Result> results = processBatch(batch, highPriority);
    if (QApplication::closingDown()) {
      return;
    }
    // Post results back to main thread
    QMetaObject::invokeMethod(
        this,
        [this, results, highPriority]() {
          if (!QApplication::closingDown()) {
            applyResultsToUi(results, highPriority);
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

