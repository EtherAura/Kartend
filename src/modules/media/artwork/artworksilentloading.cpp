// Silent (background) artwork loading methods for ArtworkManager.
// Coordinates the idle-time precache state machine that walks the active
// collection's full artwork list — driven by m_silentLoadTimer (continuous,
// idle-throttled bursts) and m_persistentLoadTimer (slow background drip).
// Path discovery + cursor + silent-cached / silent-pending sets live in
// m_pathCatalog (ArtworkPathCatalog); this file owns only the timing,
// activity-gating, and dispatch glue.
#include "artworkloaddispatcher.h"
#include "artworkmanager.h"
#include "artworkpathcatalog.h"
#include "artworkutils.h"
#include "artworkwidgetregistry.h"
#include "cachemanager.h"
#include "loggingcategories.h"
#include "uiconstants/artwork.h"
#include "uiconstants/timing.h"

#include <QApplication>
#include <QDateTime>
#include <QMutexLocker>
#include <QPixmap>
#include <QPointer>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>

// Starts early dentry prewarm for a collection BEFORE items are loaded.
// This warms the OS filesystem cache so artwork lookups are fast when widgets
// appear.
void ArtworkManager::startEarlyDentryPrewarm(int collectionIndex) {
  if (!collections || collectionIndex < 0 || collectionIndex >= collections->size()) {
    return;
  }
  const CollectionConfig &collection = (*collections)[collectionIndex];
  if (!collection.showAllSubcollectionItems) {
    return; // Only needed for flattened subcollection views
  }

  const QStringList dirList = ArtworkPathCatalog::collectArtworkDirs(collections, collectionIndex,
                                                                     /*includeDescendants=*/true);
  if (dirList.isEmpty()) {
    return;
  }

  // Kartend-uzs42: capped at one in-flight prewarm so a burst of early-load
  // calls / collection switches doesn't pile up redundant global-pool walks.
  ArtworkUtils::DirectoryCache::instance().schedulePrewarm(dirList);

  qCDebug(lcPerfTrace) << "Started early dentry prewarm: dirs=" << dirList.size();
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
  if (collection.artworkDirectory.isEmpty()) {
    m_silentLoadingActive = false;
    return;
  }

  // Kartend-cl86n: the catalog build (parallel directory enumeration) used to
  // run synchronously here, blocking the GUI thread on a collection switch over
  // cold storage. It now runs off-thread; the silent-load timers start in
  // onCatalogBuilt once the path list is ready. Connect the watcher once.
  if (!m_catalogWatcherInited) {
    connect(&m_catalogBuildWatcher, &QFutureWatcher<void>::finished, this,
            &ArtworkManager::onCatalogBuilt);
    m_catalogWatcherInited = true;
  }
  // setFuture supersedes any in-flight build: we stop watching the old future
  // (so its finished won't fire onCatalogBuilt) and the catalog's generation
  // guard drops the superseded build's stale appends.
  m_catalogBuildWatcher.setFuture(
      m_pathCatalog.buildFromCollection(collections, *currentCollectionIndex));
}

// Kartend-cl86n: GUI-thread continuation once the off-thread catalog build
// finishes. An empty catalog means nothing to silent-load; otherwise start the
// continuous + persistent silent-load timers that walk the path list.
void ArtworkManager::onCatalogBuilt() {
  if (m_pathCatalog.isEmpty()) {
    m_silentLoadingActive = false;
    return;
  }

  if (!m_silentLoadTimer.isActive()) {
    m_silentLoadTimer.start();
  }

  if (!m_persistentLoadTimerInited) {
    m_persistentLoadTimer.setSingleShot(false);
    m_persistentLoadTimer.setInterval(UIConstants::Artwork::PERSISTENT_SILENT_LOAD_INTERVAL_MS);
    connect(&m_persistentLoadTimer, &QTimer::timeout, this,
            &ArtworkManager::processPersistentSilentLoad);
    m_persistentLoadTimerInited = true;
  }

  m_persistentSilentLoad = true;
  m_persistentLoadTimer.start();
}

// Stops silent loading and clears pending state
void ArtworkManager::stopSilentLoading() {
  m_silentLoadingActive = false;
  m_continuousSilentLoad = false;

  if (m_silentLoadTimer.isActive()) {
    m_silentLoadTimer.stop();
  }
  if (m_persistentLoadTimer.isActive()) {
    m_persistentLoadTimer.stop();
  }
  m_persistentSilentLoad = false;

  m_widgetRegistry->clearPendingOnly();
  m_pathCatalog.clearPathsAndPending();
}

namespace {
constexpr int kMaxConcurrentSilentBatches = 2;
} // namespace

// Cooldown gate shared by both silent-loading entry points: returns true when
// the caller should bail this tick because the previous batch hasn't had
// enough idle time since completing. Exposed as a pure static (clock injected)
// so the gating decision is directly testable.
auto ArtworkManager::silentLoadInCooldown(qint64 lastBatchCompletionTime, qint64 nowMs) -> bool {
  if (lastBatchCompletionTime <= 0) {
    return false;
  }
  return (nowMs - lastBatchCompletionTime) < UIConstants::Artwork::SILENT_LOAD_COOLDOWN_MS;
}

