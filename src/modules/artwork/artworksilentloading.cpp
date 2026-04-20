// Silent (background) artwork loading methods for ArtworkManager.
// Extracted from artworkmanager.cpp during LOC-reduction refactor.
// These remain ArtworkManager members; this is a translation-unit split only.
#include "artworkmanager.h"
#include "artworkutils.h"
#include "collectionutils.h"
#include "uiconstants.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QMutexLocker>
#include <QStackedWidget>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <functional>

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
