#include "scraperservice.h"

#include "applicationcontext.h"
#include "collection/typehelpers.h"
#include "httpclient.h"
#include "idatabasemanager.h"
#include "isettingsmanager.h"
#include "pathutils.h"
#include "scrapelock.h"
#include "scrapependingstate.h"
#include "scrapepersistence.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QLoggingCategory>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>
#include <QTimer>
#include <utility>

namespace Scraper {

namespace {
Q_LOGGING_CATEGORY(lcScraperService, "kartend.scraperservice", QtWarningMsg)
} // namespace

int ScraperService::PendingState::totalRemaining() const {
  int total = 0;
  // Entity jobs carry no item list but are one unit of work each — mirror
  // countQueueRemaining() so the resume prompt's "remaining" count is right
  // (Kartend-ckepd.2).
  for (const auto &j : queue) total += j.isEntityJob() ? 1 : j.items.size();
  return total;
}

ScraperService::ScraperService(QObject *parent) : QObject(parent) {
  // 250ms coalescing window keeps per-item updates off the disk-write
  // path during heavy auto-scrape runs; on shutdown / pause / cancel
  // we flush synchronously so no resume progress is lost.
  m_persistTimer = new QTimer(this);
  m_persistTimer->setSingleShot(true);
  m_persistTimer->setInterval(250);
  connect(m_persistTimer, &QTimer::timeout, this, &ScraperService::flushPendingPersist);
}

ScraperService::~ScraperService() {
  // Stop any in-flight entity media writes, then give them a bounded drain —
  // same contract as ~BatchScrapeRunner: the token makes the write lambdas
  // exit within one in-progress asset, so the full budget is only consumed on
  // genuinely wedged storage, and abandoning past it is safe (the lambdas
  // capture values + the shared token only, never `this`).
  m_entityWriteCancel->store(true, std::memory_order_release);
  constexpr int kEntityWriteDrainBudgetMs = 1000;
  QDeadlineTimer deadline(kEntityWriteDrainBudgetMs);
  bool abandoned = false;
  for (auto &future : m_inFlightEntityWrites) {
    while (!future.isFinished() && !deadline.hasExpired()) {
      QThread::msleep(10);
    }
    if (!future.isFinished()) abandoned = true;
  }
  if (abandoned) {
    qCWarning(lcScraperService)
        << "entity media writes did not drain in" << kEntityWriteDrainBudgetMs
        << "ms during destruction; abandoned them (value-captures only — the in-progress "
           "asset may still finish writing to disk)";
  }
  // Final flush so app shutdown mid-scrape preserves the latest
  // queue state for the next launch's resume prompt.
  flushPendingPersist();
}

void ScraperService::setContext(const Context &ctx) {
  m_ctx = ctx;
}

int ScraperService::countQueueRemaining() const {
  int total = 0;
  // An entity job is one unit of work (no item list); a normal job counts its
  // remaining paths (Kartend-ckepd.2).
  for (const auto &j : m_queue) total += j.isEntityJob() ? 1 : j.items.size();
  return total;
}

bool ScraperService::startScrape(const QList<CollectionJob> &jobs, Mode mode,
                                 const QSet<QString> &mediaFilter, bool writeMetadata) {
  if (m_state != State::Idle) {
    qCWarning(lcScraperService) << "startScrape called while not idle, state="
                                << static_cast<int>(m_state);
    return false;
  }
  // Take the ownership lock so a second instance sees this scrape as
  // live and won't offer to resume it. If another instance already
  // owns the state file, this run still proceeds (the user asked for
  // it) but persistState() will skip writing — see m_ownsStateFile.
  m_ownsStateFile = m_scrapeLock.acquire(ScrapePendingState::lockFilePath());
  ++m_runGeneration; // invalidate any stale in-flight entity-fetch callback
  // Fresh cancel token per run: cancel() flips the old one, and any write
  // still draining under it must not suppress this run's writes.
  m_entityWriteCancel = std::make_shared<std::atomic<bool>>(false);
  m_queue = jobs;
  m_queueCursor = 0;
  m_mode = mode;
  m_mediaFilter = mediaFilter;
  m_writeMetadata = writeMetadata;
  m_consecutive429Count = 0;
  m_summary = Summary{};
  // Kartend-resxp: Auto mode pre-filters already-covered items inside
  // BatchScrapeRunner::start(); interactive mode must drop them from its
  // queue up front instead, or a Skip/FillMissing run prompts the user for
  // every item regardless of coverage. Runs before the totals below so the
  // dropped items are counted as skipped-and-settled from the first tick,
  // the same way a resumed run folds processedItems() into its totals.
  if (m_mode == Mode::Interactive) {
    m_summary.skipped += preFilterInteractiveQueue();
  }
  m_totalItemsAtStart = countQueueRemaining() + m_summary.skipped;
  m_itemsCompleted = m_summary.skipped;
  m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
  m_currentCollectionName.clear();
  m_currentItemPath.clear();
  m_lastScrapedItem = Scraper::ScrapedItem();
  m_recentMediaPaths.clear();
  m_pausedItemPath.clear();
  m_totalBytesDownloaded = 0;
  m_bytesAtCollectionStart = 0;

  // Enter the running state *before* the initial persist. persistState()
  // bails out when m_state is Idle, and pump() (below) is what sets the
  // running state — so without this the startup snapshot is never
  // written, and a crash before the first debounced persist leaves
  // nothing on disk to resume. pump() re-affirms the same state.
  m_state = m_mode == Mode::Auto ? State::RunningAuto : State::RunningInteractive;
  emit scrapeStarted(m_totalItemsAtStart);
  persistState();
  pump();
  return true;
}

int ScraperService::preFilterInteractiveQueue() {
  if (!m_ctx.generalSettings) return 0;
  const auto rescrapeMode =
      static_cast<Scraper::RescrapeMode>(m_ctx.generalSettings->scraper.options.rescrapeMode);
  // Overwrite / UpdateChanged intentionally visit every item — Overwrite
  // always re-fetches and UpdateChanged needs the bytes back to compare —
  // so only Skip / FillMissing pay for the coverage indexes. Same gate as
  // BatchScrapeRunner::start().
  if (rescrapeMode != Scraper::RescrapeMode::Skip &&
      rescrapeMode != Scraper::RescrapeMode::FillMissing) {
    return 0;
  }
  IDatabaseManager *db = m_ctx.ctx ? m_ctx.ctx->databaseManager() : nullptr;
  const int skipRecentDays = m_ctx.generalSettings->scraper.options.skipRecentScrapeDays;
  int dropped = 0;
  for (auto &job : m_queue) {
    if (job.isEntityJob() || job.items.isEmpty()) continue;
    BatchScrapeRunner::PreFilterResult res = BatchScrapeRunner::preFilterAlreadyScraped(
        db, job.collectionUuid, job.artworkDir, m_mediaFilter,
        /*fetchPrimaryCover=*/true, rescrapeMode, m_writeMetadata, skipRecentDays, job.items);
    if (res.dropped > 0) {
      qCInfo(lcScraperService) << "interactive pre-filter:" << job.collectionName << "dropped"
                               << res.dropped << "of" << job.items.size()
                               << "already-covered items";
    }
    dropped += res.dropped;
    job.items = std::move(res.kept);
  }
  return dropped;
}

void ScraperService::resumeFromState(const PendingState &state) {
  if (!state.isValid()) return;
  if (m_state != State::Idle) return;
  // Claim ownership before touching the queue. A failure here means
  // another live instance grabbed this scrape between loadPendingState
  // and now — resuming anyway would run it twice, so bail.
  if (!m_scrapeLock.acquire(ScrapePendingState::lockFilePath())) {
    qCWarning(lcScraperService)
        << "Refusing to resume: another live Kartend instance owns this scrape.";
    return;
  }
  m_ownsStateFile = true;
  ++m_runGeneration; // invalidate any stale in-flight entity-fetch callback
  m_entityWriteCancel = std::make_shared<std::atomic<bool>>(false); // see startScrape
  m_queue = state.queue;
  m_queueCursor = 0;
  m_mode = state.mode;
  m_mediaFilter = state.mediaFilter;
  m_writeMetadata = state.writeMetadata;
  m_consecutive429Count = 0;
  m_summary = state.summarySoFar;
  // The persisted flag documents WHY the previous leg stopped; the user chose
  // to resume (quota presumably reset), so the new leg starts unexhausted —
  // otherwise a clean resumed run would still report quota death at the end.
  m_summary.quotaExhausted = false;
  // Collection indices are NOT stable across sessions (add/remove/reorder,
  // playlist resync) but the persisted index is used to build the provider
  // (per-collection overrides / systemeid) and — for entity jobs — to mutate
  // that collection's config fields. Re-resolve every job's live index from
  // its stable UUID and drop jobs whose UUID no longer resolves; trusting a
  // stale index would silently scrape against (and write art into) another
  // collection's config. Legacy v1 snapshots without a UUID keep their index.
  if (m_ctx.collections) {
    QList<CollectionJob> liveQueue;
    liveQueue.reserve(m_queue.size());
    for (CollectionJob job : std::as_const(m_queue)) {
      if (!job.collectionUuid.isEmpty()) {
        const int liveIndex = CollectionUtils::indexForUuid(*m_ctx.collections, job.collectionUuid);
        if (liveIndex < 0) {
          qCWarning(lcScraperService)
              << "Dropping resumed job for" << job.collectionName
              << "— its collection UUID no longer resolves (collection removed or renamed)";
          continue;
        }
        job.collectionIndex = liveIndex;
        if (job.isEntityJob()) job.entity.collectionIndex = liveIndex;
      }
      liveQueue.append(job);
    }
    m_queue = std::move(liveQueue);
    // Failed items re-queue against the live list too ("re-scrape failed"
    // after a resume). Unresolvable entries go to -1; the dialog's grouping
    // skips out-of-range owners.
    for (auto &failed : m_summary.failedItems) {
      if (failed.collectionUuid.isEmpty()) continue;
      failed.collectionIndex =
          CollectionUtils::indexForUuid(*m_ctx.collections, failed.collectionUuid);
    }
  }
  m_totalItemsAtStart = countQueueRemaining() + m_summary.processedItems();
  m_itemsCompleted = m_summary.processedItems();
  m_startedAtMs =
      state.startedAtUnixMs > 0 ? state.startedAtUnixMs : QDateTime::currentMSecsSinceEpoch();
  m_currentCollectionName.clear();
  m_currentItemPath.clear();
  m_lastScrapedItem = Scraper::ScrapedItem();
  m_recentMediaPaths.clear();
  m_pausedItemPath.clear();
  // Enter the running state before the initial persist — see
  // startScrape(): persistState() needs a non-Idle state to write, and
  // pump() only sets it afterwards.
  m_state = m_mode == Mode::Auto ? State::RunningAuto : State::RunningInteractive;
  emit scrapeStarted(m_totalItemsAtStart);
  persistState();
  pump();
}

void ScraperService::cancel() {
  if (m_state == State::Idle) return;
  // Establish the terminal state BEFORE triggering any drain. Both branches
  // below call HttpClient::clearPending() (directly, or inside
  // BatchScrapeRunner::cancel()), which resolves queued callbacks
  // SYNCHRONOUSLY on this stack. If we drained while still RunningAuto/
  // RunningInteractive with our slots connected, a synchronously-resolved
  // callback could re-enter onAutoFinished / interactiveLookupComplete,
  // advance the queue, and start the next collection detached — plus a
  // double scrapeFinished. Setting Finishing first makes those callbacks'
  // `m_state != Running…` guards short-circuit to a clean return.
  m_state = State::Finishing;
  // Bump so a still-in-flight entity-fetch callback from this run no-ops when
  // it resolves (Kartend-ckepd.2 review).
  ++m_runGeneration;
  // Stop any queued/in-progress entity media write promptly — the token is
  // polled between assets inside writeMediaFiles, so cancel interrupts a
  // multi-asset platform-art write instead of letting it run to completion.
  m_entityWriteCancel->store(true, std::memory_order_release);
  if (m_autoRunner) {
    // An in-flight HTTP request can't be interrupted — waiting for the
    // runner's own `finished` (the old behaviour) leaves the UI frozen
    // until every in-flight lookup/download finishes or times out, so
    // Cancel looks dead. Instead: tell the runner to stop, detach it
    // (drop our signal connections so its drain doesn't drive the
    // service), and let it delete itself once its `finished` lands.
    // The synthesized finish below returns the UI to the setup view
    // immediately.
    //
    // Detach BEFORE cancel(): the runner's cancel() drains queued HTTP
    // callbacks synchronously, and the last one can emit finished() on this
    // stack. Dropping our slots first means that finished can't re-enter
    // onAutoFinished (which would ++m_queueCursor + pump() the next
    // collection). Null the service pointer first too, so the finished→
    // deleteLater connect below binds the runner that's actually going away.
    BatchScrapeRunner *runner = m_autoRunner;
    m_autoRunner = nullptr;
    disconnect(runner, nullptr, this, nullptr);
    connect(runner, &BatchScrapeRunner::finished, runner, &QObject::deleteLater);
    runner->cancel();
  } else {
    // No runner (interactive lookup or entity fan-out in flight): those
    // requests queue in the same per-host HttpClient queues — drop the
    // not-yet-dispatched ones so a cancelled run stops burning quota and a
    // fresh run doesn't contend behind dead requests. The Finishing state set
    // above makes any synchronously-resolved interactive callback no-op. (The
    // runner path does this inside BatchScrapeRunner::cancel().)
    HttpClient::instance()->clearPending();
  }
  // Interactive / paused: synthesize a finish.
  clearStateFile();
  emit scrapeFinished(m_summary);
  m_state = State::Idle;
}

void ScraperService::skipCurrentItem() {
  if (m_autoRunner) {
    m_autoRunner->skipCurrentItem();
  }
}

void ScraperService::pauseInteractive() {
  if (m_state != State::RunningInteractive) return;
  // Invalidate any in-flight entity fetch / media-download callbacks so one
  // that resolves AFTER a resume (which re-dispatches the item from scratch)
  // can't double-advance the queue or double-count (Kartend-ckepd.3 review).
  // Mirrors cancel()/startScrape's generation bump.
  ++m_runGeneration;
  m_state = State::PausedInteractive;
  emit scrapePaused();
  persistState();
}

void ScraperService::resumePaused() {
  if (m_state != State::PausedInteractive) return;
  m_state = State::RunningInteractive;
  emit scrapeResumed();
  // Re-fire the lookup for the currently-cued item.
  startInteractiveItem();
}

void ScraperService::applyPick(const Scraper::ScrapedItem &item) {
  if (m_state != State::RunningInteractive) return;
  if (m_queueCursor >= m_queue.size()) return;
  auto &job = m_queue[m_queueCursor];
  if (job.items.isEmpty()) return;
  // The dialog has already persisted via its applyResult callback by
  // the time it calls us; we only book-keep here.
  ++m_summary.scraped;
  ++m_itemsCompleted;
  m_lastScrapedItem = item;
  job.items.removeFirst();
  if (job.items.isEmpty()) {
    ++m_queueCursor;
  }
  schedulePersist();
  // Fire a synthetic itemCompleted so the Live view's metadata panel
  // updates. Media paths are unknown to the service in interactive
  // mode (the dialog's apply path writes them); leave the list empty.
  emit itemCompleted(m_itemsCompleted, m_totalItemsAtStart, item, QStringList{});
  pump();
}

void ScraperService::skipPick() {
  if (m_state != State::RunningInteractive) return;
  if (m_queueCursor >= m_queue.size()) return;
  auto &job = m_queue[m_queueCursor];
  if (job.items.isEmpty()) return;
  ++m_summary.skipped;
  ++m_itemsCompleted;
  job.items.removeFirst();
  if (job.items.isEmpty()) ++m_queueCursor;
  schedulePersist();
  pump();
}

void ScraperService::pump() {
  // Auto-mode: the BatchScrapeRunner drives item iteration; we just
  // walk collections when each runner finishes. Interactive-mode:
  // we drive item-by-item ourselves.
  if (m_queueCursor >= m_queue.size()) {
    // COMPLETED queue (cancel and quota-stop exit elsewhere): match
    // manufacturer logos against the company data this run just enriched
    // before announcing the finish (Kartend-cnti4).
    m_entityCoordinator.applyManufacturerLogos();
    m_state = State::Finishing;
    clearStateFile();
    emit scrapeFinished(m_summary);
    m_state = State::Idle;
    return;
  }
  // Skip empty queue entries (defensive — applyPick/skipPick prune
  // them inline but a malformed resume could land here). Entity jobs have no
  // item list but ARE work, so they must not be skipped (Kartend-ckepd.2).
  while (m_queueCursor < m_queue.size() && m_queue[m_queueCursor].items.isEmpty() &&
         !m_queue[m_queueCursor].isEntityJob()) {
    ++m_queueCursor;
  }
  if (m_queueCursor >= m_queue.size()) {
    m_entityCoordinator.applyManufacturerLogos(); // completed queue, as above
    m_state = State::Finishing;
    clearStateFile();
    emit scrapeFinished(m_summary);
    m_state = State::Idle;
    return;
  }
  m_currentCollectionName = m_queue[m_queueCursor].collectionName;
  // An entity scrape is a single provider fetch with no per-item picker, so it
  // dispatches the same way regardless of Auto / Interactive mode.
  if (m_queue[m_queueCursor].isEntityJob()) {
    m_state = m_mode == Mode::Auto ? State::RunningAuto : State::RunningInteractive;
    m_entityCoordinator.startEntityCollection();
    return;
  }
  if (m_mode == Mode::Auto) {
    m_state = State::RunningAuto;
    startAutoCollection();
  } else {
    m_state = State::RunningInteractive;
    startInteractiveItem();
  }
}

void ScraperService::startAutoCollection() {
  // Kartend-m02z: ScraperService::Context now carries the full
  // ApplicationContext; treat the absence of a usable DB through ctx the
  // same as the old databaseManager==nullptr branch.
  auto *db = m_ctx.ctx ? m_ctx.ctx->databaseManager() : nullptr;
  if (!m_ctx.providerBuilder || !db || !m_ctx.generalSettings) {
    // Whole collection fails — count every item as an error (not just
    // one) so scraped + skipped + errors still reconciles with the
    // total, and record a reason so the error-details popup explains it.
    const int failed = m_queue[m_queueCursor].items.size();
    m_summary.errors += failed;
    m_itemsCompleted += failed;
    if (m_summary.firstFailures.size() < kMaxReportedFailures) {
      m_summary.firstFailures.append(
          QStringLiteral("%1: scraper not configured").arg(m_queue[m_queueCursor].collectionName));
    }
    m_queue[m_queueCursor].items.clear();
    ++m_queueCursor;
    schedulePersist();
    pump();
    return;
  }
  auto &job = m_queue[m_queueCursor];
  qCInfo(lcScraperService) << "startAutoCollection idx=" << job.collectionIndex
                           << "name=" << job.collectionName << "items=" << job.items.size();
  auto provider = m_ctx.providerBuilder(job.collectionIndex);
  if (!provider) {
    qCWarning(lcScraperService) << "no provider applies for collection" << job.collectionName;
    m_summary.firstFailures.append(
        QStringLiteral("%1: no provider applies").arg(job.collectionName));
    m_summary.errors += job.items.size();
    m_itemsCompleted += job.items.size();
    job.items.clear();
    ++m_queueCursor;
    schedulePersist();
    pump();
    return;
  }
  m_priorAtCollectionStart = m_itemsCompleted;
  m_currentCollectionItemCount = job.items.size();
  m_summaryAtCollectionStart = m_summary;
  m_bytesAtCollectionStart = m_totalBytesDownloaded;
  const auto rescrapeMode =
      static_cast<Scraper::RescrapeMode>(m_ctx.generalSettings->scraper.options.rescrapeMode);
  const int itemConcurrency = m_ctx.generalSettings->scraper.options.batchItemConcurrency;
  const int skipRecentDays = m_ctx.generalSettings->scraper.options.skipRecentScrapeDays;
  m_autoRunner = new BatchScrapeRunner(
      m_ctx.ctx, std::move(provider), job.collectionUuid, job.items, job.artworkDir,
      /*fetchPrimaryCover=*/true, rescrapeMode, itemConcurrency, skipRecentDays, this);
  m_autoRunner->setMediaTypeFilter(m_mediaFilter);
  m_autoRunner->setWriteMetadata(m_writeMetadata);

  connect(m_autoRunner, &BatchScrapeRunner::progress, this,
          [this](int doneInCol, int totalInCol, const QString &name) {
            onAutoItemBegan(doneInCol, totalInCol, name);
          });
  connect(m_autoRunner, &BatchScrapeRunner::itemCompleted, this,
          [this](int doneInCol, int totalInCol, const Scraper::ScrapedItem &scraped,
                 const QStringList &paths) {
            onAutoItemCompleted(doneInCol, totalInCol, scraped, paths);
          });
  connect(m_autoRunner, &BatchScrapeRunner::finished, this,
          [this](const BatchScrapeRunner::Summary &s) { onAutoFinished(s); });
  // Pass the runner's per-account quota updates straight through to
  // the dialog. Main-thread → main-thread, so no qRegisterMetaType.
  connect(m_autoRunner, &BatchScrapeRunner::quotaUpdated, this, &ScraperService::quotaUpdated);
  // Kartend-ou0a: forward the runner's hashing/extracting stage to our
  // own itemStageChanged so the unified-mode dialog can show the same
  // progress label regardless of whether the work came from
  // startInteractiveItem (interactive picker flow) or this auto runner.
  connect(m_autoRunner, &BatchScrapeRunner::itemStageChanged, this,
          &ScraperService::itemStageChanged);
  m_autoRunner->start();
}

void ScraperService::rollRunnerSummaryIntoSummary(const BatchScrapeRunner::Summary &runnerSummary) {
  // Roll the active runner's per-collection counts onto the
  // pre-collection snapshot. SET semantics (not +=) so calling this
  // from every itemBegan / itemCompleted / finished hook stays
  // idempotent. firstFailures is rebuilt here too: it used to be
  // copied only at collection-finish, so the error-details popup was
  // empty while a collection was still scraping even though the error
  // *count* had already ticked up — "errors appear, but no detail".
  m_summary.scraped = m_summaryAtCollectionStart.scraped + runnerSummary.scraped;
  m_summary.skipped = m_summaryAtCollectionStart.skipped + runnerSummary.skipped;
  m_summary.errors = m_summaryAtCollectionStart.errors + runnerSummary.errors;
  m_summary.notFound = m_summaryAtCollectionStart.notFound + runnerSummary.notFound;
  m_summary.mediaWritten = m_summaryAtCollectionStart.mediaWritten + runnerSummary.mediaWritten;
  m_summary.mediaFetchFailures =
      m_summaryAtCollectionStart.mediaFetchFailures + runnerSummary.mediaFetchFailures;
  m_summary.mediaWriteFailures =
      m_summaryAtCollectionStart.mediaWriteFailures + runnerSummary.mediaWriteFailures;
  m_summary.sidecarFailures =
      m_summaryAtCollectionStart.sidecarFailures + runnerSummary.sidecarFailures;
  m_summary.firstFailures = m_summaryAtCollectionStart.firstFailures;
  for (const QString &f : runnerSummary.firstFailures) {
    if (m_summary.firstFailures.size() >= kMaxReportedFailures) break;
    m_summary.firstFailures.append(f);
  }
  // Kartend-jjjo5: tag each errored path with the collection currently being
  // scraped so the dialog can rebuild one CollectionJob per owner. SET
  // semantics, same as firstFailures — m_queueCursor still points at the active
  // collection when this runs (it advances only after onAutoFinished).
  m_summary.failedItems = m_summaryAtCollectionStart.failedItems;
  const bool haveJob = m_queueCursor < m_queue.size();
  const int curIdx = haveJob ? m_queue[m_queueCursor].collectionIndex : -1;
  const QString curUuid = haveJob ? m_queue[m_queueCursor].collectionUuid : QString();
  for (const QString &p : runnerSummary.failedPaths) {
    if (m_summary.failedItems.size() >= kMaxReportedFailures) break;
    m_summary.failedItems.append({curIdx, p, curUuid, /*isEntity=*/false, {}});
  }
}

bool ScraperService::noteRateLimited429() {
  ++m_consecutive429Count;
  if (m_consecutive429Count < BatchScrapeRunner::kConsecutive429StopThreshold) return false;
  // The limiter answered 429 to every recent request — it isn't a burst we
  // can ride out. The caller escalates through stopForQuotaExhaustion() so
  // the un-finished work persists as the resume point (Kartend-jjyst.15,
  // mirroring the runner's noteRateLimited429). Log/report only at the
  // tripping call: an entity media fan-out settles all its assets before
  // the stop lands, so post-trip 429s re-enter here.
  if (m_consecutive429Count == BatchScrapeRunner::kConsecutive429StopThreshold) {
    qCWarning(lcScraperService) << m_consecutive429Count
                                << "consecutive HTTP 429 rate-limit responses — stopping the "
                                   "queue; un-finished work stays queued for resume";
    if (m_summary.firstFailures.size() < kMaxReportedFailures) {
      m_summary.firstFailures.append(
          QStringLiteral("Stopped after %1 consecutive HTTP 429 rate-limit responses — the "
                         "provider is throttling this run; remaining items were left queued "
                         "for resume")
              .arg(m_consecutive429Count));
    }
  }
  return true;
}

void ScraperService::stopForQuotaExhaustion() {
  // Mirrors onAutoFinished's quota branch: do NOT advance the cursor or clear
  // the state file — the persisted queue (current job included) IS the resume
  // point the user continues from after the quota resets.
  m_summary.quotaExhausted = true;
  schedulePersist();
  flushPendingPersist(); // ensure the resume point hits disk now
  m_state = State::Finishing;
  emit scrapeFinished(m_summary);
  m_state = State::Idle;
}

void ScraperService::onAutoItemBegan(int doneInCol, int /*totalInCol*/, const QString &name) {
  m_currentItemPath = name;
  // The runner's progress signal counts ALL completions (success +
  // skip + error). Reset itemsCompleted to the authoritative value so
  // the progress bar advances even for errored items mid-collection.
  m_itemsCompleted = m_priorAtCollectionStart + doneInCol;
  // Roll the per-collection counts into m_summary so the dialog's
  // Live view "Scraped X · Skipped Y · Errors Z" line ticks on each
  // item start instead of jumping at the end of the collection.
  if (m_autoRunner) {
    rollRunnerSummaryIntoSummary(m_autoRunner->currentSummary());
    m_totalBytesDownloaded = m_bytesAtCollectionStart + m_autoRunner->totalBytesDownloaded();
    // Sync the persisted remaining list here too: errored / not-found items
    // never fire itemCompleted, but they DO tick progress — without this a
    // crash after a run of pure failures would persist a stale remaining
    // list. Cheap: an implicitly-shared list copy per tick.
    if (m_queueCursor < m_queue.size()) {
      m_queue[m_queueCursor].items = m_autoRunner->remainingPaths();
      schedulePersist();
    }
  }
  qCDebug(lcScraperService) << "itemBegan name=" << name << "doneInCol=" << doneInCol
                            << "totalCompleted=" << m_itemsCompleted << "of" << m_totalItemsAtStart;
  emit itemBegan(m_itemsCompleted, m_totalItemsAtStart, m_currentCollectionName, name);
}

void ScraperService::onAutoItemCompleted(int doneInCol, int /*totalInCol*/,
                                         const Scraper::ScrapedItem &scraped,
                                         const QStringList &paths) {
  // Sync itemsCompleted to the runner's authoritative count (post-
  // success). onAutoItemBegan resets this on every progress emit, but
  // a runner with one-at-a-time concurrency emits itemCompleted
  // BEFORE the next item's progress, so we need to advance here too.
  m_itemsCompleted = m_priorAtCollectionStart + doneInCol;
  // Same per-item summary roll-up as onAutoItemBegan — captures the
  // tail-end item the next progress() never fires for.
  if (m_autoRunner) {
    rollRunnerSummaryIntoSummary(m_autoRunner->currentSummary());
    m_totalBytesDownloaded = m_bytesAtCollectionStart + m_autoRunner->totalBytesDownloaded();
  }
  m_lastScrapedItem = scraped;
  appendRecentMedia(paths);
  qCDebug(lcScraperService) << "itemCompleted scraped.title=" << scraped.title
                            << "mediaPaths=" << paths.size()
                            << "totalCompleted=" << m_itemsCompleted;
  // Sync the persisted remaining list from the runner, which trims items by
  // identity as they reach a terminal outcome. The old positional
  // removeFirst() dropped the HEAD of the list whenever ANY item succeeded —
  // with interleaved errors (or itemConcurrency > 1, where completion order
  // != queue order) a resume would re-scrape completed items and never
  // revisit dropped ones.
  if (m_queueCursor < m_queue.size() && m_autoRunner) {
    m_queue[m_queueCursor].items = m_autoRunner->remainingPaths();
  }
  schedulePersist();
  emit itemCompleted(m_itemsCompleted, m_totalItemsAtStart, scraped, paths);
}

void ScraperService::onAutoFinished(const BatchScrapeRunner::Summary &summary) {
  // m_summary is already rolling per-item via the per-progress/
  // -itemCompleted hooks above, so a final snap to
  // (m_summaryAtCollectionStart + summary) settles any drift without
  // double-counting. SET semantics — same helper as the per-item
  // hooks — so the firstFailures list isn't double-appended.
  rollRunnerSummaryIntoSummary(summary);
  if (m_autoRunner) {
    m_totalBytesDownloaded = m_bytesAtCollectionStart + m_autoRunner->totalBytesDownloaded();
  }
  qCInfo(lcScraperService) << "onAutoFinished collection done — scraped=" << summary.scraped
                           << "skipped=" << summary.skipped << "errors=" << summary.errors
                           << "notFound=" << summary.notFound
                           << "quotaExhausted=" << summary.quotaExhausted;

  // Quota exhausted (HTTP 430/431): SS's daily allowance is spent, so
  // every remaining item in every remaining collection would just
  // fail. Stop the whole queue here. Crucially we do NOT clear the
  // current collection's items or advance m_queueCursor — the runner's
  // remaining list (everything without a real terminal verdict,
  // quota-stopped work included) stays in the queue, and the
  // schedulePersist below writes it out so the user can resume after
  // the quota resets. We also do NOT clearStateFile() (unlike the
  // normal queue-drained path in pump()) — the persisted state IS the
  // resume point.
  if (summary.quotaExhausted) {
    m_summary.quotaExhausted = true;
    if (m_autoRunner) {
      // Final remaining-list sync before the runner goes away: keeps the
      // quota-erroring item and everything undispatched, drops whatever
      // settled with a real verdict before the stop.
      if (m_queueCursor < m_queue.size()) {
        m_queue[m_queueCursor].items = m_autoRunner->remainingPaths();
      }
      m_autoRunner->deleteLater();
      m_autoRunner = nullptr;
    }
    schedulePersist();
    flushPendingPersist(); // ensure the resume point hits disk now
    m_state = State::Finishing;
    emit scrapeFinished(m_summary);
    m_state = State::Idle;
    return;
  }

  // A collection that finished without a quota/429 stop proves the limiter
  // is letting requests through — end any entity/interactive 429 streak
  // (Kartend-jjyst.15; the runner keeps its own counter).
  m_consecutive429Count = 0;
  // Backfill itemsCompleted to the full collection size — progress
  // signal stops just before the last item completes (no signal fires
  // for "the last item is done"), so we explicitly snap to the total
  // here. m_priorAtCollectionStart was set when this collection
  // began.
  m_itemsCompleted = m_priorAtCollectionStart + m_currentCollectionItemCount;
  // Drop any unfinished items in the queue's current entry (cancel
  // path) so the persistence file reflects what actually ran.
  if (m_queueCursor < m_queue.size()) {
    m_queue[m_queueCursor].items.clear();
  }
  if (m_autoRunner) {
    m_autoRunner->deleteLater();
    m_autoRunner = nullptr;
  }
  ++m_queueCursor;
  schedulePersist();
  pump();
}

void ScraperService::startInteractiveItem() {
  if (m_queueCursor >= m_queue.size()) {
    pump();
    return;
  }
  auto &job = m_queue[m_queueCursor];
  // Entity jobs have no per-item picker; dispatch them the same way pump()
  // does, so the interactive pause/resume path (resumePaused → here) doesn't
  // silently skip an entity job as if its empty item list meant "done"
  // (Kartend-ckepd.2 review).
  if (job.isEntityJob()) {
    m_entityCoordinator.startEntityCollection();
    return;
  }
  if (job.items.isEmpty()) {
    ++m_queueCursor;
    pump();
    return;
  }
  // Rebuild provider per collection switch — m_interactiveProvider
  // is keyed on the current collection. Cheap; the registry call is
  // synchronous.
  if (m_currentCollectionName != job.collectionName || !m_interactiveProvider) {
    if (m_ctx.providerBuilder) {
      m_interactiveProvider = m_ctx.providerBuilder(job.collectionIndex);
      // Kartend-ou0a: wire stage reporter through the service so the
      // batch / interactive UIs can show "Hashing ROM…" / "Extracting
      // archive…" instead of a frozen spinner. QPointer-guarded so a
      // service destroyed mid-lookup doesn't UAF when the worker
      // thread's continuation invokes the reporter.
      if (m_interactiveProvider) {
        QPointer<ScraperService> guard(this);
        m_interactiveProvider->setStageReporter([guard](const QString &stage) {
          if (guard) emit guard->itemStageChanged(stage);
        });
      }
    }
    m_currentCollectionName = job.collectionName;
  }
  if (!m_interactiveProvider) {
    m_summary.firstFailures.append(
        QStringLiteral("%1: no provider applies").arg(job.collectionName));
    m_summary.errors += job.items.size();
    m_itemsCompleted += job.items.size();
    job.items.clear();
    ++m_queueCursor;
    schedulePersist();
    pump();
    return;
  }
  const QString filePath = job.items.first();
  m_currentItemPath = filePath;
  m_pausedItemPath = filePath;
  const QString name = QFileInfo(filePath).fileName();
  emit itemBegan(m_itemsCompleted, m_totalItemsAtStart, job.collectionName, name);

  const QString queryText = QFileInfo(filePath).completeBaseName();
  MetadataLookupProvider::LookupContext ctx{queryText, filePath};
  QPointer<ScraperService> guard(this);
  m_interactiveProvider->lookup(ctx,
                                [guard](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> r) {
                                  if (guard.isNull()) return;
                                  guard->interactiveLookupComplete(std::move(r));
                                });
}

void ScraperService::interactiveLookupComplete(
    ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
  if (m_state != State::RunningInteractive) {
    // Not actively running interactive: either paused (the dialog was
    // closed mid-lookup — the resume path re-fires the lookup) or the
    // run was cancelled / finished. Either way this stale result must
    // not advance the queue or, worse, restart a cancelled scrape.
    return;
  }
  if (result.isError()) {
    const auto &err = result.error();
    // Quota exhaustion (as classified by the provider) stops the whole
    // queue with a resume point — the per-item auto-advance below would
    // otherwise machine-gun every remaining lookup into the exhausted quota
    // at one doomed request per item, deepening a failed-lookup ban. The
    // current item stays queued (its lookup consumed nothing scrappable).
    if (m_interactiveProvider && m_interactiveProvider->isQuotaExhausted(err)) {
      qCWarning(lcScraperService) << "interactive lookup hit provider quota (HTTP" << err.httpStatus
                                  << ") — stopping the queue with a resume point";
      stopForQuotaExhaustion();
      return;
    }
    // Consecutive-429 escalation (Kartend-jjyst.15): a lone 429 just fails
    // this item below, but a streak means the limiter isn't letting up —
    // stop like a quota death, with THIS item still queued as part of the
    // resume point (its lookup consumed nothing scrapable).
    if (err.httpStatus == 429) {
      if (noteRateLimited429()) {
        stopForQuotaExhaustion();
        return;
      }
    } else {
      m_consecutive429Count = 0;
    }
    // Kartend-e8aag: a 404 / RemoteResourceNotFound is a routine "not found",
    // counted apart from errors and kept out of the failure list.
    if (err.code == ErrorUtils::ErrorCode::RemoteResourceNotFound || err.httpStatus == 404) {
      ++m_summary.notFound;
    } else {
      ++m_summary.errors;
      if (m_summary.firstFailures.size() < kMaxReportedFailures) {
        // Kartend-e6oyu: enriched summary (status + detail) over bare message.
        m_summary.firstFailures.append(QStringLiteral("%1: %2").arg(
            QFileInfo(m_currentItemPath).fileName(), err.userFacingSummary()));
      }
      // Kartend-jjjo5: retain the errored path (tagged with its collection)
      // for "re-scrape failed".
      if (m_summary.failedItems.size() < kMaxReportedFailures) {
        const bool haveJob = m_queueCursor < m_queue.size();
        const int idx = haveJob ? m_queue[m_queueCursor].collectionIndex : -1;
        const QString uuid = haveJob ? m_queue[m_queueCursor].collectionUuid : QString();
        m_summary.failedItems.append({idx, m_currentItemPath, uuid, /*isEntity=*/false, {}});
      }
    }
    ++m_itemsCompleted;
    if (m_queueCursor < m_queue.size() && !m_queue[m_queueCursor].items.isEmpty()) {
      m_queue[m_queueCursor].items.removeFirst();
    }
    if (m_queueCursor < m_queue.size() && m_queue[m_queueCursor].items.isEmpty()) {
      ++m_queueCursor;
    }
    schedulePersist();
    pump();
    return;
  }
  // A delivered lookup result (match or clean not-found) proves the limiter
  // let the request through — it ends any consecutive-429 run.
  m_consecutive429Count = 0;
  if (result.value().isEmpty()) {
    // Kartend-e8aag: provider returned no match — "not found", not skipped.
    ++m_summary.notFound;
    ++m_itemsCompleted;
    if (m_queueCursor < m_queue.size() && !m_queue[m_queueCursor].items.isEmpty()) {
      m_queue[m_queueCursor].items.removeFirst();
    }
    if (m_queueCursor < m_queue.size() && m_queue[m_queueCursor].items.isEmpty()) {
      ++m_queueCursor;
    }
    schedulePersist();
    pump();
    return;
  }
  // Hand the candidates off to whoever's listening. The UI is
  // expected to call applyPick / skipPick / pauseInteractive next.
  auto &job = m_queue[m_queueCursor];
  emit pickerNeeded(m_currentItemPath, QFileInfo(m_currentItemPath).fileName(), result.value(),
                    m_interactiveProvider, job.artworkDir);
}

void ScraperService::appendRecentMedia(const QStringList &paths) {
  for (const QString &p : paths) {
    if (p.isEmpty()) continue;
    m_recentMediaPaths.append(p);
  }
  // Cap to a fixed history so the thumbnail strip doesn't grow
  // unbounded across a long batch.
  while (m_recentMediaPaths.size() > kRecentMediaCapacity) {
    m_recentMediaPaths.removeFirst();
  }
}

void ScraperService::persistState() {
  // Another live instance owns pending-scrape.json — never write over
  // it (that would corrupt its resume state) and never remove it.
  if (!m_ownsStateFile) return;
  if (m_state == State::Idle) {
    clearStateFile();
    return;
  }
  // No queue → nothing to resume.
  if (countQueueRemaining() == 0) {
    clearStateFile();
    return;
  }
  // Snapshot the live run into the public PendingState shape and let the
  // serialization module do the byte-level work (JSON build + atomic write).
  PendingState snap;
  snap.hasState = true;
  snap.startedAtUnixMs = m_startedAtMs;
  snap.mode = m_mode;
  snap.writeMetadata = m_writeMetadata;
  snap.mediaFilter = m_mediaFilter;
  snap.summarySoFar = m_summary;
  snap.queue = m_queue.mid(m_queueCursor);
  ScrapePendingState::atomicWrite(ScrapePendingState::filePath(),
                                  ScrapePendingState::serialize(snap));
}

void ScraperService::schedulePersist() {
  m_persistDirty = true;
  if (m_persistTimer && !m_persistTimer->isActive()) m_persistTimer->start();
}

void ScraperService::flushPendingPersist() {
  if (!m_persistDirty) return;
  m_persistDirty = false;
  if (m_persistTimer) m_persistTimer->stop();
  persistState();
}

void ScraperService::clearStateFile() {
  // Pending debounced write would just re-create the file — cancel
  // it so a queued schedulePersist() doesn't resurrect the state.
  m_persistDirty = false;
  if (m_persistTimer) m_persistTimer->stop();
  // Only this run's owner may delete the file. A concurrent secondary
  // scrape that never got the lock must leave the real owner's state
  // intact when it finishes.
  if (m_ownsStateFile) {
    const QString path = ScrapePendingState::filePath();
    if (QFile::exists(path)) QFile::remove(path);
  }
  m_scrapeLock.release();
  m_ownsStateFile = false;
}

ScraperService::PendingState ScraperService::loadPendingState(bool consumeOnLoad) {
  PendingState out;
  const QString path = ScrapePendingState::filePath();
  QFile f(path);
  if (!f.exists() || !f.open(QIODevice::ReadOnly)) return out;
  // A pending file that belongs to a still-running Kartend instance is
  // NOT an interrupted scrape — it's a live one. Return an invalid
  // state so the caller skips the resume prompt; the owning instance
  // clears the file itself when its scrape finishes. Never consume it
  // here, even with consumeOnLoad set.
  if (ScrapeLock::ownedByLiveInstance(ScrapePendingState::lockFilePath())) {
    qCInfo(lcScraperService) << "pending-scrape.json belongs to a running Kartend instance — "
                                "skipping resume.";
    return out;
  }
  const QByteArray bytes = f.readAll();
  f.close();
  // Parse failures and unsupported versions come back as an invalid state
  // (deserialize logs the reason); consuming in that case discards the
  // unusable file exactly as the valid-load consume does.
  out = ScrapePendingState::deserialize(bytes);
  if (consumeOnLoad) QFile::remove(path);
  return out;
}

void ScraperService::discardPendingState() {
  // Explicit user-driven discard from the resume prompt. This path is
  // only reachable when loadPendingState() returned a valid state,
  // which it does only for a scrape *not* owned by a live instance —
  // so an unconditional remove here can't clobber a running scrape.
  m_persistDirty = false;
  if (m_persistTimer) m_persistTimer->stop();
  const QString path = ScrapePendingState::filePath();
  if (QFile::exists(path)) QFile::remove(path);
  // Drop the now-orphaned lock file too so the config dir stays tidy;
  // QLockFile would reclaim it as stale anyway.
  QFile::remove(ScrapePendingState::lockFilePath());
}

} // namespace Scraper
