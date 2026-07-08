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
#include "batchscraperunner.h"

#include <atomic>
#include <utility>

#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>
#include <QTimer>

#include "applicationcontext.h"
#include "httpclient.h"
#include "idatabasemanager.h"
#include "loggingcategories.h"
#include "scrapepersistence.h"
#include "scrapeskipdecision.h"
#include "scrapewriteworker.h"

namespace Scraper {

namespace {
Q_LOGGING_CATEGORY(lcBatchScrape, "kartend.scrape.batch", QtWarningMsg)

/// Bounded join for the write worker thread. Ample for the worst case
/// (a single SQLite save on a busy disk) without risking a hang at
/// shutdown if the disk is wedged — we'd rather leak the thread than
/// block the UI on close. Kartend-n5m1c: lowered from 2000ms — this wait
/// runs back-to-back with the media-write drain on the GUI thread, so the
/// two budgets stacked to ~4s of unresponsive window on wedged storage.
/// The teardown is abandon-safe (the leaked thread + connection are reaped
/// at process exit), so a shorter budget costs only a leaked thread in the
/// rare timeout case, not correctness. Capped at ~2s combined now.
constexpr int kWriteWorkerShutdownWaitMs = 1000;
} // namespace

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

void BatchScrapeRunner::ensureWriteWorkerStarted() {
  // Null-DB carve-out: the test fixture and a few "metadata-only / no
  // persistence" callers pass nullptr. In that mode the runner
  // historically just no-op'd the save and treated it as success — we
  // preserve that by skipping the worker entirely. applyAndFinish has
  // a matching null-worker branch that runs the completion synchronously.
  if (!dbMgr()) return;
  if (m_writeWorker) return;

  // Connection name keyed on a process-global monotonic counter (the
  // g_connectionInstanceId pattern from databasemanager.cpp). It was
  // previously keyed on the runner's heap address — but when
  // shutdownWriteWorker()'s bounded join times out the thread (and its
  // still-registered connection) is intentionally leaked, and a later
  // runner allocated at the SAME address would addDatabase() under the
  // identical name, replacing the leaked thread's in-use connection
  // (Qt-documented UB, Kartend-fux2w). A counter never reuses a name.
  static std::atomic<quint64> s_writerInstanceId{0};
  const QString connectionName =
      QStringLiteral("kartend_scrape_writer_%1")
          .arg(s_writerInstanceId.fetch_add(1, std::memory_order_relaxed));

  // Threads aren't parented to the runner — same rationale as
  // DatabaseManager's worker threads (~QObject would auto-delete a
  // child thread mid-run, and ~QThread qFatals on a still-running
  // thread). shutdownWriteWorker() handles the bounded join.
  m_writeThread = new QThread();
  m_writeThread->setObjectName(QStringLiteral("ScrapeWriteWorker"));
  m_writeWorker = new ScrapeWriteWorker(connectionName);
  m_writeWorker->moveToThread(m_writeThread);
  // Worker self-destructs when its thread exits, so we don't have to
  // delete it explicitly on the main thread (which would race with
  // any in-flight queued performWrite slot dispatch).
  QObject::connect(m_writeThread, &QThread::finished, m_writeWorker, &QObject::deleteLater);
  // writeCompleted crosses the worker→main thread boundary as a
  // queued signal (Qt::AutoConnection auto-detects the thread
  // mismatch). The slot reads m_pendingWrites + ticks counters on
  // the main thread, which keeps all runner-state mutation single-
  // threaded.
  QObject::connect(m_writeWorker, &ScrapeWriteWorker::writeCompleted, this,
                   &BatchScrapeRunner::onWriteCompleted);

  m_writeThread->start();
  // Open the SQLite connection ON the worker thread (Qt SQL pins a
  // connection to its opening thread). Queued invocation guarantees
  // the slot runs in the worker's event loop, even though we're
  // calling from main.
  QMetaObject::invokeMethod(m_writeWorker, &ScrapeWriteWorker::openConnection,
                            Qt::QueuedConnection);
  m_writeWorkerShutdownDone = false;
}

void BatchScrapeRunner::shutdownWriteWorker() {
  if (m_writeWorkerShutdownDone) return;
  m_writeWorkerShutdownDone = true;

  if (m_writeWorker && m_writeThread) {
    // Queue the close so it runs on the worker thread (closing a
    // QSqlDatabase from a thread other than its owner is undefined).
    QMetaObject::invokeMethod(m_writeWorker, &ScrapeWriteWorker::closeConnection,
                              Qt::QueuedConnection);
  }
  if (m_writeThread) {
    m_writeThread->quit();
    if (m_writeThread->wait(kWriteWorkerShutdownWaitMs)) {
      delete m_writeThread;
    }
    // If wait() timed out the thread is intentionally leaked (and the
    // worker with it via its parent-thread association). Same trade
    // DatabaseManager makes — better than a UI hang on shutdown.
    m_writeThread = nullptr;
    m_writeWorker = nullptr;
  }
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

void BatchScrapeRunner::filterAlreadyScraped() {
  // No DB AND no artwork dir means nothing to check against — bail
  // before the loop so an empty test fixture (no DB, no artwork dir)
  // keeps every item, matching the legacy behaviour for those callers.
  auto *db = dbMgr();
  const bool dbCheckPossible = db && !m_collectionUuid.isEmpty();
  const bool sidecarCheckPossible = !m_artworkDir.isEmpty();
  if (!dbCheckPossible && !sidecarCheckPossible) return;

  // Effective "wanted" set under FillMissing — mirrors what the
  // per-item write phase would attempt for this run. mediaTypeFilter
  // non-empty wins; otherwise the legacy "front only" fallback
  // applies when fetchPrimaryCover is on (the runner header
  // documents this). With nothing wanted at all we have nothing to
  // pre-skip on, so leave the queue untouched.
  QSet<QString> wantedTypes;
  for (const QString &type : m_mediaTypeFilter) {
    wantedTypes.insert(type.toLower());
  }
  if (wantedTypes.isEmpty() && m_fetchPrimaryCover) {
    wantedTypes.insert(QStringLiteral("front"));
  }
  const bool isFillMissing = m_rescrapeMode == Scraper::RescrapeMode::FillMissing;
  if (isFillMissing && wantedTypes.isEmpty() && !m_writeMetadata) {
    // Nothing is being asked for; the run is a no-op anyway. Keep
    // the queue untouched so the caller's tallies match.
    return;
  }

  // Media-on-disk coverage indexes, pre-built once so the per-item skip check
  // is an O(1) hash lookup (Kartend audit 2w4wz).
  MediaCoverageIndex coverage =
      Scraper::buildMediaCoverageIndex(m_artworkDir, wantedTypes, sidecarCheckPossible);
  // updated_at is stored UTC ISO; compute the cutoff in UTC so the
  // comparison stays timezone-agnostic regardless of how the parsed
  // QDateTime's TimeSpec ends up after fromString().
  const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
  const bool hasWindow = m_skipRecentDays > 0;
  const QDateTime cutoff = hasWindow ? nowUtc.addDays(-m_skipRecentDays) : QDateTime();

  // Batch-fetch metadata for every candidate path up front. Without this
  // pre-flight, the per-item check below would issue one SELECT per path
  // on the GUI thread — a 1000-item collection burned multiple seconds
  // freezing the window before pump() even started.
  const QHash<QString, ItemMetadataStore::ItemMetadata> metadataByPath =
      dbCheckPossible ? db->loadItemMetadataBatch(m_collectionUuid, m_paths)
                      : QHash<QString, ItemMetadataStore::ItemMetadata>{};

  // Bundle the precomputed read-only context for the per-item skip
  // predicate. shouldSkipScrapedItem() consumes this once per path.
  ScrapeSkipContext skipCtx;
  skipCtx.mode = m_rescrapeMode;
  skipCtx.writeMetadata = m_writeMetadata;
  skipCtx.artworkDir = m_artworkDir;
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
      static_cast<int>(m_paths.size()), Scraper::kMediaPrevalenceThreshold);
  skipCtx.hasWindow = hasWindow;
  skipCtx.cutoff = cutoff;

  QStringList kept;
  kept.reserve(m_paths.size());
  for (const QString &path : m_paths) {
    if (!Scraper::shouldSkipScrapedItem(path, skipCtx)) {
      kept.append(path);
    }
  }
  // The dropped items were intentionally skipped (Skip rescrape mode:
  // they already have metadata). Count them as `skipped` rather than
  // dropping them silently — otherwise scraped+skipped+errors never
  // reconciles with the total the caller computed before this filter,
  // and the items just vanish from the progress accounting.
  m_preSkippedCount = static_cast<int>(m_paths.size() - kept.size());
  m_summary.skipped += m_preSkippedCount;
  m_paths = std::move(kept);
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
  m_provider->lookup(ctx, [self, state, lookupDone](
                              const ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> &result) {
    if (self.isNull() || *lookupDone) return;
    *lookupDone = true;
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
          [self, watcher, writeDone, state, effective, baseName]() {
            watcher->deleteLater();
            if (self.isNull() || *writeDone) return;
            *writeDone = true;
            self->onMediaWriteFinished(state, effective, baseName, watcher->result());
          });
  // Track the task and hand it the runner-lifetime cancel token
  // (Kartend-vi76q): the global pool has no per-runner drain, so the
  // destructor flips the token and waits on this list instead. Prune
  // finished entries first so the list stays O(itemConcurrency).
  m_inFlightMediaWrites.removeIf(
      [](const QFuture<Scraper::MediaWriteResult> &f) { return f.isFinished(); });
  const QFuture<Scraper::MediaWriteResult> writeFuture =
      QtConcurrent::run([artworkDir = m_artworkDir, baseName, writes, effective,
                         rescrapeMode = m_rescrapeMode, mediaCancel = m_mediaWriteCancel]() {
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
        Scraper::MediaWriteResult mediaRes =
            Scraper::writeMediaFiles(artworkDir, baseName, writes, rescrapeMode, mediaCancel);
        mediaRes.sidecarFailed = (sidecarOutcome == Scraper::SidecarWriteOutcome::Failed);
        return mediaRes;
      });
  m_inFlightMediaWrites.append(writeFuture);
  watcher->setFuture(writeFuture);
}

void BatchScrapeRunner::onMediaWriteFinished(const std::shared_ptr<ItemState> &state,
                                             const Scraper::ScrapedItem &effective,
                                             const QString &baseName,
                                             const Scraper::MediaWriteResult &writeRes) {
  if (m_cancelled) {
    itemFinished();
    return;
  }

  // Fold writeMediaFiles' genuine failures (disk full, mkpath failure, unsafe
  // remote-derived path) into the summary — previously they were dropped
  // here, so a disk-full batch reported "scraped N, 0 media" with zero
  // diagnostics (Kartend-jjyst.4). Benign rescrape-policy skips are not in
  // writeFailures, so FillMissing's kept-existing files don't flood the list.
  m_summary.mediaWriteFailures += static_cast<int>(writeRes.writeFailures.size());
  for (const QString &f : writeRes.writeFailures) {
    if (m_summary.firstFailures.size() >= kMaxReportedFailures) break;
    m_summary.firstFailures.append(QStringLiteral("%1: %2").arg(baseName, f));
  }

  const QStringList thumbPaths = resolveThumbnailPaths(baseName, writeRes.writtenPaths);

  // ── DB-write phase: dispatch ──────────────────────
  // Null-DB carve-out: the test fixture and a few metadata-only callers pass
  // nullptr. With no worker thread we keep the legacy synchronous "treat as
  // success" path so existing tests keep passing.
  if (!dbMgr() || !m_writeWorker) {
    ++m_summary.scraped;
    resetFatalStreak();
    m_summary.mediaWritten += writeRes.mediaWritten;
    if (writeRes.sidecarFailed) ++m_summary.sidecarFailures;
    m_remainingPaths.removeOne(state->path);
    emit itemCompleted(m_summary.processedItems(), totalItemCount(), effective, thumbPaths);
    itemFinished();
    return;
  }

  // Stash everything onWriteCompleted needs (the original ItemState for
  // cancellation/error context, the effective ScrapedItem for the
  // itemCompleted payload, the thumbPaths the dialog renders) keyed on a fresh
  // requestId. The worker emits writeCompleted(requestId, ok) once the SQLite
  // save returns.
  const quint64 requestId = ++m_nextWriteId;
  PendingWrite pending;
  pending.state = state;
  pending.scraped = effective;
  pending.writtenPaths = thumbPaths;
  pending.mediaWritten = writeRes.mediaWritten;
  pending.baseName = baseName;
  pending.sidecarFailed = writeRes.sidecarFailed;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    pending.dispatchedAtMs = QDateTime::currentMSecsSinceEpoch();
  }
  m_pendingWrites.insert(requestId, pending);

  // Queued cross-thread invocation. The worker handles the load → merge → save
  // against its own connection and queues writeCompleted back to the (main)
  // thread.
  QMetaObject::invokeMethod(m_writeWorker, "performWrite", Qt::QueuedConnection,
                            Q_ARG(quint64, requestId), Q_ARG(QString, m_collectionUuid),
                            Q_ARG(QString, state->path), Q_ARG(Scraper::ScrapedItem, effective),
                            Q_ARG(Scraper::NonStandardArtworkList, writeRes.nonStandardArtwork));
}

QStringList BatchScrapeRunner::resolveThumbnailPaths(const QString &baseName,
                                                     const QStringList &writtenPaths) const {
  // Probe for an existing primary-cover file on disk and append it to the
  // thumbnail-strip paths when no fresh write happened — FillMissing /
  // UpdateChanged commonly skip writing if the file already exists, but the
  // user still wants a visual ping for each item. Probes the canonical subdirs
  // the persistence layer can use (top-level mirror, /front/, /covers/,
  // /box-2D/, /screenshot/).
  QStringList thumbPaths = writtenPaths;
  if (thumbPaths.isEmpty() && !m_artworkDir.isEmpty()) {
    static const QStringList kProbeDirs = {QString(), QStringLiteral("front"),
                                           QStringLiteral("covers"), QStringLiteral("box-2D"),
                                           QStringLiteral("screenshot")};
    static const QStringList kProbeExts = {QStringLiteral("png"), QStringLiteral("jpg"),
                                           QStringLiteral("jpeg"), QStringLiteral("webp")};
    for (const QString &dir : kProbeDirs) {
      QString prefix = m_artworkDir;
      if (!dir.isEmpty()) prefix += QLatin1Char('/') + dir;
      prefix += QLatin1Char('/') + baseName + QLatin1Char('.');
      bool found = false;
      for (const QString &ext : kProbeExts) {
        const QString p = prefix + ext;
        if (QFileInfo::exists(p)) {
          thumbPaths.append(p);
          found = true;
          break;
        }
      }
      if (found) break;
    }
  }
  return thumbPaths;
}

void BatchScrapeRunner::onWriteCompleted(quint64 requestId, bool ok) {
  // Look up the pending row before doing anything else — a stray
  // duplicate emit (shouldn't happen, but defensive) would otherwise
  // double-tick counters.
  auto it = m_pendingWrites.find(requestId);
  if (it == m_pendingWrites.end()) return;
  PendingWrite pending = std::move(it.value());
  m_pendingWrites.erase(it);

  if (m_cancelled) {
    // Cancellation may have raced ahead of the worker reply. Don't
    // emit per-item completion; just drain the in-flight slot so the
    // queue can wind down.
    itemFinished();
    return;
  }

  if (!ok) {
    recordError(QStringLiteral("%1: metadata save failed").arg(pending.baseName),
                pending.state ? pending.state->path : QString());
    return;
  }

  // Worker bypassed DatabaseManager's main-thread cache. Invalidate
  // the per-item slot here so the next sidebar refresh sees the
  // freshly-saved metadata instead of the pre-scrape cached row.
  if (auto *db = dbMgr()) {
    db->invalidateMetadataCacheItem(m_collectionUuid, pending.state->path);
  }

  ++m_summary.scraped;
  resetFatalStreak();
  m_summary.mediaWritten += pending.mediaWritten;
  if (pending.sidecarFailed) ++m_summary.sidecarFailures;
  if (pending.state) {
    m_remainingPaths.removeOne(pending.state->path);
  }

  if (pending.dispatchedAtMs > 0) {
    // Perf trace: dispatch→writeCompleted latency = the window during
    // which the user's main thread is unblocked (vs the synchronous
    // pre-gnkb path where every per-item save blocked the UI for the
    // SQLite write duration). Drives Kartend-5vwt item 3 data.
    const qint64 latencyMs = QDateTime::currentMSecsSinceEpoch() - pending.dispatchedAtMs;
    qCDebug(lcPerfTrace).nospace() << "BatchScrapeRunner write: latencyMs=" << latencyMs
                                   << " requestId=" << requestId << " base=" << pending.baseName;
  }

  // Notify observers (ScraperService → dialog Live view) of the
  // freshly-written (or existing fallback) paths.
  emit itemCompleted(m_summary.processedItems(), totalItemCount(), pending.scraped,
                     pending.writtenPaths);
  itemFinished();
}

int BatchScrapeRunner::stepWatchdogMs() {
  // Read fresh (a getenv + parse, ~2x per item — negligible) rather than
  // cached, so a test can drive the budget to a few milliseconds per case and
  // a user can retune it without a restart. 10min default; env override lets a
  // user with multi-GB images on a slow mount raise it if a legitimate hash
  // ever approaches the ceiling (Kartend audit xnm8a).
  bool ok = false;
  const int v = qEnvironmentVariableIntValue("KARTEND_SCRAPE_STEP_TIMEOUT_MS", &ok);
  return (ok && v > 0) ? v : 600000;
}

std::shared_ptr<bool> BatchScrapeRunner::armStepWatchdog(const std::shared_ptr<ItemState> &state,
                                                         const QString &stageLabel) {
  auto done = std::make_shared<bool>(false);
  // Parented to `this`, so a runner torn down before the timer fires destroys
  // it cleanly (it never fires post-dtor — same guarantee as the media-write
  // watchers). On a normal completion the step's callback sets *done; this
  // timer still fires once at the budget, sees *done, and only deleteLater()s
  // itself. The self-clean keeps the call sites to a single `if (*done)` check.
  auto *timer = new QTimer(this);
  timer->setSingleShot(true);
  QPointer<BatchScrapeRunner> self(this);
  connect(timer, &QTimer::timeout, this, [self, timer, done, state, stageLabel]() {
    timer->deleteLater();
    if (self.isNull() || *done) return; // step already completed normally
    *done = true;
    self->onStepTimedOut(state, stageLabel);
  });
  timer->start(stepWatchdogMs());
  return done;
}

void BatchScrapeRunner::onStepTimedOut(const std::shared_ptr<ItemState> &state,
                                       const QString &stageLabel) {
  // An unbounded local step (provider ROM hash-read / artwork+sidecar write)
  // didn't finish within the watchdog budget — almost always a slow or wedged
  // storage mount. The blocked syscall can't be interrupted, so the worker
  // thread / QtConcurrent future is left to drain on its own (value-captures
  // only, like the destructor's abandon path) while we free this item's slot
  // and let the batch advance instead of freezing forever (Kartend audit
  // xnm8a).
  if (m_cancelled) {
    itemFinished();
    return;
  }
  qCWarning(lcBatchScrape) << "BatchScrapeRunner:" << stageLabel << "for"
                           << QFileInfo(state->path).fileName() << "exceeded" << stepWatchdogMs()
                           << "ms; erroring the item and advancing (storage may be unresponsive)";
  recordError(QStringLiteral("%1: %2 timed out (storage unresponsive?)")
                  .arg(QFileInfo(state->path).fileName(), stageLabel),
              state->path);
}

void BatchScrapeRunner::noteRateLimited429() {
  ++m_consecutive429Count;
  if (m_consecutive429Count < kConsecutive429StopThreshold || m_quotaStopped) return;
  // The limiter answered 429 to every recent request — it isn't a burst
  // we can ride out. Stop new dispatch via the quota-stop machinery so the
  // un-run work persists as the resume point (Kartend-jjyst.3).
  m_summary.quotaExhausted = true;
  m_quotaStopped = true;
  qCWarning(lcBatchScrape) << "BatchScrapeRunner:" << m_consecutive429Count
                           << "consecutive HTTP 429 rate-limit responses — stopping dispatch; "
                              "un-dispatched items stay queued for resume";
  if (m_summary.firstFailures.size() < kMaxReportedFailures) {
    m_summary.firstFailures.append(
        QStringLiteral("Stopped after %1 consecutive HTTP 429 rate-limit responses — the "
                       "provider is throttling this run; remaining items were left queued "
                       "for resume")
            .arg(m_consecutive429Count));
  }
}

void BatchScrapeRunner::recordError(const QString &reason, const QString &failedPath) {
  ++m_summary.errors;
  if (m_summary.firstFailures.size() < kMaxReportedFailures) {
    m_summary.firstFailures.append(reason);
  }
  // Kartend-jjjo5: retain the full path of every errored item so the dialog can
  // offer "re-scrape failed". Not-found / skipped items never reach here, so
  // they're excluded by construction. Bounded like firstFailures.
  if (!failedPath.isEmpty() && m_summary.failedPaths.size() < kMaxReportedFailures) {
    m_summary.failedPaths.append(failedPath);
  }
  // A genuine error is a terminal verdict (re-runnable via "re-scrape
  // failed"), so it leaves the resume list. Once the quota stop has flipped,
  // errors are kept instead: the quota-erroring item itself and anything
  // failing in its wake never really got a shot, and the persisted resume
  // point should retry them after the quota resets.
  if (!failedPath.isEmpty() && !m_quotaStopped) {
    m_remainingPaths.removeOne(failedPath);
  }
  itemFinished();
}

void BatchScrapeRunner::recordError(const QString &itemPath, const ErrorUtils::ErrorContext &err) {
  const QString itemName = QFileInfo(itemPath).fileName();
  // Kartend-e8aag: a provider "not found" (the remote DB genuinely has no
  // entry — an HTTP 404, or a miss a provider tagged RemoteResourceNotFound)
  // is a routine outcome, not a failure. Count it apart from errors and keep
  // it out of the failure list (and out of failedPaths — not-found items won't
  // succeed on retry against the same provider).
  if (err.code == ErrorUtils::ErrorCode::RemoteResourceNotFound || err.httpStatus == 404) {
    ++m_summary.notFound;
    // The provider answered normally — a healthy outcome for breaker purposes.
    resetFatalStreak();
    // Terminal like the empty-candidates branch: a not-found won't succeed on
    // retry, so it leaves the resume list.
    m_remainingPaths.removeOne(itemPath);
    itemFinished();
    return;
  }
  // Kartend-oa1ry: a quota-exhaustion response — as classified by the
  // provider that made the request (isQuotaExhausted; the base default covers
  // 429 plus ScreenScraper's non-standard 430/431) — means every remaining
  // item would just burn against an exhausted quota. Flag it so pump() stops
  // dispatching new items; in-flight items still finish (they're not gated on
  // m_quotaStopped).
  if (m_provider && m_provider->isQuotaExhausted(err)) {
    m_summary.quotaExhausted = true;
    m_quotaStopped = true;
  } else if (err.httpStatus == 429) {
    // A 429 landing here means the provider's transient retry either waited
    // out a Retry-After hint and still got throttled, or had no hint to wait
    // on. It's burst throttling, not daily-quota exhaustion — a single one no
    // longer halts the whole multi-collection run (Kartend-jjyst.3); the
    // queue stops only after kConsecutive429StopThreshold in a row. A 429 is
    // also a differently-shaped error for the fatal breaker below, so its
    // streak resets here — without resetting the 429 streak itself (which
    // resetFatalStreak() would).
    m_consecutiveFatalCount = 0;
    m_lastFatalStatus = 0;
    noteRateLimited429();
  } else if (err.httpStatus == 401 || err.httpStatus == 403 || err.httpStatus == 423 ||
             err.httpStatus == 426) {
    // A non-429 status breaks any consecutive-429 run (see resetFatalStreak;
    // this branch tracks its own streak instead of calling it).
    m_consecutive429Count = 0;
    // Circuit breaker (see the member doc): persistent auth/infra failures
    // fail every request identically — stop dispatch after N consecutive
    // identical statuses instead of firing one doomed request per item (each
    // failed lookup also deepens ScreenScraper's failed-lookup ban). Shares
    // the quota-stop machinery, so the un-dispatched work stays queued as
    // the persisted resume point for after the user fixes the cause.
    m_consecutiveFatalCount =
        (err.httpStatus == m_lastFatalStatus) ? m_consecutiveFatalCount + 1 : 1;
    m_lastFatalStatus = err.httpStatus;
    if (m_consecutiveFatalCount >= kFatalErrorBreakerThreshold && !m_quotaStopped) {
      m_summary.quotaExhausted = true;
      m_quotaStopped = true;
      qCWarning(lcBatchScrape) << "BatchScrapeRunner:" << m_consecutiveFatalCount
                               << "consecutive HTTP" << err.httpStatus
                               << "failures — stopping dispatch (check provider credentials / "
                                  "status); un-dispatched items stay queued for resume";
      if (m_summary.firstFailures.size() < kMaxReportedFailures) {
        m_summary.firstFailures.append(
            QStringLiteral("Stopped after %1 consecutive HTTP %2 failures — check provider "
                           "credentials / status; remaining items were left queued for resume")
                .arg(m_consecutiveFatalCount)
                .arg(err.httpStatus));
      }
    }
  } else {
    // A differently-shaped error breaks the "identical fatal" streak.
    resetFatalStreak();
  }
  // Kartend-e6oyu: record the enriched one-line summary (status + a server
  // detail snippet) rather than the bare err.message, so the failure list is
  // diagnosable instead of a wall of "HTTP request failed".
  // Kartend-jjjo5: pass the full path so the errored item can be re-queued.
  recordError(QStringLiteral("%1: %2").arg(itemName, err.userFacingSummary()), itemPath);
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
