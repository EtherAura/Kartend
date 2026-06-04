#include "scraperservice.h"

#include "applicationcontext.h"
#include "idatabasemanager.h"
#include "pathutils.h"
#include "scrapepersistence.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QLoggingCategory>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace Scraper {

namespace {
Q_LOGGING_CATEGORY(lcScraperService, "kartend.scraperservice", QtWarningMsg)
constexpr int kCurrentStateVersion = 1;
} // namespace

int ScraperService::PendingState::totalRemaining() const {
  int total = 0;
  for (const auto &j : queue) total += j.items.size();
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
  // Final flush so app shutdown mid-scrape preserves the latest
  // queue state for the next launch's resume prompt.
  flushPendingPersist();
}

void ScraperService::setContext(const Context &ctx) {
  m_ctx = ctx;
}

QString ScraperService::pendingStateFilePath() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  return QDir(dir).filePath(QStringLiteral("pending-scrape.json"));
}

QString ScraperService::pendingStateLockFilePath() {
  return pendingStateFilePath() + QStringLiteral(".lock");
}

bool ScraperService::acquireScrapeLock() {
  if (m_scrapeLock && m_scrapeLock->isLocked()) return true;
  const QString path = pendingStateLockFilePath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  m_scrapeLock = std::make_unique<QLockFile>(path);
  // Disable time-based staleness: a scrape legitimately runs for
  // minutes or hours, so the lock file's age says nothing. QLockFile
  // still treats a lock whose owning PID is no longer running as
  // stale (PID + process-name check) and reclaims it — exactly the
  // crashed-owner case we *want* to resume from.
  m_scrapeLock->setStaleLockTime(0);
  if (m_scrapeLock->tryLock(0)) return true;
  qCWarning(lcScraperService) << "pending-scrape state is owned by another live Kartend instance;"
                              << "this run will not be persisted for resume.";
  m_scrapeLock.reset();
  return false;
}

void ScraperService::releaseScrapeLock() {
  if (m_scrapeLock) {
    m_scrapeLock->unlock(); // also removes the .lock file
    m_scrapeLock.reset();
  }
}

bool ScraperService::pendingScrapeOwnedByLiveInstance() {
  QLockFile probe(pendingStateLockFilePath());
  probe.setStaleLockTime(0);
  if (probe.tryLock(0)) {
    // Acquired it ourselves → no live owner (nobody scraping, or the
    // previous owner crashed and its stale lock was reclaimed). Drop
    // it again; the actual run takes its own lock via acquireScrapeLock.
    probe.unlock();
    return false;
  }
  // tryLock failed: a live process holds it iff the error is
  // LockFailedError. Any other error (permissions, etc.) is treated
  // as "not owned" so a transient glitch can't strand a real resume.
  return probe.error() == QLockFile::LockFailedError;
}

int ScraperService::countQueueRemaining() const {
  int total = 0;
  for (const auto &j : m_queue) total += j.items.size();
  return total;
}

void ScraperService::startScrape(const QList<CollectionJob> &jobs, Mode mode,
                                 const QSet<QString> &mediaFilter, bool writeMetadata) {
  if (m_state != State::Idle) {
    qCWarning(lcScraperService) << "startScrape called while not idle, state="
                                << static_cast<int>(m_state);
    return;
  }
  // Take the ownership lock so a second instance sees this scrape as
  // live and won't offer to resume it. If another instance already
  // owns the state file, this run still proceeds (the user asked for
  // it) but persistState() will skip writing — see m_ownsStateFile.
  m_ownsStateFile = acquireScrapeLock();
  m_queue = jobs;
  m_queueCursor = 0;
  m_mode = mode;
  m_mediaFilter = mediaFilter;
  m_writeMetadata = writeMetadata;
  m_summary = Summary{};
  m_totalItemsAtStart = countQueueRemaining();
  m_itemsCompleted = 0;
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
}

void ScraperService::resumeFromState(const PendingState &state) {
  if (!state.isValid()) return;
  if (m_state != State::Idle) return;
  // Claim ownership before touching the queue. A failure here means
  // another live instance grabbed this scrape between loadPendingState
  // and now — resuming anyway would run it twice, so bail.
  if (!acquireScrapeLock()) {
    qCWarning(lcScraperService)
        << "Refusing to resume: another live Kartend instance owns this scrape.";
    return;
  }
  m_ownsStateFile = true;
  m_queue = state.queue;
  m_queueCursor = 0;
  m_mode = state.mode;
  m_mediaFilter = state.mediaFilter;
  m_writeMetadata = state.writeMetadata;
  m_summary = state.summarySoFar;
  m_totalItemsAtStart =
      countQueueRemaining() + m_summary.scraped + m_summary.skipped + m_summary.errors;
  m_itemsCompleted = m_summary.scraped + m_summary.skipped + m_summary.errors;
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
  if (m_autoRunner) {
    // An in-flight HTTP request can't be interrupted — waiting for the
    // runner's own `finished` (the old behaviour) leaves the UI frozen
    // until every in-flight lookup/download finishes or times out, so
    // Cancel looks dead. Instead: tell the runner to stop, detach it
    // (drop our signal connections so its drain doesn't drive the
    // service), and let it delete itself once its `finished` lands.
    // The synthesized finish below returns the UI to the setup view
    // immediately.
    m_autoRunner->cancel();
    disconnect(m_autoRunner, nullptr, this, nullptr);
    connect(m_autoRunner, &BatchScrapeRunner::finished, m_autoRunner, &QObject::deleteLater);
    m_autoRunner = nullptr;
  }
  // Interactive / paused: synthesize a finish.
  m_state = State::Finishing;
  clearStateFile();
  emit scrapeFinished(m_summary);
  m_state = State::Idle;
}

