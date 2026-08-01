// Async worker pool that drives a batch scrape over one collection.
// Per-item: provider.lookup → provider.fetchDetail (auto-pick first
// candidate) → optional fetchMediaBytes for the primary cover →
// Scraper::applyScrapedItem. Multiple items run through this chain in
// parallel when itemConcurrency > 1; per-provider rate limits are
// honoured by the underlying Scraper::HttpClient throttle, so high
// item concurrency just stacks more work behind the per-host gate.
//
// Threading: the runner itself lives on the main thread (HTTP provider
// callbacks fire there). Per-item DB writes are dispatched to a
// dedicated ScrapeWriteWorker on its own QThread so the SQLite save
// doesn't tax the UI event loop. File I/O continues to run on the
// global QThreadPool via QtConcurrent::run.
//
// This TU keeps the core: construction/teardown, the queue pump, the
// already-scraped pre-filter, and the per-item lookup/detail chain.
// Sibling TUs (same class, partial-split pattern):
//   batchscraperunner_worker.cpp   — write-worker lifecycle + DB-write
//                                    dispatch/completion
//   batchscraperunner_media.cpp    — media fetch/write pipeline
//   batchscraperunner_watchdog.cpp — step watchdogs + error/rate-limit
//                                    accounting
#include "batchscraperunner.h"

#include <atomic>
#include <utility>

#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include "applicationcontext.h"
#include "httpclient.h"
#include "idatabasemanager.h"
#include "scrapepersistence.h"
#include "scrapeskipdecision.h"

