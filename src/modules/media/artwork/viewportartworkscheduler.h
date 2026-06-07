#ifndef VIEWPORTARTWORKSCHEDULER_H
#define VIEWPORTARTWORKSCHEDULER_H

#include <functional>
#include <tuple>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>

#include "adaptivebatcher.h"
#include "artworkmanager.h"

class ICacheManager;
class ItemWidget;
QT_BEGIN_NAMESPACE
class QScrollArea;
class QWidget;
QT_END_NAMESPACE

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

  // Viewport-prioritization math, exposed as pure statics for direct testing
  // (Kartend-nzjxm). No member state — the scheduler's instance methods call
  // these too.
  struct Viewports {
    QRect immediate; ///< On-screen band (grid coordinates).
    QRect extended;  ///< immediate + a one-viewport overscan margin on each side.
  };
  /// Immediate + extended viewports for a scroll area's current scroll position.
  [[nodiscard]] static Viewports computeViewports(const QScrollArea *scrollArea);
  /// Partition @p localPending into {immediate, extended, remaining} by where
  /// each item's widget falls relative to @p vps; null or already-loaded items
  /// (per @p isLoaded) are skipped.
  [[nodiscard]] static std::tuple<QList<ArtworkInfo>, QList<ArtworkInfo>, QList<ArtworkInfo>>
  partitionByViewport(const QList<ArtworkInfo> &localPending, QWidget *grid, const Viewports &vps,
                      const std::function<bool(ItemWidget *)> &isLoaded);
  /// Per-batch decode size: caller override wins, else the adaptive batcher's
  /// size, halved (min 2) for low-priority work.
  [[nodiscard]] static int determineBatchSize(bool highPriority, int customBatchSize,
                                              const AdaptiveBatcher &batcher);

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