auto ArtworkManager::continuousSilentBatchSize(bool userIdle, int baseBatchSize) -> int {
  return userIdle ? baseBatchSize
                  : qMax(1, baseBatchSize / UIConstants::Artwork::SILENT_LOAD_THROTTLE_DIVISOR);
}

auto ArtworkManager::persistentSilentBatchSize(bool userIdle) -> int {
  return userIdle ? UIConstants::Artwork::PERSISTENT_SILENT_BATCH_IDLE
                  : UIConstants::Artwork::PERSISTENT_SILENT_BATCH_ACTIVE;
}

void ArtworkManager::onSilentPrecacheBatchComplete(const QStringList &requestedPaths,
                                                   const QList<ArtworkPrecacheResult> &results) {
  // Always clear pending entries — even on decode failure — so a future
  // silent-load pass can retry.
  for (const QString &p : requestedPaths) {
    m_pathCatalog.unmarkSilentPending(p);
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
    if (auto *cache = cacheMgr()) {
      if (r.loadedFromDiskCache) {
        cache->cacheArtworkInMemoryOnly(r.artworkPath, pixmap);
      } else {
        cache->cacheArtwork(r.artworkPath, pixmap);
      }
    }
    m_pathCatalog.markSilentlyCached(r.artworkPath);
  }
  m_lastBatchCompletionTime.store(QDateTime::currentMSecsSinceEpoch());
}

// Performs persistent low-frequency caching over the artwork list
void ArtworkManager::processPersistentSilentLoad() {
  if (!m_persistentSilentLoad || m_pathCatalog.isExhausted()) {
    m_persistentLoadTimer.stop();
    m_persistentSilentLoad = false;
    return;
  }

  if (silentLoadInCooldown(m_lastBatchCompletionTime.load(), QDateTime::currentMSecsSinceEpoch())) {
    return;
  }
  // Throttle: skip this tick if too many batches are already in-flight to
  // keep CPU usage from spiking during background precache.
  if (m_dispatcher->runningFutureCount() >= kMaxConcurrentSilentBatches) {
    return;
  }
  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  if (QApplication::closingDown()) {
    return;
  }

  const int batchSize = persistentSilentBatchSize(isUserIdle());
  const QStringList batch = m_pathCatalog.takeNextBatch(batchSize);
  QStringList toPrecache = m_pathCatalog.filterAndMarkPending(batch);
  QPointer<ArtworkManager> guard(this);
  m_dispatcher->dispatchPrecacheBatch(
      std::move(toPrecache),
      [guard](const QStringList &requestedPaths, const QList<ArtworkPrecacheResult> &results,
              int /*requestedCount*/, qint64 /*elapsedMs*/) {
        if (guard) {
          guard->onSilentPrecacheBatchComplete(requestedPaths, results);
        }
      });

  // Restart the continuous timer if the user is now idle and there's still
  // work to do — the persistent timer keeps drilling, but the continuous
  // timer is what drives faster bursts when we have idle CPU.
  if (!m_silentLoadingActive && isUserIdle() && !m_pathCatalog.isExhausted()) {
    m_silentLoadingActive = true;
    m_continuousSilentLoad = true;
    if (!m_silentLoadTimer.isActive()) {
      m_silentLoadTimer.start();
    }
  }
}

// Performs continuous caching bursts based on user idleness
void ArtworkManager::processContinuousSilentLoad() {
  if (m_persistentSilentLoad) {
    return;
  }

  if (silentLoadInCooldown(m_lastBatchCompletionTime.load(), QDateTime::currentMSecsSinceEpoch())) {
    return;
  }
  if (m_dispatcher->runningFutureCount() >= kMaxConcurrentSilentBatches) {
    return;
  }
  if (!m_continuousSilentLoad || m_pathCatalog.isExhausted()) {
    stopSilentLoading();
    return;
  }
  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  if (QApplication::closingDown()) {
    return;
  }

  const int batchSize = continuousSilentBatchSize(isUserIdle(), m_silentLoadBatchSize);
  const QStringList batch = m_pathCatalog.takeNextBatch(batchSize);

  if (!m_continuousSilentLoad) {
    return;
  }
  QStringList toPrecache = m_pathCatalog.filterAndMarkPending(batch);
  QPointer<ArtworkManager> guard(this);
  m_dispatcher->dispatchPrecacheBatch(
      std::move(toPrecache),
      [guard](const QStringList &requestedPaths, const QList<ArtworkPrecacheResult> &results,
              int /*requestedCount*/, qint64 /*elapsedMs*/) {
        if (guard) {
          guard->onSilentPrecacheBatchComplete(requestedPaths, results);
        }
      });

  if (isUserIdle()) {
    m_silentLoadTimer.setInterval(UIConstants::Artwork::SILENT_LOAD_INTERVAL_MS);
  } else {
    m_silentLoadTimer.setInterval(UIConstants::Timing::LONG_DELAY_MS);
  }
}

// Records last user activity time
void ArtworkManager::updateUserActivity() {
  m_lastUserActivity.store(QDateTime::currentMSecsSinceEpoch());
}

// Returns whether user is idle
auto ArtworkManager::isUserIdle() const -> bool {
  qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
  return (currentTime - m_lastUserActivity.load()) >= UIConstants::Timing::USER_IDLE_THRESHOLD_MS;
}
