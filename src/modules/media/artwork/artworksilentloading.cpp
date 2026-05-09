// Silent (background) artwork loading methods for ArtworkManager.
// Coordinates the idle-time precache state machine that walks the active
// collection's full artwork list — driven by m_silentLoadTimer (continuous,
// idle-throttled bursts) and m_persistentLoadTimer (slow background drip).
// Path discovery + cursor + silent-cached / silent-pending sets live in
// m_pathCatalog (ArtworkPathCatalog); this file owns only the timing,
// activity-gating, and dispatch glue.
#include "artworkmanager.h"
#include "artworkpathcatalog.h"
#include "artworkutils.h"
#include "artworkwidgetregistry.h"
#include "loggingcategories.h"
#include "uiconstants.h"

#include <QApplication>
#include <QDateTime>
#include <QMutexLocker>
#include <QStackedWidget>
#include <QStringList>
#include <QThreadPool>
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

  const QStringList dirList =
      ArtworkPathCatalog::collectArtworkDirs(collections, collectionIndex, /*includeDescendants=*/true);
  if (dirList.isEmpty()) {
    return;
  }

  QThreadPool::globalInstance()->start([dirList]() {
    auto &cache = ArtworkUtils::DirectoryCache::instance();
    cache.prewarmDirectories(dirList);
    cache.processQueuedDirectories();
    qCDebug(lcPerfTrace) << "Early dentry prewarm complete: dirs=" << dirList.size();
  });

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

  m_pathCatalog.buildFromCollection(collections, *currentCollectionIndex);
  if (m_pathCatalog.isEmpty()) {
    m_silentLoadingActive = false;
    return;
  }

  if (m_silentLoadTimer && !m_silentLoadTimer->isActive()) {
    m_silentLoadTimer->start();
  }

  if (!m_persistentLoadTimer) {
    m_persistentLoadTimer = new QTimer(this);
    m_persistentLoadTimer->setSingleShot(false);
    m_persistentLoadTimer->setInterval(UIConstants::Artwork::PERSISTENT_SILENT_LOAD_INTERVAL_MS);
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

  m_widgetRegistry->clearPendingOnly();
  m_pathCatalog.clearPathsAndPending();
}

namespace {
// Cooldown gate shared by both silent-loading entry points: returns true when
// the caller should bail this tick because the previous batch hasn't had
// enough idle time since completing.
auto inCooldown(qint64 lastBatchCompletionTime) -> bool {
  if (lastBatchCompletionTime <= 0) {
    return false;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  return (now - lastBatchCompletionTime) < UIConstants::Artwork::SILENT_LOAD_COOLDOWN_MS;
}
} // namespace

// Performs persistent low-frequency caching over the artwork list
void ArtworkManager::processPersistentSilentLoad() {
  if (!m_persistentSilentLoad || m_pathCatalog.isExhausted()) {
    if (m_persistentLoadTimer) {
      m_persistentLoadTimer->stop();
    }
    m_persistentSilentLoad = false;
    return;
  }

  if (inCooldown(m_lastBatchCompletionTime.load())) {
    return;
  }

  // Throttle: skip this tick if too many batches are already in-flight to
  // keep CPU usage from spiking during background precache.
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
      return;
    }
  }

  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }
  if (QApplication::closingDown()) {
    return;
  }

  const int batchSize = isUserIdle() ? UIConstants::Artwork::PERSISTENT_SILENT_BATCH_IDLE
                                     : UIConstants::Artwork::PERSISTENT_SILENT_BATCH_ACTIVE;
  const QStringList batch = m_pathCatalog.takeNextBatch(batchSize);
  const QStringList toPrecache = m_pathCatalog.filterAndMarkPending(batch);
  dispatchAndTrackPrecacheBatch(toPrecache);

  // Restart the continuous timer if the user is now idle and there's still
  // work to do — the persistent timer keeps drilling, but the continuous
  // timer is what drives faster bursts when we have idle CPU.
  if (!m_silentLoadingActive && isUserIdle() && !m_pathCatalog.isExhausted()) {
    m_silentLoadingActive = true;
    m_continuousSilentLoad = true;
    if (m_silentLoadTimer && !m_silentLoadTimer->isActive()) {
      m_silentLoadTimer->start();
    }
  }
}

// Performs continuous caching bursts based on user idleness
void ArtworkManager::processContinuousSilentLoad() {
  if (m_persistentSilentLoad) {
    return;
  }

  if (inCooldown(m_lastBatchCompletionTime.load())) {
    return;
  }

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
      return;
    }
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

  const int batchSize =
      isUserIdle() ? m_silentLoadBatchSize
                   : qMax(1, m_silentLoadBatchSize / UIConstants::Artwork::SILENT_LOAD_THROTTLE_DIVISOR);
  const QStringList batch = m_pathCatalog.takeNextBatch(batchSize);

  if (!m_continuousSilentLoad) {
    return;
  }
  const QStringList toPrecache = m_pathCatalog.filterAndMarkPending(batch);
  dispatchAndTrackPrecacheBatch(toPrecache);

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
  return (currentTime - m_lastUserActivity.load()) >= UIConstants::Timing::USER_IDLE_THRESHOLD_MS;
}
