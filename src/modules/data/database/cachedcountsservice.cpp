#include "cachedcountsservice.h"

#include <functional>
#include <QTimer>
#include <QVector>

#include "isessionmanager.h"

CachedCountsService::CachedCountsService(ISessionManager *sessionManager, int debounceMs,
                                         QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager), m_timer(new QTimer(this)) {
  m_timer->setSingleShot(true);
  m_timer->setInterval(debounceMs);
  connect(m_timer, &QTimer::timeout, this, &CachedCountsService::onTimerFired);
}

void CachedCountsService::requestUpdate(const QList<CollectionConfig> &allCollections,
                                        const QStringList &uuids) {
  m_pendingCollections = allCollections;
  m_pendingUuids = uuids;
  m_timer->start();
}

void CachedCountsService::onTimerFired() {
  if (!m_sessionManager) {
    return;
  }
  m_inFlightGeneration = ++m_generation;
  m_inFlightCollections = m_pendingCollections;
  m_inFlightUuids = m_pendingUuids;
  emit dispatchToWorker(m_inFlightGeneration, m_inFlightUuids);
}

void CachedCountsService::onWorkerComputed(quint64 generation, qint64 globalCount,
                                           const QHash<QString, qint64> &directCountsByUuid) {
  if (!m_sessionManager) {
    return;
  }
  if (generation != m_inFlightGeneration) {
    // A newer dispatch was issued before this one came back — drop it.
    return;
  }

  const QList<CollectionConfig> &collections = m_inFlightCollections;
  const int collectionCount = collections.size();

  QVector<qint64> directCounts(collectionCount);
  QVector<qint64> recursiveCounts(collectionCount);

  for (int i = 0; i < collectionCount; ++i) {
    const QString uuid = (i < m_inFlightUuids.size()) ? m_inFlightUuids[i] : QString();
    directCounts[i] = uuid.isEmpty() ? 0 : directCountsByUuid.value(uuid, 0);
  }

  QVector<QList<int>> children(collectionCount);
  for (int i = 0; i < collectionCount; ++i) {
    const int parent = collections[i].parentCollectionIndex;
    if (parent >= 0 && parent < collectionCount) {
      children[parent].append(i);
    }
  }

  QVector<int> visitState(collectionCount, 0);
  std::function<qint64(int)> computeRecursiveCount = [&](int index) -> qint64 {
    if (index < 0 || index >= collectionCount) {
      return 0;
    }
    if (visitState[index] == 2) {
      return recursiveCounts[index];
    }
    if (visitState[index] == 1) {
      // Cycle guard: treat the in-progress node as a leaf.
      return directCounts[index];
    }
    visitState[index] = 1;
    qint64 total = directCounts[index];
    for (int childIndex : children[index]) {
      total += computeRecursiveCount(childIndex);
    }
    visitState[index] = 2;
    recursiveCounts[index] = total;
    return total;
  };
  for (int i = 0; i < collectionCount; ++i) {
    computeRecursiveCount(i);
  }

  m_sessionManager->setGlobalItemCount(globalCount);
  for (int i = 0; i < collectionCount; ++i) {
    m_sessionManager->setCollectionCounts(collections[i], collections, directCounts[i],
                                          recursiveCounts[i]);
  }
  m_sessionManager->saveToDisk();
  emit updated();
}
