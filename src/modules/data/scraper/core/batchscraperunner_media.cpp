// Sibling TU: the per-item media fetch/write pipeline for BatchScrapeRunner.
// Lives here: resolveWantedMediaAssets (asset selection), fetchMediaAndFinish
// + onMediaBytesComplete (parallel byte fetches onto the shared aggregator),
// and applyAndFinish (the QtConcurrent artwork/sidecar file-write phase).
// The write-completion continuation (onMediaWriteFinished) belongs to the
// DB-write dispatch path and lives in batchscraperunner_worker.cpp; the core
// queue pump and lookup/detail chain stay in batchscraperunner.cpp.
#include "batchscraperunner.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

namespace Scraper {

QList<Scraper::MediaAsset>
BatchScrapeRunner::resolveWantedMediaAssets(const Scraper::ScrapedItem &scraped) const {
  // Media-fetch resolution. Two modes depending on whether the user
  // supplied an explicit media-type filter:
  //   • filter empty → legacy "front cover only" path. Backwards-compat
  //     for callers that pre-date the per-type checkbox UI.
  //   • filter non-empty → every asset whose `type` matches one of the
  //     requested types, fetched in parallel by the caller.
  // Caller has already confirmed m_fetchPrimaryCover; this only resolves
  // which assets to fetch.
  const bool useFilter = !m_mediaTypeFilter.isEmpty();
  QList<Scraper::MediaAsset> wantedAssets;
  for (const Scraper::MediaAsset &m : scraped.media) {
    if (!m.url.isValid()) continue;
    if (useFilter) {
      if (m_mediaTypeFilter.contains(m.type.toLower())) {
        wantedAssets.append(m);
      }
    } else if (m.type.compare(QStringLiteral("front"), Qt::CaseInsensitive) == 0) {
      wantedAssets.append(m);
      break; // legacy path: at most one cover per item.
    }
  }
  return wantedAssets;
}

void BatchScrapeRunner::fetchMediaAndFinish(const std::shared_ptr<ItemState> &state,
                                            const Scraper::ScrapedItem &scraped,
                                            const QList<Scraper::MediaAsset> &wantedAssets) {
  // Shared aggregator: every parallel fetch decrements `pending` on
  // completion; the last one out commits. shared_ptr because the lambdas
  // outlive any one fetch.
  auto agg = std::make_shared<MediaAggregator>();
  agg->pending = wantedAssets.size();
  QPointer<BatchScrapeRunner> self(this);
  for (const auto &asset : wantedAssets) {
    // Kind-aware entry point: the asset type selects the provider's per-kind
    // Content-Type prefix + response cap (video/manual vs image, Kartend-jjyst.1).
    m_provider->fetchMediaBytesForType(
        asset.url, asset.type,
        [self, state, scraped, asset, agg](const ErrorUtils::Result<QByteArray> &r) {
          if (self.isNull()) return;
          self->onMediaBytesComplete(state, scraped, asset, agg, r);
        });
  }
}

void BatchScrapeRunner::onMediaBytesComplete(const std::shared_ptr<ItemState> &state,
                                             const Scraper::ScrapedItem &scraped,
                                             const Scraper::MediaAsset &asset,
                                             const std::shared_ptr<MediaAggregator> &agg,
                                             const ErrorUtils::Result<QByteArray> &r) {
  if (m_cancelled) {
    if (--agg->pending == 0) {
      itemFinished();
    }
    return;
  }
  if (state->cancelToken->load(std::memory_order_acquire)) {
    // Skipped mid media-fetch: drop the in-flight assets and
    // count the item as skipped once the last fetch returns
    // (it never reaches applyAndFinish, so nothing is written).
    // A user skip is terminal — trim it from the resume list.
    if (--agg->pending == 0) {
      ++m_summary.skipped;
      m_remainingPaths.removeOne(state->path);
      itemFinished();
    }
    return;
  }
  if (r.isOk() && !r.value().isEmpty()) {
    // Track byte count regardless of write success
    // — the user's bandwidth was already spent.
    m_totalBytesDownloaded += r.value().size();
    // A delivered asset proves the host's rate limiter let us through —
    // it ends any consecutive-429 run (Kartend-jjyst.3).
    m_consecutive429Count = 0;
    Scraper::PendingMediaWrite w;
    w.asset = asset;
    w.bytes = r.value();
    agg->writes.append(w);
  } else {
    // Returned-but-undownloadable: remember the type so the mediaAbsent
    // merge doesn't prune its absent marker as satisfied (Kartend-jjyst.1).
    agg->failedTypes.append(asset.type.toLower());
    // Count the loss and record a bounded diagnosis — previously a failed
    // asset fetch ticked nothing and a run full of dead media URLs reported
    // "scraped N, 0 media" with zero explanation (Kartend-jjyst.4). The
    // entity path already records its fetch failures; this brings the game
    // path in line.
    ++m_summary.mediaFetchFailures;
    if (m_summary.firstFailures.size() < kMaxReportedFailures) {
      const QString itemName = QFileInfo(state->path).fileName();
      m_summary.firstFailures.append(
          r.isError() ? QStringLiteral("%1: %2 fetch failed: %3")
                            .arg(itemName, asset.type, r.error().userFacingSummary())
                      : QStringLiteral("%1: %2 fetch returned no data").arg(itemName, asset.type));
    }
    if (r.isError() && m_provider && m_provider->isQuotaExhausted(r.error())) {
      // A quota-exhausted media fetch is still
      // non-fatal for THIS item (it keeps its
      // metadata + whatever assets already landed),
      // but it must stop new items from dispatching
      // — same stop signal as the lookup/detail path.
      m_summary.quotaExhausted = true;
      m_quotaStopped = true;
    } else if (r.isError() && r.error().httpStatus == 429) {
      // Media CDNs are the realistic 429 source (TMDB images, Cover Art
      // Archive). One 429'd asset is throttling, not exhaustion — escalate
      // to a queue stop only when the limiter answers 429 repeatedly
      // (Kartend-jjyst.3).
      noteRateLimited429();
    }
  }
  // Asset fetch failures are non-fatal — partial
  // success is better than failing the whole item
  // because one 404'd asset.
  if (--agg->pending == 0) {
    Scraper::ScrapedItem effective = scraped;
    effective.mediaFetchFailedThisRun = agg->failedTypes;
    applyAndFinish(state, effective, agg->writes);
  }
}

void BatchScrapeRunner::applyAndFinish(const std::shared_ptr<ItemState> &state,
                                       const Scraper::ScrapedItem &scraped,
                                       const QList<Scraper::PendingMediaWrite> &writes) {
  if (m_cancelled) {
    itemFinished();
    return;
  }
  const QString baseName = QFileInfo(state->path).completeBaseName();
  // Strip text fields when the user opted out of metadata writes —
  // the persistence layer keeps any existing DB values when the
  // scraped fields are empty (pickNonEmpty in scrapepersistence.cpp).
  // Custom fields collapse to empty so the merge becomes a no-op.
  // sourceProviderId is also cleared so the item's `source` column
  // doesn't get overwritten to attribute the (skipped) text scrape.
  // INVARIANT (Kartend-kihyx): do NOT clear effective.mediaAbsentThisRun,
  // effective.mediaFetchFailedThisRun, or effective.media here. Known-absent
  // media tracking is independent of the text-metadata opt-out — a media-only
  // FillMissing run (metadata already complete, only filling art) is precisely
  // where it must keep working, and the merge needs `media` (minus the failed
  // fetches, Kartend-jjyst.1) to prune types the provider now supplies. Clearing
  // any of them would silently reintroduce the perpetual re-scrape this fixes.
  Scraper::ScrapedItem effective = scraped;
  if (!m_writeMetadata) {
    effective.title.clear();
    effective.description.clear();
    effective.genre.clear();
    effective.developer.clear();
    effective.publisher.clear();
    effective.releaseDate.clear();
    effective.contentRating.clear();
    effective.players.clear();
    effective.tagsJson.clear();
    effective.customFields.clear();
    effective.sourceProviderId.clear();
    effective.runtimeSeconds = -1;
  }

  // ── File-I/O phase (QThreadPool) ──────────────────────────────
  // Artwork writes + the existence probes / byte comparisons inside
  // writeMediaFiles' rescrape gate run on the global QThreadPool so
  // the main UI thread doesn't stall once per item. The result hops
  // back to the main thread via the watcher's finished slot — from
  // there the DB save is dispatched to the dedicated ScrapeWriteWorker
  // thread (which owns its own QSqlDatabase connection), and the
  // per-item state machine resumes when the worker queues the
  // writeCompleted signal back here.
  auto *watcher = new QFutureWatcher<Scraper::MediaWriteResult>(this);
  QPointer<BatchScrapeRunner> self(this);
  // Stall guard for the artwork/sidecar write (Kartend audit xnm8a): the
  // QtConcurrent task below blocks in write()/fsync() on a wedged mount with no
  // timeout. Share `writeDone` with the watcher so the watchdog and the normal
  // completion race cleanly — whichever fires first wins; the other no-ops.
  auto writeDone = armStepWatchdog(state, QStringLiteral("artwork/metadata write"));
  connect(watcher, &QFutureWatcher<Scraper::MediaWriteResult>::finished, this,
          [self, watcher, writeDone, state, effective, baseName]() mutable {
            watcher->deleteLater();
            if (self.isNull() || writeDone.fired()) return;
            writeDone.finish();
            self->onMediaWriteFinished(state, effective, baseName, watcher->result());
          });
  // Track the task and hand it the runner-lifetime cancel token
  // (Kartend-vi76q): the global pool has no per-runner drain, so the
  // destructor flips the token and waits on this list instead. Prune
  // finished entries first so the list stays O(itemConcurrency).
  m_inFlightMediaWrites.removeIf(
      [](const QFuture<Scraper::MediaWriteResult> &f) { return f.isFinished(); });
  // Read the item's hand-linked types HERE, on the main thread: the worker
  // below must not touch the database, so the set is computed now and captured
  // by value (Kartend-yibgw).
  const QSet<QString> protectedTypes =
      Scraper::handLinkedArtworkTypes(dbMgr(), m_collectionUuid, state->path, m_artworkDir);
  const QFuture<Scraper::MediaWriteResult> writeFuture = QtConcurrent::run(
      [artworkDir = m_artworkDir, baseName, writes, effective, rescrapeMode = m_rescrapeMode,
       mediaCancel = m_mediaWriteCancel, protectedTypes]() {
        // Cancelled while queued (pool saturated during a long batch):
        // skip the sidecar + media writes entirely.
        if (mediaCancel->load(std::memory_order_acquire)) {
          return Scraper::MediaWriteResult{};
        }
        // Human-readable JSON sidecar alongside the artwork. `effective`
        // is blank when the user opted out of metadata, so the sidecar
        // helper returns Skipped in that case. A genuine Failed write
        // (mkpath / atomic-write error) is carried back on the result so
        // the batch summary can count it (Kartend audit hhr5x); Skipped
        // and Written are silent.
        const Scraper::SidecarWriteOutcome sidecarOutcome =
            Scraper::writeMetadataSidecar(artworkDir, baseName, effective, rescrapeMode);
        Scraper::MediaWriteResult mediaRes = Scraper::writeMediaFiles(
            artworkDir, baseName, writes, rescrapeMode, mediaCancel, protectedTypes);
        mediaRes.sidecarFailed = (sidecarOutcome == Scraper::SidecarWriteOutcome::Failed);
        return mediaRes;
      });
  m_inFlightMediaWrites.append(writeFuture);
  watcher->setFuture(writeFuture);
}

} // namespace Scraper
