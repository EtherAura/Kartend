#ifndef CACHEDCOUNTSSERVICE_H
#define CACHEDCOUNTSSERVICE_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "collectionutils.h"

class QTimer;
class SessionManager;

/// Owns the debounced async cached-count update flow that lives between
/// DatabaseManager (main thread) and the QueryManager worker (DB thread).
///
/// Lifecycle:
///   - DatabaseManager calls requestUpdate() whenever counts may have drifted
///     (e.g. after a collection load finishes).
///   - requestUpdate() coalesces successive calls behind a single-shot timer
///     so a flurry of nav/filter changes only triggers one worker round-trip.
///   - When the timer fires, dispatchToWorker() is emitted; DatabaseManager
///     wires that to QueryManager::updateCachedCounts.
///   - The worker emits cachedCountsComputed -> onWorkerComputed slot, which
///     fans the per-uuid totals out to SessionManager and emits updated().
class CachedCountsService : public QObject {
  Q_OBJECT
public:
  explicit CachedCountsService(SessionManager *sessionManager, int debounceMs,
                               QObject *parent = nullptr);

  /// Coalesce a counts-recompute request. Multiple calls inside the debounce
  /// window collapse into one worker dispatch carrying the most recent inputs.
  void requestUpdate(const QList<CollectionConfig> &allCollections, const QStringList &uuids);

  /// Forwarded from QueryManager: computes recursive totals from the worker's
  /// direct-counts payload, applies them to SessionManager, and emits updated().
  /// Stale generations (i.e. computations completed after a newer dispatch was
  /// issued) are silently dropped.
  void onWorkerComputed(quint64 generation, qint64 globalCount,
                        const QHash<QString, qint64> &directCountsByUuid);

signals:
  /// Wire to QueryManager::updateCachedCounts. Carries the generation token so
  /// onWorkerComputed can drop stale completions.
  void dispatchToWorker(quint64 generation, const QStringList &collectionUuids);

  /// Emitted after the most recent computation has been applied to
  /// SessionManager. Consumers can refresh dependent UI.
  void updated();

private slots:
  void onTimerFired();

private:
  SessionManager *m_sessionManager;
  QTimer *m_timer;
  quint64 m_generation = 0;
  quint64 m_inFlightGeneration = 0;
  QList<CollectionConfig> m_pendingCollections;
  QStringList m_pendingUuids;
  QList<CollectionConfig> m_inFlightCollections;
  QStringList m_inFlightUuids;
};

#endif
