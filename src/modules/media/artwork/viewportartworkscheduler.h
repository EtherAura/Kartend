#ifndef VIEWPORTARTWORKSCHEDULER_H
#define VIEWPORTARTWORKSCHEDULER_H

#include <QList>
#include <QObject>
#include <QPointer>

#include "adaptivebatcher.h"
#include "artworkmanager.h"

class ICacheManager;

/**
 * @brief Viewport-driven artwork loading pipeline extracted from ArtworkManager.
 *
 * Owns the viewport-prioritization → batch-dispatch → GUI-apply pipeline that
 * formerly lived as private members of ArtworkManager (the silent-load /
 * precache coordinator stays on ArtworkManager). The canonical shared state —
 * the async dispatcher, the widget registry, the UI references, and the
 * suppression / user-activity / cache-save hooks — stays on ArtworkManager and
 * is reached here via friendship + a back-pointer. The adaptive batch sizer and
 * the cache-save cadence counters are genuinely viewport-local and live here.
 *
 * Lifetime: owned by ArtworkManager via std::unique_ptr and Qt-parented under
 * it, so it is destroyed before the borrowed dispatcher / registry; the
 * back-pointer is a QPointer guarding against destruction-order surprises
 * (mirrors VirtualScrollEngine).
 */
class ViewportArtworkScheduler : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ViewportArtworkScheduler)

public:
  explicit ViewportArtworkScheduler(ArtworkManager *owner);
  ~ViewportArtworkScheduler() override = default;

  /// Updates visible widgets' artwork based on viewport and suppression policy.
  void updateViewportArtwork();
  /// Parallel artwork loading: split @p items into already-cached (applied
  /// in-line) and uncached (dispatched to the worker pool in adaptive-sized
  /// batches).
  void loadArtworkParallel(const QList<ArtworkInfo> &items, bool highPriority,
                           int customBatchSize = 0);

private:
  void collectUncachedAndApplyCached(const QList<ArtworkInfo> &items,
                                     QList<ArtworkInfo> &uncachedItems);
  /// Applies processed artwork results to UI widgets on the GUI thread.
  void applyResultsToUi(const QList<ArtworkInfo::Result> &batchResults);
  /// Periodically schedules a deferred persistent cache save once the cache has
  /// grown enough. Per-instance counters (were function-local statics shared
  /// across every ArtworkManager instance and never reset).
  void maybeTriggerCacheSave(ICacheManager *cacheManager);

  // Back-pointer to the owning ArtworkManager. QPointer guards against dangling
  // reads if a future refactor changes the destruction order — the scheduler is
  // Qt-parented under ArtworkManager so under normal teardown m_owner is live
  // for the scheduler's whole lifetime.
  QPointer<ArtworkManager> m_owner;

  // Adaptive batching for performance-based batch sizing.
  AdaptiveBatcher m_adaptiveBatcher;

  // Per-instance cache-save cadence counters (were function-local statics in
  // maybeTriggerCacheSave, shared across every ArtworkManager instance and
  // never reset).
  int m_cacheUpdateCount = 0;
  qint64 m_lastCacheSaveSize = 0;
};

#endif // VIEWPORTARTWORKSCHEDULER_H
