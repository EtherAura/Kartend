// ViewportArtworkScheduler: drives the viewport-prioritization → batch-dispatch
// → GUI-apply artwork pipeline for ArtworkManager. Promoted from private
// members of ArtworkManager; the silent-load / precache coordinator stays on
// ArtworkManager.
#include "viewportartworkscheduler.h"

#include "artworkloaddispatcher.h"
#include "artworkutils.h"
#include "artworkwidgetregistry.h"
#include "icachemanager.h"
#include "itemwidget.h"
#include "loggingcategories.h"
#include "uiconstants/artwork.h"
#include "uiconstants/cache.h"

#include <functional>
#include <tuple>
#include <utility>

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QList>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QString>
#include <QWidget>

// Computes immediate and extended viewports based on a scroll area's current
// position.
ViewportArtworkScheduler::Viewports
ViewportArtworkScheduler::computeViewports(const QScrollArea *scrollArea) {
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

// Partitions pending items into immediate/extended/remaining by viewport.
std::tuple<QList<ArtworkInfo>, QList<ArtworkInfo>, QList<ArtworkInfo>>
ViewportArtworkScheduler::partitionByViewport(const QList<ArtworkInfo> &localPending, QWidget *grid,
                                              const Viewports &vps,
                                              const std::function<bool(ItemWidget *)> &isLoaded) {
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

// Picks the per-batch decode size: caller override wins, else the adaptive
// batcher's current size, halved for low-priority work to leave headroom for
// fast-arriving foreground requests.
int ViewportArtworkScheduler::determineBatchSize(bool highPriority, int customBatchSize,
                                                 const AdaptiveBatcher &batcher) {
  if (customBatchSize > 0) {
    return customBatchSize;
  }
  const int adaptive = batcher.currentBatchSize();
  return highPriority ? adaptive : qMax(2, adaptive / 2);
}

ViewportArtworkScheduler::ViewportArtworkScheduler(ArtworkManager *owner)
    : QObject(owner), m_owner(owner),
      m_adaptiveBatcher(AdaptiveBatcher::Config{
          UIConstants::Artwork::BATCH_HIGH,                 // initialBatchSize
          UIConstants::Artwork::ADAPTIVE_BATCHER_MIN_SIZE,  // minBatchSize
          UIConstants::Artwork::ADAPTIVE_BATCHER_MAX_SIZE,  // maxBatchSize
          UIConstants::Artwork::ADAPTIVE_BATCHER_TARGET_MS, // targetTimeMs
          UIConstants::Artwork::ADAPTIVE_BATCHER_SMOOTHING, // smoothingFactor
          UIConstants::Artwork::ADAPTIVE_BATCHER_HISTORY    // historySize
      }) {}

// Periodically schedules a deferred persistent cache save once the cache has
// grown enough. Per-instance counters (Kartend-r2722: were function-local
// statics shared across every ArtworkManager instance and never reset).
void ViewportArtworkScheduler::maybeTriggerCacheSave(ICacheManager *cacheManager) {
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

// Updates visible widgets' artwork based on viewport and suppression policy
void ViewportArtworkScheduler::updateViewportArtwork() {
  QElapsedTimer perfTimer;
  if (lcPerfTrace().isDebugEnabled()) {
    perfTimer.start();
  }

  if (m_owner->isArtworkSuppressed()) {
    qCDebug(lcPerfTrace) << "updateViewportArtwork: SUPPRESSED";
    return;
  }

  if (!m_owner->ui.itemScrollArea || !m_owner->gridContainer || !m_owner->stackedWidget ||
      m_owner->stackedWidget->currentWidget() != m_owner->itemsPage) {
    return;
  }
  QList<ArtworkInfo> localPending = m_owner->m_widgetRegistry->takePending();
  if (localPending.isEmpty()) {
    return;
  }

  m_owner->updateUserActivity();

  const Viewports vps = computeViewports(m_owner->ui.itemScrollArea);
  ArtworkWidgetRegistry *registry = m_owner->m_widgetRegistry;
  auto isLoaded = [registry](ItemWidget *widget) -> bool { return registry->isLoaded(widget); };
  QList<ArtworkInfo> immediateItems;
  QList<ArtworkInfo> extendedItems;
  QList<ArtworkInfo> remainingItems;
  std::tie(immediateItems, extendedItems, remainingItems) =
      partitionByViewport(localPending, m_owner->gridContainer, vps, isLoaded);

  // Kartend-b8qe.3: drop remainingItems (out-of-extended-window) instead of
  // storing them back as pending. Items scrolled out of the extended preload
  // window during this update get cancelled; if the user scrolls back into
  // them, configureArtworkForWidget will re-enqueue via addPendingArtwork on
  // the next widget configure pass. Trading a small redundant enqueue on
  // back-scroll for an unbounded queue under long rapid forward-scroll.
  const int droppedOutOfWindow = remainingItems.size();
  Q_UNUSED(droppedOutOfWindow)

  qint64 afterPartition = lcPerfTrace().isDebugEnabled() ? perfTimer.elapsed() : 0;

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

  maybeTriggerCacheSave(m_owner->cacheMgr());
}

// ─── Batch-processing pipeline ────────────────────────────────
// Filters items that are already cached and applies them immediately,
// returning the rest for worker dispatch.
void ViewportArtworkScheduler::collectUncachedAndApplyCached(const QList<ArtworkInfo> &items,
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
    if (ArtworkUtils::baseMatchKey(widgetBaseName) != ArtworkUtils::baseMatchKey(artworkBaseName)) {
      continue;
    }

    QPixmap cached = m_owner->getCachedPixmap(info.artworkPath);
    if (!cached.isNull()) {
      info.mediaItem->setArtworkPixmap(cached);
      m_owner->m_widgetRegistry->markLoaded(info.mediaItem, info.artworkPath);
      m_owner->m_widgetRegistry->track(info.mediaItem);
    } else {
      uncachedItems.append(info);
    }
  }
}

// Applies decoded artwork results to UI widgets on the GUI thread (run from
// the dispatcher's main-thread completion handler).
void ViewportArtworkScheduler::applyResultsToUi(const QList<ArtworkInfo::Result> &batchResults) {
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
  if (m_owner->isArtworkSuppressed()) {
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
      m_owner->m_widgetRegistry->enqueuePending(info);
    }
    return;
  }
  // Kartend-nhnlw: stage the incoming batch into the member apply-queue and
  // drain a bounded slice now. The unprocessed tail stays in m_pendingApply
  // (drained from its front next tick) instead of being copied into a fresh
  // QList and re-posted every tick during a scroll storm.
  for (const auto &result : batchResults) {
    m_pendingApply.push_back(result);
  }
  drainPendingApply();
}

void ViewportArtworkScheduler::drainPendingApply() {
  if (QApplication::closingDown()) {
    m_pendingApply.clear();
    return;
  }

  QElapsedTimer perfTimer;
  const bool perfTrace = lcPerfTrace().isDebugEnabled();
  if (perfTrace) perfTimer.start();

  // Cap the synchronous work per event-loop tick (Kartend-d3qo). A
  // dispatcher batch of ~30 items used to apply every QPixmap::fromImage
  // + setArtworkPixmap + widget->update() in one tick, and multiple
  // batches landing in quick succession compounded into a visible hitch
  // during scroll. Process up to kMaxPerTick items synchronously and
  // re-schedule a single drain via QMetaObject::invokeMethod (QueuedConnection)
  // so the next tick can paint between the slices. The cap counts total
  // iterations (applied + skipped) so a queue full of stale widgets still
  // progresses without a giant sweep.
  constexpr int kMaxPerTick = UIConstants::Artwork::MAX_DECODE_PER_TICK;
  int applied = 0;
  int skipped = 0;
  int processed = 0;
  while (processed < kMaxPerTick && !m_pendingApply.empty()) {
    const ArtworkInfo::Result result = std::move(m_pendingApply.front());
    m_pendingApply.pop_front();
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
      if (ArtworkUtils::baseMatchKey(widgetBaseName) !=
          ArtworkUtils::baseMatchKey(result.artworkBaseName)) {
        ++skipped;
        continue;
      }
    }

    m_owner->m_widgetRegistry->markLoaded(widget, result.artworkPath);
    m_owner->m_widgetRegistry->track(widget);
    if (auto *cache = m_owner->cacheMgr()) {
      if (result.loadedFromDiskCache) {
        cache->cacheArtworkInMemoryOnly(result.artworkPath, pixmap);
      } else {
        // Pass the worker-decoded QImage through so the debounced disk flush
        // can encode it directly instead of deep-copying the pixmap back to
        // a QImage on the GUI thread.
        cache->cacheArtwork(result.artworkPath, pixmap, result.image);
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

  if (!m_pendingApply.empty()) {
    // Tail remains in the member queue — schedule a single queued drain so the
    // GUI can paint between slices. No QList copy: the results stay put in
    // m_pendingApply and the next drain pops from its front. QPointer guard
    // (mirroring loadArtworkParallel's `self`) makes the queued call a no-op if
    // this scheduler is destroyed first (Kartend-zl1g); m_applyDrainScheduled
    // keeps at most one queued drain in flight (Kartend-nhnlw).
    if (!m_applyDrainScheduled) {
      m_applyDrainScheduled = true;
      QPointer<ViewportArtworkScheduler> self(this);
      QMetaObject::invokeMethod(
          this,
          [self]() {
            if (self) {
              self->m_applyDrainScheduled = false;
              self->drainPendingApply();
            }
          },
          Qt::QueuedConnection);
    }
  }

  if (perfTrace) {
    qCDebug(lcPerfTrace).nospace()
        << "drainPendingApply: totalMs=" << perfTimer.elapsed() << " applied=" << applied
        << " skipped=" << skipped << " processed=" << processed
        << " queued=" << m_pendingApply.size();
  }
}

// Parallel artwork loading: split @p items into already-cached (applied
// in-line) and uncached (dispatched to the worker pool in adaptive-sized
// batches).
void ViewportArtworkScheduler::loadArtworkParallel(const QList<ArtworkInfo> &items,
                                                   bool highPriority, int customBatchSize) {
  if (QApplication::closingDown()) {
    return;
  }
  if (items.isEmpty() || m_owner->shouldSkipArtworkLoading()) {
    return;
  }

  QElapsedTimer perfTimer;
  if (lcPerfTrace().isDebugEnabled()) {
    perfTimer.start();
  }

  const int batchSize = determineBatchSize(highPriority, customBatchSize, m_adaptiveBatcher);
  QList<ArtworkInfo> uncachedItems;
  uncachedItems.reserve(items.size());
  collectUncachedAndApplyCached(items, uncachedItems);
  if (QApplication::closingDown()) {
    return;
  }

  const qint64 afterCollect = lcPerfTrace().isDebugEnabled() ? perfTimer.elapsed() : 0;
  int batchCount = 0;

  QPointer<ViewportArtworkScheduler> self(this);
  for (int i = 0; i < uncachedItems.size(); i += batchSize) {
    if (QApplication::closingDown()) {
      break;
    }
    const int end = qMin(i + batchSize, uncachedItems.size());
    QList<ArtworkInfo> batch = uncachedItems.mid(i, end - i);
    m_owner->m_dispatcher->dispatchBatch(
        std::move(batch), highPriority,
        [self](const QList<ArtworkInfo::Result> &results, int batchItemCount, qint64 elapsedMs,
               bool wasHighPriority) {
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
                << "requested=" << batchItemCount << "produced=" << results.size()
                << "diskHits=" << diskHits << "elapsedMs=" << elapsedMs;
          }
          self->applyResultsToUi(results);
          if (wasHighPriority && !results.isEmpty()) {
            self->m_adaptiveBatcher.observeBatch(batchItemCount, elapsedMs);
          }
        });
    ++batchCount;
  }

  if (lcPerfTrace().isDebugEnabled() && perfTimer.elapsed() > 5) {
    qCDebug(lcPerfTrace) << "loadArtworkParallel: totalMs=" << perfTimer.elapsed()
                         << "collectMs=" << afterCollect << "items=" << items.size()
                         << "uncached=" << uncachedItems.size() << "batches=" << batchCount;
  }
}