void ScraperService::pauseInteractive() {
  if (m_state != State::RunningInteractive) return;
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
    m_state = State::Finishing;
    clearStateFile();
    emit scrapeFinished(m_summary);
    m_state = State::Idle;
    return;
  }
  // Skip empty queue entries (defensive — applyPick/skipPick prune
  // them inline but a malformed resume could land here).
  while (m_queueCursor < m_queue.size() && m_queue[m_queueCursor].items.isEmpty()) {
    ++m_queueCursor;
  }
  if (m_queueCursor >= m_queue.size()) {
    m_state = State::Finishing;
    clearStateFile();
    emit scrapeFinished(m_summary);
    m_state = State::Idle;
    return;
  }
  m_currentCollectionName = m_queue[m_queueCursor].collectionName;
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
  m_summary.mediaWritten = m_summaryAtCollectionStart.mediaWritten + runnerSummary.mediaWritten;
  m_summary.firstFailures = m_summaryAtCollectionStart.firstFailures;
  for (const QString &f : runnerSummary.firstFailures) {
    if (m_summary.firstFailures.size() >= kMaxReportedFailures) break;
    m_summary.firstFailures.append(f);
  }
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
  // Remove the just-completed item from the persisted queue so a
  // resume picks up at the *next* unfinished item. The runner is
  // walking m_queue[m_queueCursor].items by value (it was copy-
  // constructed from `job.items` at startAutoCollection), so we own
  // the queue-side trim independently.
  if (m_queueCursor < m_queue.size() && !m_queue[m_queueCursor].items.isEmpty()) {
    m_queue[m_queueCursor].items.removeFirst();
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
                           << "quotaExhausted=" << summary.quotaExhausted;

  // Quota exhausted (HTTP 430/431): SS's daily allowance is spent, so
  // every remaining item in every remaining collection would just
  // fail. Stop the whole queue here. Crucially we do NOT clear the
  // current collection's items or advance m_queueCursor — the items
  // still un-scraped (the runner removes only successfully-scraped
  // ones) stay in the queue, and the schedulePersist below writes
  // them out so the user can resume after the quota resets. We also
  // do NOT clearStateFile() (unlike the normal queue-drained path in
  // pump()) — the persisted state IS the resume point.
  if (summary.quotaExhausted) {
    m_summary.quotaExhausted = true;
    if (m_autoRunner) {
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
                                  guard->interactiveLookupComplete(r);
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
    ++m_summary.errors;
    if (m_summary.firstFailures.size() < kMaxReportedFailures) {
      m_summary.firstFailures.append(QStringLiteral("%1: %2").arg(
          QFileInfo(m_currentItemPath).fileName(), result.error().message));
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
  if (result.value().isEmpty()) {
    ++m_summary.skipped;
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
  QJsonObject root;
  root[QStringLiteral("version")] = kCurrentStateVersion;
  root[QStringLiteral("started_at_unix_ms")] = m_startedAtMs;
  root[QStringLiteral("mode")] =
      m_mode == Mode::Auto ? QStringLiteral("auto") : QStringLiteral("interactive");
  root[QStringLiteral("write_metadata")] = m_writeMetadata;
  QJsonArray filterArr;
  for (const auto &t : m_mediaFilter) filterArr.append(t);
  root[QStringLiteral("media_filter")] = filterArr;
  QJsonObject sumObj;
  sumObj[QStringLiteral("scraped")] = m_summary.scraped;
  sumObj[QStringLiteral("skipped")] = m_summary.skipped;
  sumObj[QStringLiteral("errors")] = m_summary.errors;
  sumObj[QStringLiteral("media_written")] = m_summary.mediaWritten;
  QJsonArray failArr;
  for (const auto &f : m_summary.firstFailures) failArr.append(f);
  sumObj[QStringLiteral("first_failures")] = failArr;
  root[QStringLiteral("summary_so_far")] = sumObj;
  QJsonArray queueArr;
  for (int i = m_queueCursor; i < m_queue.size(); ++i) {
    const auto &j = m_queue[i];
    if (j.items.isEmpty()) continue;
    QJsonObject jObj;
    jObj[QStringLiteral("collection_index")] = j.collectionIndex;
    jObj[QStringLiteral("collection_uuid")] = j.collectionUuid;
    jObj[QStringLiteral("collection_name")] = j.collectionName;
    jObj[QStringLiteral("artwork_dir")] = j.artworkDir;
    QJsonArray itemsArr;
    for (const auto &p : j.items) itemsArr.append(p);
    jObj[QStringLiteral("remaining")] = itemsArr;
    queueArr.append(jObj);
  }
  root[QStringLiteral("queue")] = queueArr;

  const QString path = pendingStateFilePath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  // QSaveFile streams into a temp sibling and atomically renames on
  // commit() — so a crash mid-write can't leave a truncated
  // pending-scrape.json. A partial file fails loadPendingState()'s JSON
  // parse, which silently suppresses the resume prompt on next launch.
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    qCWarning(lcScraperService) << "Failed to write pending state to" << path;
    return;
  }
  // Compact rather than Indented: smaller file, faster to format, and
  // the file is machine-read only — no human ever edits this.
  f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  if (!f.commit()) {
    qCWarning(lcScraperService) << "Failed to commit pending state to" << path;
  }
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
    const QString path = pendingStateFilePath();
    if (QFile::exists(path)) QFile::remove(path);
  }
  releaseScrapeLock();
  m_ownsStateFile = false;
}

ScraperService::PendingState ScraperService::loadPendingState(bool consumeOnLoad) {
  PendingState out;
  const QString path = pendingStateFilePath();
  QFile f(path);
  if (!f.exists() || !f.open(QIODevice::ReadOnly)) return out;
  // A pending file that belongs to a still-running Kartend instance is
  // NOT an interrupted scrape — it's a live one. Return an invalid
  // state so the caller skips the resume prompt; the owning instance
  // clears the file itself when its scrape finishes. Never consume it
  // here, even with consumeOnLoad set.
  if (pendingScrapeOwnedByLiveInstance()) {
    qCInfo(lcScraperService) << "pending-scrape.json belongs to a running Kartend instance — "
                                "skipping resume.";
    return out;
  }
  const QByteArray bytes = f.readAll();
  f.close();
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    qCWarning(lcScraperService) << "Bad pending-scrape.json:" << err.errorString();
    if (consumeOnLoad) QFile::remove(path);
    return out;
  }
  const QJsonObject root = doc.object();
  const int version = root.value(QStringLiteral("version")).toInt(0);
  if (version != kCurrentStateVersion) {
    qCWarning(lcScraperService) << "Unsupported pending-scrape.json version" << version;
    if (consumeOnLoad) QFile::remove(path);
    return out;
  }
  out.startedAtUnixMs =
      static_cast<qint64>(root.value(QStringLiteral("started_at_unix_ms")).toDouble(0));
  out.mode = root.value(QStringLiteral("mode")).toString() == QLatin1String("interactive")
                 ? Mode::Interactive
                 : Mode::Auto;
  out.writeMetadata = root.value(QStringLiteral("write_metadata")).toBool(true);
  const auto filterArr = root.value(QStringLiteral("media_filter")).toArray();
  for (const auto &v : filterArr) out.mediaFilter.insert(v.toString());
  const auto sumObj = root.value(QStringLiteral("summary_so_far")).toObject();
  out.summarySoFar.scraped = sumObj.value(QStringLiteral("scraped")).toInt(0);
  out.summarySoFar.skipped = sumObj.value(QStringLiteral("skipped")).toInt(0);
  out.summarySoFar.errors = sumObj.value(QStringLiteral("errors")).toInt(0);
  out.summarySoFar.mediaWritten = sumObj.value(QStringLiteral("media_written")).toInt(0);
  const auto failArr = sumObj.value(QStringLiteral("first_failures")).toArray();
  for (const auto &v : failArr) out.summarySoFar.firstFailures.append(v.toString());
  const auto queueArr = root.value(QStringLiteral("queue")).toArray();
  for (const auto &v : queueArr) {
    const auto jo = v.toObject();
    CollectionJob job;
    job.collectionIndex = jo.value(QStringLiteral("collection_index")).toInt(-1);
    job.collectionUuid = jo.value(QStringLiteral("collection_uuid")).toString();
    job.collectionName = jo.value(QStringLiteral("collection_name")).toString();
    job.artworkDir = jo.value(QStringLiteral("artwork_dir")).toString();
    const auto items = jo.value(QStringLiteral("remaining")).toArray();
    for (const auto &p : items) job.items.append(p.toString());
    if (!job.items.isEmpty()) out.queue.append(job);
  }
  out.hasState = !out.queue.isEmpty();
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
  const QString path = pendingStateFilePath();
  if (QFile::exists(path)) QFile::remove(path);
  // Drop the now-orphaned lock file too so the config dir stays tidy;
  // QLockFile would reclaim it as stale anyway.
  QFile::remove(pendingStateLockFilePath());
}

} // namespace Scraper