namespace Scraper {

// Namespace scope (not file-local) so batchscraperunner_watchdog.cpp can log
// to the same category via Q_DECLARE_LOGGING_CATEGORY — same pattern as
// cachemanager.cpp / cachemanagerdisk.cpp share lcCacheManager.
Q_LOGGING_CATEGORY(lcBatchScrape, "kartend.scrape.batch", QtWarningMsg)

BatchScrapeRunner::BatchScrapeRunner(const ApplicationContext *ctx,
                                     std::shared_ptr<MetadataLookupProvider> provider,
                                     QString collectionUuid, QStringList paths, QString artworkDir,
                                     bool fetchPrimaryCover, Scraper::RescrapeMode rescrapeMode,
                                     int itemConcurrency, int skipRecentDays, QObject *parent)
    : QObject(parent), m_ctx(ctx), m_provider(std::move(provider)),
      m_collectionUuid(std::move(collectionUuid)), m_paths(std::move(paths)),
      m_artworkDir(std::move(artworkDir)), m_fetchPrimaryCover(fetchPrimaryCover),
      m_rescrapeMode(rescrapeMode), m_itemConcurrency(std::max(1, itemConcurrency)),
      m_skipRecentDays(qBound(0, skipRecentDays, 365)) {
  // Kartend-ou0a: relay provider stage changes (hashing, extracting,
  // …) as our own itemStageChanged signal so BatchScrapeProgressView
  // can show the current stage and the user doesn't think the scrape
  // is hung during a multi-minute PS2 .zip extraction. QPointer guard
  // because the callback fires from the GUI thread but after the
  // runner may have been destroyed (e.g. cancel + destroy mid-hash).
  if (m_provider) {
    QPointer<BatchScrapeRunner> guard(this);
    m_provider->setStageReporter([guard](const QString &stage) {
      if (guard) emit guard->itemStageChanged(stage);
    });
  }
}

BatchScrapeRunner::~BatchScrapeRunner() {
  // Stop and drain the in-flight global-pool media writes (Kartend-vi76q).
  // The token makes both the lambda preamble and writeMediaFiles' per-asset
  // loop exit promptly, so the drain normally completes within one
  // in-progress asset write. Kartend-8mx2q: the wait is BOUNDED now — the
  // runner lives on the GUI thread, and the old unconditional
  // waitForFinished() hung the UI for as long as one multi-MB write to
  // wedged/network storage took (unbounded), unlike every other teardown in
  // the codebase (2s budgets). Abandoning past the budget is safe: the
  // write lambdas capture values + the shared cancel token only (no
  // `this`), so the worst case is the current asset file finishing its
  // write to disk after the runner is gone. The watchers are children of
  // `this` and are destroyed below — they never fire post-dtor.
  m_mediaWriteCancel->store(true, std::memory_order_release);
  // Kartend-n5m1c: lowered from 2000ms. This drain runs back-to-back with
  // shutdownWriteWorker()'s thread join below, both on the GUI thread, so the
  // two budgets stacked to ~4s of an unresponsive window on wedged/network
  // storage. The token already makes the write lambdas exit within one
  // in-progress asset, so the full budget is only ever consumed on genuinely
  // stalled storage — abandoning past it is safe (value-captures only). 1000ms
  // here + 1000ms join caps the worst-case GUI freeze at ~2s.
  constexpr int kMediaWriteDrainBudgetMs = 1000;
  QDeadlineTimer deadline(kMediaWriteDrainBudgetMs);
  bool abandoned = false;
  for (auto &future : m_inFlightMediaWrites) {
    while (!future.isFinished() && !deadline.hasExpired()) {
      QThread::msleep(10);
    }
    if (!future.isFinished()) {
      abandoned = true;
    }
  }
  if (abandoned) {
    qCWarning(lcBatchScrape) << "BatchScrapeRunner: media writes did not drain in"
                             << kMediaWriteDrainBudgetMs
                             << "ms during destruction; abandoned them (value-captures only — the "
                                "in-progress asset may still finish writing to disk)";
  }
  shutdownWriteWorker();
}

IDatabaseManager *BatchScrapeRunner::dbMgr() const {
  return m_ctx ? m_ctx->databaseManager() : nullptr;
}

void BatchScrapeRunner::start() {
  // Spin up the write worker eagerly. We could lazy-start it on the
  // first applyAndFinish, but doing it here pulls the connection-open
  // cost into the start() tick (off-screen) instead of paying it at
  // the first item completion (where it would interleave with the
  // user-visible "first item scraped" event).
  ensureWriteWorkerStarted();

  // Always defer first work to the next event-loop tick. Callers
  // typically wire up signal handlers right after constructing the
  // runner and calling start(); emitting `finished` synchronously
  // here (in the no-provider / empty-list cases) would beat the
  // caller's connect and silently swallow the result.
  QTimer::singleShot(0, this, [this]() {
    if (!m_provider) {
      m_summary.firstFailures.append(QStringLiteral("No metadata provider configured."));
      if (!m_finishedEmitted) {
        m_finishedEmitted = true;
        emit finished(m_summary);
      }
      return;
    }
    // filterAlreadyScraped is the batch-level pre-flight for both
    // `Skip` (drop any item with metadata on disk) and `FillMissing`
    // (drop items where every ticked checkbox is already covered).
    // FillMissing's per-asset gate inside applyScrapedItem still
    // catches the partial-coverage case mid-run — this filter only
    // skips items where there is literally nothing the provider
    // would have to fill, saving the lookup request. Overwrite /
    // UpdateChanged intentionally hit every item: Overwrite always
    // re-fetches, and UpdateChanged needs the bytes back to compare.
    if (m_rescrapeMode == Scraper::RescrapeMode::Skip ||
        m_rescrapeMode == Scraper::RescrapeMode::FillMissing) {
      filterAlreadyScraped();
    }
    // Seed the resume bookkeeping AFTER the pre-filter so pre-skipped items
    // don't linger as "remaining" (they'd just be re-skipped on resume, but
    // the count in the resume prompt would overstate the work left).
    m_remainingPaths = m_paths;
    pump();
  });
}

void BatchScrapeRunner::cancel() {
  m_cancelled = true;
  // Signal every in-flight provider hash/extraction worker to abort promptly —
  // kills the extractor QProcess instead of finishing the multi-minute hash.
  // Per-item tokens (skipCurrentItem flips one of these for a single item).
  for (const auto &weak : m_inFlightItems) {
    if (auto state = weak.lock()) {
      state->cancelToken->store(true, std::memory_order_release);
    }
  }
  // Also stop the global-pool media-write fan-out (Kartend-vi76q): a
  // cancelled batch shouldn't keep writing artwork files the user just
  // abandoned. onWriteCompleted already discards results once m_cancelled
  // is set, so partial results from a mid-write cancel are dropped cleanly.
  m_mediaWriteCancel->store(true, std::memory_order_release);
  // Drop every not-yet-dispatched HTTP request. Flags alone only make the
  // CALLBACKS abandon their items — requests already sitting in HttpClient's
  // per-host queues (up to itemConcurrency x N assets) would still dispatch
  // as slots free, burning bandwidth and the provider's daily quota for a
  // run the user just killed, and contending with any freshly-started run.
  // clearPending resolves each dropped callback with RequestQueueCleared — a
  // code RetryPolicy::isTransient never retries, so the provider's transient-
  // retry gate can't re-issue the dropped lookups 500ms later against a run
  // the user just killed (Kartend-jjyst.2); the cancelled chains discard
  // those callbacks — and leaves in-flight replies to finish.
  // The queue is global, but a cancel is user-initiated and rare: the only
  // other queued traffic is background catalog/quota refreshes, which retry
  // on their own schedule.
  Scraper::HttpClient::instance()->clearPending();
  // Don't emit finished here — let the in-flight callbacks observe the
  // flag and drain. The last completing callback emits finished()
  // once m_inFlight returns to 0, which avoids a double-emit if any
  // chain happens to land synchronously while cancel() is running.
  // The write worker is left running so any in-flight writeCompleted
  // signals can still be received and the per-item slots drained
  // cleanly; the destructor handles the eventual thread shutdown.
}

void BatchScrapeRunner::skipCurrentItem() {
  // Flip only the displayed item's token, leaving m_cancelled false so the
  // batch and any other in-flight items keep running. The chain callbacks
  // observe the token and count this item as skipped. No-op if the item
  // already finished (weak handle expired) or nothing is in flight.
  if (auto state = m_currentItem.lock()) {
    state->cancelToken->store(true, std::memory_order_release);
  }
}

BatchScrapeRunner::PreFilterResult BatchScrapeRunner::preFilterAlreadyScraped(
    IDatabaseManager *db, const QString &collectionUuid, const QString &artworkDir,
    const QSet<QString> &mediaTypeFilter, bool fetchPrimaryCover,
    Scraper::RescrapeMode rescrapeMode, bool writeMetadata, int skipRecentDays,
    const QStringList &paths) {
  PreFilterResult out;
  out.kept = paths;
  // No DB AND no artwork dir means nothing to check against — bail
  // before the loop so an empty test fixture (no DB, no artwork dir)
  // keeps every item, matching the legacy behaviour for those callers.
  const bool dbCheckPossible = db && !collectionUuid.isEmpty();
  const bool sidecarCheckPossible = !artworkDir.isEmpty();
  if (!dbCheckPossible && !sidecarCheckPossible) return out;

  // Effective "wanted" set under FillMissing — mirrors what the
  // per-item write phase would attempt for this run. mediaTypeFilter
  // non-empty wins; otherwise the legacy "front only" fallback
  // applies when fetchPrimaryCover is on (the runner header
  // documents this). With nothing wanted at all we have nothing to
  // pre-skip on, so leave the queue untouched.
  QSet<QString> wantedTypes;
  for (const QString &type : mediaTypeFilter) {
    wantedTypes.insert(type.toLower());
  }
  if (wantedTypes.isEmpty() && fetchPrimaryCover) {
    wantedTypes.insert(QStringLiteral("front"));
  }
  const bool isFillMissing = rescrapeMode == Scraper::RescrapeMode::FillMissing;
  if (isFillMissing && wantedTypes.isEmpty() && !writeMetadata) {
    // Nothing is being asked for; the run is a no-op anyway. Keep
    // the queue untouched so the caller's tallies match.
    return out;
  }

  // Media-on-disk coverage indexes, pre-built once so the per-item skip check
  // is an O(1) hash lookup (Kartend audit 2w4wz).
  MediaCoverageIndex coverage =
      Scraper::buildMediaCoverageIndex(artworkDir, wantedTypes, sidecarCheckPossible);
  // updated_at is stored UTC ISO; compute the cutoff in UTC so the
  // comparison stays timezone-agnostic regardless of how the parsed
  // QDateTime's TimeSpec ends up after fromString().
  const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
  const bool hasWindow = skipRecentDays > 0;
  const QDateTime cutoff = hasWindow ? nowUtc.addDays(-skipRecentDays) : QDateTime();

  // Batch-fetch metadata for every candidate path up front. Without this
  // pre-flight, the per-item check below would issue one SELECT per path
  // on the GUI thread — a 1000-item collection burned multiple seconds
  // freezing the window before pump() even started.
  const QHash<QString, ItemMetadataStore::ItemMetadata> metadataByPath =
      dbCheckPossible ? db->loadItemMetadataBatch(collectionUuid, paths)
                      : QHash<QString, ItemMetadataStore::ItemMetadata>{};

  // Bundle the precomputed read-only context for the per-item skip
  // predicate. shouldSkipScrapedItem() consumes this once per path.
  ScrapeSkipContext skipCtx;
  skipCtx.mode = rescrapeMode;
  skipCtx.writeMetadata = writeMetadata;
  skipCtx.artworkDir = artworkDir;
  skipCtx.dbCheckPossible = dbCheckPossible;
  skipCtx.sidecarCheckPossible = sidecarCheckPossible;
  skipCtx.wantedTypes = std::move(wantedTypes);
  skipCtx.metadataByPath = metadataByPath;
  skipCtx.presentByType = std::move(coverage.presentByType);
  skipCtx.frontFlatBases = std::move(coverage.frontFlatBases);
  // Which wanted media types the provider reliably supplies for this collection
  // (present for the vast majority of items) — only those block a FillMissing
  // skip; sparse / never-delivered types are optional (Kartend-ib46d). Computed
  // from the just-built collection-wide presence index over all candidate paths.
  skipCtx.requiredMediaTypes = Scraper::computePrevalentMediaTypes(
      skipCtx.presentByType, skipCtx.frontFlatBases, skipCtx.wantedTypes,
      static_cast<int>(paths.size()), Scraper::kMediaPrevalenceThreshold);
  skipCtx.hasWindow = hasWindow;
  skipCtx.cutoff = cutoff;

  QStringList kept;
  kept.reserve(paths.size());
  for (const QString &path : paths) {
    if (!Scraper::shouldSkipScrapedItem(path, skipCtx)) {
      kept.append(path);
    }
  }
  out.dropped = static_cast<int>(paths.size() - kept.size());
  out.kept = std::move(kept);
  return out;
}

void BatchScrapeRunner::filterAlreadyScraped() {
  PreFilterResult res = preFilterAlreadyScraped(
      dbMgr(), m_collectionUuid, m_artworkDir, m_mediaTypeFilter, m_fetchPrimaryCover,
      m_rescrapeMode, m_writeMetadata, m_skipRecentDays, m_paths);
  // The dropped items were intentionally skipped (Skip rescrape mode:
  // they already have metadata). Count them as `skipped` rather than
  // dropping them silently — otherwise scraped+skipped+errors never
  // reconciles with the total the caller computed before this filter,
  // and the items just vanish from the progress accounting.
  m_preSkippedCount = res.dropped;
  m_summary.skipped += m_preSkippedCount;
  m_paths = std::move(res.kept);
}

void BatchScrapeRunner::pump() {
  // m_cancelled and m_quotaStopped both stop NEW dispatch here. In the per-item
  // callbacks m_cancelled makes in-flight items abandon all remaining work;
  // m_quotaStopped (Kartend-fv3yr) is gentler — an item past lookup skips its
  // detail request, and an item past detail keeps its already-fetched metadata
  // but skips the media downloads, so in-flight items don't fire fresh requests
  // into an exhausted quota. Either way the drain below emits finished() once
  // the in-flight count returns to 0.
  if (m_cancelled || m_quotaStopped) {
    if (m_inFlight == 0 && !m_finishedEmitted) {
      m_finishedEmitted = true;
      emit finished(m_summary);
    }
    return;
  }
  // Fill empty slots until either the queue is exhausted or we've hit
  // the per-batch concurrency cap. Each new slot starts its own
  // independent lookup → detail → apply chain.
  while (m_inFlight < m_itemConcurrency && m_queueCursor < m_paths.size()) {
    auto state = std::make_shared<ItemState>();
    state->path = m_paths[m_queueCursor];
    state->queueIndex = m_queueCursor;
    ++m_queueCursor;
    ++m_inFlight;
    startItem(state);
  }
  // Queue drained and nothing in flight → done.
  if (m_inFlight == 0 && m_queueCursor >= m_paths.size() && !m_finishedEmitted) {
    m_finishedEmitted = true;
    emit finished(m_summary);
  }
}

void BatchScrapeRunner::startItem(const std::shared_ptr<ItemState> &state) {
  // Track this as the current/displayed item (skipCurrentItem targets it) and
  // register it so cancel() can flip its per-item token alongside the others.
  m_currentItem = state;
  m_inFlightItems.push_back(state);

  // Emit progress BEFORE the network call so the UI shows
  // "Scraping <name>" while the request is in flight.
  emit progress(m_summary.processedItems(), totalItemCount(), QFileInfo(state->path).fileName());

  const QString query = QFileInfo(state->path).completeBaseName();
  MetadataLookupProvider::LookupContext ctx{query, state->path, state->cancelToken};
  // QPointer guard: each per-item chain can outlive the runner if the
  // caller deletes us after cancel(). The lambda checks the pointer
  // before touching member state.
  QPointer<BatchScrapeRunner> self(this);
  // Stall guard for the lookup leg (Kartend audit xnm8a): the provider reads
  // and hashes the ROM before the HTTP query, and that file read has no
  // timeout — on a wedged mount it would otherwise hang this item, and the
  // whole batch, indefinitely. The watchdog shares `lookupDone` with the
  // callback so whichever fires first wins and the other no-ops.
  auto lookupDone = armStepWatchdog(state, QStringLiteral("metadata lookup"));
  m_provider->lookup(
      ctx, [self, state,
            lookupDone](const ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> &result) mutable {
        if (self.isNull() || lookupDone.fired()) return;
        lookupDone.finish();
        self->onLookupComplete(state, result);
      });
}

bool BatchScrapeRunner::cancelledFinish() {
  if (m_cancelled) {
    itemFinished();
    return true;
  }
  return false;
}

void BatchScrapeRunner::skipAndFinish() {
  ++m_summary.skipped;
  itemFinished();
}

bool BatchScrapeRunner::skippedByToken(const std::shared_ptr<ItemState> &state) {
  // User skipped this item (its token flipped, m_cancelled still false):
  // count it as skipped and free the slot so the batch carries on. A
  // deliberate skip is a terminal verdict — trim it from the resume list.
  if (state->cancelToken->load(std::memory_order_acquire)) {
    m_remainingPaths.removeOne(state->path);
    skipAndFinish();
    return true;
  }
  return false;
}

void BatchScrapeRunner::onLookupComplete(
    const std::shared_ptr<ItemState> &state,
    const ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> &result) {
  if (cancelledFinish()) return;
  if (skippedByToken(state)) return;
  if (result.isError()) {
    recordError(state->path, result.error());
    return;
  }
  const auto &candidates = result.value();
  if (candidates.isEmpty()) {
    // Kartend-e8aag: the provider ran and returned no match — the remote DB
    // has no entry for this item. A routine "not found", not a skip and not an
    // error. (TMDB / MusicBrainz / OpenLibrary / WebSearch all signal a miss
    // with an empty candidate list; ScreenScraper signals it as a 404, handled
    // in recordError.) Terminal: a not-found won't succeed on retry, so it
    // leaves the resume list. The provider answered normally, so it also
    // resets the fatal-error breaker streak.
    ++m_summary.notFound;
    resetFatalStreak();
    m_remainingPaths.removeOne(state->path);
    itemFinished();
    return;
  }
  if (m_quotaStopped) {
    // Kartend-fv3yr: a sibling item hit SS quota exhaustion (430/431) while
    // this item's lookup was in flight. Don't fire the detail request — it
    // would just burn another request against the exhausted quota (deepening a
    // 431 ban). Skip it; pump()'s drain still finishes the batch.
    skipAndFinish();
    return;
  }
  // Auto-pick the first candidate. The provider ranks candidates
  // by relevance, so the first one is what an interactive scrape
  // would default to.
  // NOTE on shared provider state: ScreenScraperProvider stashes
  // each full ScrapedItem in m_detailCache (an id-keyed, 64-entry
  // FIFO bounded by kMaxDetailCacheEntries) during lookup, and
  // fetchDetail returns it by providerSpecificId. The safety
  // invariant isn't synchronous read-after-write — it's threading:
  // lookup, fetchDetail, and the underlying HTTP-completion
  // callbacks all run on the main-thread event loop (HttpClient::get
  // asserts main-thread affinity), so there is no concurrent access
  // to the cache even with itemConcurrency > 1. Because the cache is
  // id-keyed and bounded rather than single-entry, the entry is safe
  // across event-loop turns: an interleaved lookup for another item
  // can't displace this item's detail before fetchDetail reads it.
  QPointer<BatchScrapeRunner> self(this);
  m_provider->fetchDetail(
      candidates.first(),
      [self, state](const ErrorUtils::Result<Scraper::ScrapedItem> &detailResult) {
        if (self.isNull()) return;
        self->onDetailComplete(state, detailResult);
      });
}

void BatchScrapeRunner::onDetailComplete(
    const std::shared_ptr<ItemState> &state,
    const ErrorUtils::Result<Scraper::ScrapedItem> &detailResult) {
  if (cancelledFinish()) return;
  if (skippedByToken(state)) return;
  if (detailResult.isError()) {
    recordError(state->path, detailResult.error());
    return;
  }
  // Mutable copy so we can stamp the known-absent set (wanted media the provider
  // returned nothing for) before it rides the queued Q_ARG(ScrapedItem) invoke
  // into the write worker, where it accumulates into ItemMetadata::mediaAbsent
  // (Kartend-kihyx). Computed here, the one place both m_mediaTypeFilter and the
  // provider's returned assets (scraped.media) are known together. The set logic
  // lives in the pure computeMediaAbsentTypes so it is unit-testable.
  Scraper::ScrapedItem scraped = detailResult.value();
  scraped.mediaAbsentThisRun =
      computeMediaAbsentTypes(m_rescrapeMode, m_mediaTypeFilter, scraped.media);
  if (m_quotaStopped) {
    // Kartend-fv3yr: quota was exhausted by a sibling between this
    // item's detail fetch and now. Keep the metadata we already have
    // (free) but skip the media downloads, which would burn more
    // requests against the exhausted quota.
    applyAndFinish(state, scraped, {});
    return;
  }
  // Media-fetch pass. Two modes depending on whether the
  // user supplied an explicit media-type filter:
  //   • filter empty → legacy "front cover only" path
  //     (gated by m_fetchPrimaryCover). Backwards-compat
  //     for callers that pre-date the per-type checkbox UI.
  //   • filter non-empty → fetch every asset whose `type`
  //     matches one of the requested types, in parallel.
  //     Pending-fetch count tracked on a shared aggregator
  //     so the final applyScrapedItem fires exactly once
  //     per item, with all successful bytes attached.
  if (!m_fetchPrimaryCover) {
    applyAndFinish(state, scraped, {});
    return;
  }
  const QList<Scraper::MediaAsset> wantedAssets = resolveWantedMediaAssets(scraped);
  if (wantedAssets.isEmpty()) {
    applyAndFinish(state, scraped, {});
    return;
  }
  fetchMediaAndFinish(state, scraped, wantedAssets);
}

void BatchScrapeRunner::itemFinished() {
  // One slot freed up. Drop the in-flight count and either start
  // another item from the queue (steady state) or emit `finished` if
  // the queue is drained AND no items are left in flight.
  --m_inFlight;
  // Drop weak handles whose ItemState was destroyed so m_inFlightItems tracks
  // only genuinely in-flight items — otherwise it grows by one stale entry per
  // started item across a long batch and cancel() iterates them all
  // (Kartend-p0oyj).
  std::erase_if(m_inFlightItems, [](const std::weak_ptr<ItemState> &w) { return w.expired(); });
  // Surface the provider's latest request quota to observers. Only
  // ScreenScraper reports a valid one (after its first response);
  // other providers return an invalid status and stay silent.
  if (m_provider) {
    const Scraper::QuotaStatus quota = m_provider->quotaStatus();
    if (quota.valid) {
      emit quotaUpdated(quota);
    }
  }
  pump();
}

} // namespace Scraper
