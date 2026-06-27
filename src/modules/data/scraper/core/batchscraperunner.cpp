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
#include "idatabasemanager.h"
#include "loggingcategories.h"
#include "scrapepersistence.h"
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

  // Build a basename-set per wanted type so the per-item check is an
  // O(1) hash lookup instead of a directory scan per item. Files in
  // each type's subdir are indexed by lowercase complete base name
  // (the runner uses completeBaseName when assembling per-item paths,
  // so the same key roundtrips).
  QHash<QString, QSet<QString>> presentByType;
  if (sidecarCheckPossible) {
    for (const QString &type : wantedTypes) {
      const QString subdir = QDir(m_artworkDir).filePath(type);
      QDir d(subdir);
      if (!d.exists()) continue;
      QSet<QString> bases;
      const auto files = d.entryList(QDir::Files | QDir::NoDotAndDotDot);
      bases.reserve(files.size());
      for (const QString &f : files) {
        bases.insert(QFileInfo(f).completeBaseName().toLower());
      }
      // operator[] returns T& so the assignment is a real move; QHash's
      // (const Key&, const T&) insert overload would have copied.
      presentByType[type] = std::move(bases);
    }
  }
  // `front` also mirrors to the flat artwork directory ({base}.<ext>)
  // for the grid tile — that is the slot the auto-discoverer reads.
  // Treat either location as "front covered".
  QSet<QString> frontFlatBases;
  if (sidecarCheckPossible && wantedTypes.contains(QStringLiteral("front"))) {
    static const QStringList kImageGlobs = {QStringLiteral("*.png"),  QStringLiteral("*.jpg"),
                                            QStringLiteral("*.jpeg"), QStringLiteral("*.webp"),
                                            QStringLiteral("*.gif"),  QStringLiteral("*.bmp")};
    QDir d(m_artworkDir);
    const auto files = d.entryList(kImageGlobs, QDir::Files | QDir::NoDotAndDotDot);
    frontFlatBases.reserve(files.size());
    for (const QString &f : files) {
      frontFlatBases.insert(QFileInfo(f).completeBaseName().toLower());
    }
  }
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
  skipCtx.dbCheckPossible = dbCheckPossible;
  skipCtx.sidecarCheckPossible = sidecarCheckPossible;
  skipCtx.wantedTypes = std::move(wantedTypes);
  skipCtx.metadataByPath = metadataByPath;
  skipCtx.presentByType = std::move(presentByType);
  skipCtx.frontFlatBases = std::move(frontFlatBases);
  skipCtx.hasWindow = hasWindow;
  skipCtx.cutoff = cutoff;

  QStringList kept;
  kept.reserve(m_paths.size());
  for (const QString &path : m_paths) {
    if (!shouldSkipScrapedItem(path, skipCtx)) {
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

bool BatchScrapeRunner::shouldSkipScrapedItem(const QString &path,
                                              const ScrapeSkipContext &ctx) const {
  // Helper: decide whether the metadata "slot" is covered for an item.
  // Returns one of three states so the caller can apply the time-window
  // gate consistently for both DB and on-disk evidence.
  struct MetaPresence {
    bool present = false;
    bool hasTimestamp = false;
    QDateTime timestampUtc;
  };
  auto metadataPresenceFor = [&](const QString &p, const QString &baseName) -> MetaPresence {
    MetaPresence out;
    if (ctx.dbCheckPossible) {
      const auto md = ctx.metadataByPath.value(p);
      if (!md.source.isEmpty()) {
        out.present = true;
        // ItemMetadataStore writes updated_at via
        // QDateTime::currentDateTimeUtc().toString(Qt::ISODate), which
        // emits UTC values without the "Z" suffix; fromString() on
        // that returns a LocalTime-spec datetime carrying UTC values.
        // Appending the "Z" before parsing makes ISODate tag the
        // result as UTC directly — avoids the Qt 6.5-deprecated
        // setTimeSpec(Qt::UTC) re-label while staying portable to
        // CI's Qt 6.4.
        QString tsStr = md.updatedAt;
        if (!tsStr.isEmpty() && !tsStr.endsWith(QLatin1Char('Z'))) {
          tsStr.append(QLatin1Char('Z'));
        }
        const QDateTime ts = QDateTime::fromString(tsStr, Qt::ISODate);
        if (ts.isValid()) {
          out.hasTimestamp = true;
          out.timestampUtc = ts;
        }
      }
    }
    if (!out.present && ctx.sidecarCheckPossible && !baseName.isEmpty()) {
      const QString sidecar =
          QDir(m_artworkDir)
              .filePath(QStringLiteral("metadata/") + baseName + QStringLiteral(".json"));
      const QFileInfo fi(sidecar);
      if (fi.exists() && fi.isFile()) {
        out.present = true;
        const QDateTime mtime = fi.lastModified().toUTC();
        if (mtime.isValid()) {
          out.hasTimestamp = true;
          out.timestampUtc = mtime;
        }
      }
    }
    return out;
  };

  // Helper: per-type media-on-disk check using the pre-built indexes.
  auto typeCoveredFor = [&](const QString &baseNameLower, const QString &type) {
    if (type == QStringLiteral("front") && ctx.frontFlatBases.contains(baseNameLower)) {
      return true;
    }
    return ctx.presentByType.value(type).contains(baseNameLower);
  };

  // Helper: apply the window to a presence record. "Within window"
  // means the saved timestamp is at-or-after the cutoff. Items with
  // no readable timestamp keep the safe behaviour (preserve the
  // skip) — the user can clear the row or delete the file if they
  // really want a refresh.
  auto withinWindow = [&](const MetaPresence &mp) {
    if (!ctx.hasWindow) return true;
    if (!mp.hasTimestamp) return true;
    return mp.timestampUtc >= ctx.cutoff;
  };

  const QString baseName = QFileInfo(path).completeBaseName();
  const QString baseNameLower = baseName.toLower();
  const MetaPresence meta = metadataPresenceFor(path, baseName);

  bool skipThis = false;
  if (m_rescrapeMode == Scraper::RescrapeMode::Skip) {
    // Skip mode: any metadata marker is enough — the user told us
    // "if scraped, leave it alone." The time window optionally
    // releases stale items back for refresh.
    if (meta.present && withinWindow(meta)) {
      skipThis = true;
    }
  } else { // FillMissing
    // FillMissing only burns a request when there is at least one
    // missing field for this item. Walk the user's ticked
    // checkboxes (_metadata implicit via m_writeMetadata, plus the
    // resolved mediaTypeFilter). If every ticked field is already
    // covered AND the existing data is within the refresh window,
    // there is nothing the provider could give us — skip.
    bool fullyCovered = true;
    if (m_writeMetadata) {
      if (!meta.present || !withinWindow(meta)) {
        fullyCovered = false;
      }
    }
    if (fullyCovered) {
      for (const QString &type : ctx.wantedTypes) {
        if (!typeCoveredFor(baseNameLower, type)) {
          fullyCovered = false;
          break;
        }
      }
    }
    // Media-only runs (m_writeMetadata == false) still want to
    // honour the refresh window when *any* timestamp is available
    // (sidecar or DB row). If the only signal of staleness is the
    // sidecar mtime, treat that as the run's freshness anchor.
    if (fullyCovered && !m_writeMetadata && meta.present && !withinWindow(meta)) {
      fullyCovered = false;
    }
    skipThis = fullyCovered;
  }

  return skipThis;
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
  emit progress(m_summary.scraped + m_summary.skipped + m_summary.errors, totalItemCount(),
                QFileInfo(state->path).fileName());

  const QString query = QFileInfo(state->path).completeBaseName();
  MetadataLookupProvider::LookupContext ctx{query, state->path, state->cancelToken};
  // QPointer guard: each per-item chain can outlive the runner if the
  // caller deletes us after cancel(). The lambda checks the pointer
  // before touching member state.
  QPointer<BatchScrapeRunner> self(this);
  m_provider->lookup(ctx, [self,
                           state](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
    if (self.isNull()) return;
    if (self->m_cancelled) {
      self->itemFinished();
      return;
    }
    if (state->cancelToken->load(std::memory_order_acquire)) {
      // User skipped this item (its token flipped, m_cancelled still false):
      // count it as skipped and free the slot so the batch carries on.
      ++self->m_summary.skipped;
      self->itemFinished();
      return;
    }
    if (result.isError()) {
      self->recordError(QFileInfo(state->path).fileName(), result.error());
      return;
    }
    const auto candidates = result.value();
    if (candidates.isEmpty()) {
      ++self->m_summary.skipped;
      self->itemFinished();
      return;
    }
    if (self->m_quotaStopped) {
      // Kartend-fv3yr: a sibling item hit SS quota exhaustion (430/431) while
      // this item's lookup was in flight. Don't fire the detail request — it
      // would just burn another request against the exhausted quota (deepening a
      // 431 ban). Skip it; pump()'s drain still finishes the batch.
      ++self->m_summary.skipped;
      self->itemFinished();
      return;
    }
    // Auto-pick the first candidate. The provider ranks candidates
    // by relevance, so the first one is what an interactive scrape
    // would default to.
    // NOTE on shared provider state: ScreenScraperProvider keeps a
    // single-entry detail cache (m_lastDetail). fetchDetail reads
    // it synchronously, so the cache survives interleaving — the
    // lookup-callback writes m_lastDetail and immediately calls
    // fetchDetail which reads it, all before the Qt event loop
    // can dispatch another item's lookup-completion. Safe so long
    // as the provider's lookup callback is the one that calls
    // fetchDetail (which it is, here).
    self->m_provider->fetchDetail(
        candidates.first(), [self, state](ErrorUtils::Result<Scraper::ScrapedItem> detailResult) {
          if (self.isNull()) return;
          if (self->m_cancelled) {
            self->itemFinished();
            return;
          }
          if (state->cancelToken->load(std::memory_order_acquire)) {
            ++self->m_summary.skipped;
            self->itemFinished();
            return;
          }
          if (detailResult.isError()) {
            self->recordError(QFileInfo(state->path).fileName(), detailResult.error());
            return;
          }
          const auto scraped = detailResult.value();
          if (self->m_quotaStopped) {
            // Kartend-fv3yr: quota was exhausted by a sibling between this
            // item's detail fetch and now. Keep the metadata we already have
            // (free) but skip the media downloads, which would burn more
            // requests against the exhausted quota.
            self->applyAndFinish(state, scraped, {});
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
          if (!self->m_fetchPrimaryCover) {
            self->applyAndFinish(state, scraped, {});
            return;
          }
          const QList<Scraper::MediaAsset> wantedAssets = self->resolveWantedMediaAssets(scraped);
          if (wantedAssets.isEmpty()) {
            self->applyAndFinish(state, scraped, {});
            return;
          }
          // Shared aggregator: every parallel fetch decrements
          // `pending` on completion; the last one out commits.
          // shared_ptr because the lambdas outlive any one fetch.
          struct Aggregator {
            int pending = 0;
            QList<Scraper::PendingMediaWrite> writes;
          };
          auto agg = std::make_shared<Aggregator>();
          agg->pending = wantedAssets.size();
          for (const auto &asset : wantedAssets) {
            self->m_provider->fetchMediaBytes(
                asset.url, [self, state, scraped, asset, agg](ErrorUtils::Result<QByteArray> r) {
                  if (self.isNull()) return;
                  if (self->m_cancelled) {
                    if (--agg->pending == 0) {
                      self->itemFinished();
                    }
                    return;
                  }
                  if (state->cancelToken->load(std::memory_order_acquire)) {
                    // Skipped mid media-fetch: drop the in-flight assets and
                    // count the item as skipped once the last fetch returns
                    // (it never reaches applyAndFinish, so nothing is written).
                    if (--agg->pending == 0) {
                      ++self->m_summary.skipped;
                      self->itemFinished();
                    }
                    return;
                  }
                  if (r.isOk() && !r.value().isEmpty()) {
                    // Track byte count regardless of write success
                    // — the user's bandwidth was already spent.
                    self->m_totalBytesDownloaded += r.value().size();
                    Scraper::PendingMediaWrite w;
                    w.asset = asset;
                    w.bytes = r.value();
                    agg->writes.append(w);
                  } else if (r.isError() &&
                             (r.error().httpStatus == 430 || r.error().httpStatus == 431)) {
                    // A quota-exhausted media fetch is still
                    // non-fatal for THIS item (it keeps its
                    // metadata + whatever assets already landed),
                    // but it must stop new items from dispatching
                    // — same stop signal as the lookup/detail path.
                    self->m_summary.quotaExhausted = true;
                    self->m_quotaStopped = true;
                  }
                  // Asset fetch failures are non-fatal — partial
                  // success is better than failing the whole item
                  // because one 404'd asset.
                  if (--agg->pending == 0) {
                    self->applyAndFinish(state, scraped, agg->writes);
                  }
                });
          }
        });
  });
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
  connect(watcher, &QFutureWatcher<Scraper::MediaWriteResult>::finished, this,
          [self, watcher, state, effective, baseName]() {
            watcher->deleteLater();
            if (self.isNull()) return;
            if (self->m_cancelled) {
              self->itemFinished();
              return;
            }
            const Scraper::MediaWriteResult writeRes = watcher->result();

            // Probe for an existing primary-cover file on disk and append
            // it to the thumbnail-strip paths when no fresh write
            // happened — FillMissing / UpdateChanged commonly skip
            // writing if the file already exists, but the user still
            // wants a visual ping for each item. Probes the canonical
            // subdirs the persistence layer can use (top-level mirror,
            // /front/, /covers/, /box-2D/, /screenshot/).
            QStringList thumbPaths = writeRes.writtenPaths;
            if (thumbPaths.isEmpty() && !self->m_artworkDir.isEmpty()) {
              static const QStringList kProbeDirs = {
                  QString(), QStringLiteral("front"), QStringLiteral("covers"),
                  QStringLiteral("box-2D"), QStringLiteral("screenshot")};
              static const QStringList kProbeExts = {QStringLiteral("png"), QStringLiteral("jpg"),
                                                     QStringLiteral("jpeg"),
                                                     QStringLiteral("webp")};
              for (const QString &dir : kProbeDirs) {
                QString prefix = self->m_artworkDir;
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

            // ── DB-write phase: dispatch ──────────────────────
            // Stash everything onWriteCompleted needs (the original
            // ItemState for cancellation/error context, the effective
            // ScrapedItem for the itemCompleted signal payload, the
            // thumbPaths the dialog renders) keyed on a fresh
            // requestId. The worker emits writeCompleted(requestId,
            // ok) once the SQLite save returns.
            //
            // Null-DB carve-out: the test fixture and a few
            // metadata-only callers pass nullptr. With no worker thread
            // we keep the legacy synchronous "treat as success" path so
            // existing tests keep passing.
            if (!self->dbMgr() || !self->m_writeWorker) {
              ++self->m_summary.scraped;
              self->m_summary.mediaWritten += writeRes.mediaWritten;
              emit self->itemCompleted(self->m_summary.scraped + self->m_summary.skipped +
                                           self->m_summary.errors,
                                       self->totalItemCount(), effective, thumbPaths);
              self->itemFinished();
              return;
            }

            const quint64 requestId = ++self->m_nextWriteId;
            PendingWrite pending;
            pending.state = state;
            pending.scraped = effective;
            pending.writtenPaths = thumbPaths;
            pending.mediaWritten = writeRes.mediaWritten;
            pending.baseName = baseName;
            if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
              pending.dispatchedAtMs = QDateTime::currentMSecsSinceEpoch();
            }
            // QHash::insert takes const T&, so std::move was a no-op here.
            self->m_pendingWrites.insert(requestId, pending);

            // Queued cross-thread invocation. The worker handles the
            // load → merge → save against its own connection and
            // queues the writeCompleted signal back to the runner's
            // (main) thread.
            QMetaObject::invokeMethod(
                self->m_writeWorker, "performWrite", Qt::QueuedConnection,
                Q_ARG(quint64, requestId), Q_ARG(QString, self->m_collectionUuid),
                Q_ARG(QString, state->path), Q_ARG(Scraper::ScrapedItem, effective),
                Q_ARG(Scraper::NonStandardArtworkList, writeRes.nonStandardArtwork));
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
        // helper returns Skipped in that case. The outcome is intentionally
        // discarded here — a Failed write is already logged by the helper;
        // surfacing it in the batch summary is tracked as Kartend audit E-01.
        (void)Scraper::writeMetadataSidecar(artworkDir, baseName, effective, rescrapeMode);
        return Scraper::writeMediaFiles(artworkDir, baseName, writes, rescrapeMode, mediaCancel);
      });
  m_inFlightMediaWrites.append(writeFuture);
  watcher->setFuture(writeFuture);
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
    recordError(QStringLiteral("%1: metadata save failed").arg(pending.baseName));
    return;
  }

  // Worker bypassed DatabaseManager's main-thread cache. Invalidate
  // the per-item slot here so the next sidebar refresh sees the
  // freshly-saved metadata instead of the pre-scrape cached row.
  if (auto *db = dbMgr()) {
    db->invalidateMetadataCacheItem(m_collectionUuid, pending.state->path);
  }

  ++m_summary.scraped;
  m_summary.mediaWritten += pending.mediaWritten;

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
  emit itemCompleted(m_summary.scraped + m_summary.skipped + m_summary.errors, totalItemCount(),
                     pending.scraped, pending.writtenPaths);
  itemFinished();
}

void BatchScrapeRunner::recordError(const QString &reason) {
  ++m_summary.errors;
  if (m_summary.firstFailures.size() < kMaxReportedFailures) {
    m_summary.firstFailures.append(reason);
  }
  itemFinished();
}

void BatchScrapeRunner::recordError(const QString &itemName, const ErrorUtils::ErrorContext &err) {
  // HTTP 430 (SS daily request quota) / 431 (SS daily failed-lookup
  // quota) mean every remaining item would just burn against an
  // exhausted quota. Flag it so pump() stops dispatching new items;
  // in-flight items still finish (they're not gated on m_quotaStopped).
  if (err.httpStatus == 430 || err.httpStatus == 431) {
    m_summary.quotaExhausted = true;
    m_quotaStopped = true;
  }
  recordError(QStringLiteral("%1: %2").arg(itemName, err.message));
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
