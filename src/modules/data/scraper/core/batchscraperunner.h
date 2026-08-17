#ifndef BATCHSCRAPERUNNER_H
#define BATCHSCRAPERUNNER_H

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <QDateTime>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "itemmetadata.h"
#include "metadatalookupprovider.h"
#include "scrapepersistence.h"

class IDatabaseManager;
class QThread;
class QTimer;
#include "applicationcontext_fwd.h"

namespace Scraper {

class ScrapeWriteWorker;

/// Drives an async batch scrape over the items in one collection.
/// Per-item flow:
///   1. provider.lookup(filename, …)            — find candidates
///   2. provider.fetchDetail(candidates[0], …)  — auto-pick the first
///   3. Scraper::applyScrapedItem(…)            — write metadata
///
/// **Parallel items.** `itemConcurrency` controls how many items run
/// through this chain in parallel. Default 1 preserves the legacy
/// strict-serial behaviour; setting it to 4-8 lets the runner
/// pipeline lookup/detail/apply across items the way Skyscraper's
/// `--threads N` worker pool does. Per-provider HTTP rate limits
/// still apply (the underlying Scraper::HttpClient throttles per
/// host), so high item concurrency just means there's always
/// something queued behind the per-host gate.
///
/// Cancellation: `cancel()` flips a flag; in-flight callbacks observe
/// it on entry, skip their work, and the last one out emits the
/// finished signal. Cancellation never leaves a partial item
/// half-written because `applyScrapedItem` is atomic per item
/// (metadata save + media writes happen together for one item before
/// that item's chain ends).
///
/// When `fetchPrimaryCover` is true (the default), the runner also
/// downloads the **first `"front"`-typed MediaAsset** on each
/// ScrapedItem and persists it as the primary cover. Cover fetch
/// failures are non-fatal — the metadata still saves; only the
/// cover for that item is skipped. Set the flag to false for runs
/// where the user only wants metadata (large libraries on metered
/// connections, etc.).
///
/// `RescrapeMode::Skip` and `RescrapeMode::FillMissing` both
/// pre-filter the queue at `start()` so a quota-limited provider is
/// only asked about items where the run could actually contribute
/// something:
///   * Skip drops any item that already has a metadata marker —
///     either `item_metadata.source` non-empty OR a metadata
///     sidecar JSON (`{artworkDir}/metadata/{basename}.json`) on
///     disk. The sidecar branch catches kart imports, partial
///     scrapes that lost their DB row, and migrations from older
///     builds.
///   * FillMissing drops items where every ticked field already
///     has data — `_metadata` covered by the same DB/sidecar
///     check above, plus each enabled media type covered by a
///     file in `{artworkDir}/{type}/{basename}.<ext>` (or the
///     flat `{artworkDir}/{basename}.<ext>` mirror for `front`).
///     An item missing even one ticked field still flows through
///     so the provider can fill the gaps.
/// The optional `skipRecentScrapeDays` window narrows both modes
/// to "covered within the last N days" so users can refresh stale
/// entries; 0 keeps the legacy "skip / fill-skip if covered, ever"
/// behaviour. Overwrite and UpdateChanged intentionally visit every
/// item — Overwrite always re-fetches, and UpdateChanged needs the
/// bytes back to compare.
class BatchScrapeRunner : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(BatchScrapeRunner)

public:
  /// Aggregate counts emitted via `finished` so callers can summarise
  /// the run without tracking per-event state themselves.
  struct Summary {
    int scraped = 0; ///< Items where applyScrapedItem succeeded.
    /// Items skipped without a verdict — user-skipped, pre-skipped by the
    /// rescrape-mode filter, or an in-flight item whose detail fetch was
    /// cut short by a quota stop. Items left undispatched after a quota
    /// stop are NOT counted here (or in any other bucket): they stay in
    /// the service's queue as the resume point, so on a quota stop
    /// processedItems() legitimately ends below the run's total.
    int skipped = 0;
    int errors = 0; ///< Items where lookup / fetchDetail / apply failed.
    /// Items the provider has no entry for — an HTTP 404 or an empty
    /// "no match" lookup result. A routine, expected outcome counted apart
    /// from `errors` so an unmatched-but-otherwise-fine library reads as
    /// 0 errors (Kartend-e8aag).
    int notFound = 0;
    /// Aggregate count of media files written by applyScrapedItem
    /// across all items — covers / screenshots / videos / etc.
    /// Ticks per-write, not per-item, so the Live view can show
    /// "12 items, 47 media" rather than just the item count.
    int mediaWritten = 0;
    /// Media asset fetches that failed or returned empty across the run
    /// (404, refused redirect, oversize abort, Content-Type rejection).
    /// Non-fatal per item — the item still counts as scraped with whatever
    /// assets landed — but surfaced so a "scraped N, 0 media" run carries a
    /// diagnosis instead of silence (Kartend-jjyst.4).
    int mediaFetchFailures = 0;
    /// Genuine media write failures from writeMediaFiles (disk full, mkpath
    /// failure, unsafe remote-derived path, empty payload) — distinct from
    /// benign rescrape-policy skips, which are not failures. The per-asset
    /// reasons are folded into firstFailures (Kartend-jjyst.4).
    int mediaWriteFailures = 0;
    /// First N (≤5) per-item failure messages — for the summary box.
    /// Bounded so a 10k-item rescrape with a broken provider doesn't
    /// produce an unreadable wall of text.
    QStringList firstFailures;
    /// Full source paths of items that landed in `errors` (NOT notFound or
    /// skipped) — the set the "re-scrape failed" affordance re-queues
    /// (Kartend-jjjo5). Bounded by kMaxReportedFailures like firstFailures.
    QStringList failedPaths;
    /// Set when an item failed with an HTTP 430/431 — ScreenScraper's
    /// daily request / failed-lookup quota is exhausted. The runner
    /// stops dispatching new items (in-flight ones still finish); the
    /// caller uses this to leave a resume point instead of advancing
    /// to the next collection and burning the rest of the queue
    /// against an exhausted quota.
    bool quotaExhausted = false;
    /// Items whose DB metadata saved but whose human-readable JSON
    /// sidecar write failed (disk full / permissions). The sidecar is a
    /// derived copy — authoritative metadata still landed — so these are
    /// counted separately from `errors` (Kartend audit hhr5x).
    int sidecarFailures = 0;
    /// Items that reached a terminal verdict this run (every per-item
    /// outcome bucket). Used for progress math so a notFound item still
    /// advances the "X of N" counter.
    [[nodiscard]] int processedItems() const { return scraped + skipped + errors + notFound; }
  };

  /// Outcome of the shared already-scraped pre-filter: the paths that still
  /// need scraping plus how many were dropped as already covered.
  struct PreFilterResult {
    QStringList kept;
    int dropped = 0;
  };

  /// The Skip / FillMissing already-scraped pre-filter, callable without a
  /// runner instance so the interactive path can drop covered items from its
  /// queue up front with the exact same criterion as an Auto run
  /// (Kartend-resxp). Batch-loads DB metadata for @p paths (when @p db and
  /// @p collectionUuid allow a DB check), indexes media-on-disk under
  /// @p artworkDir, and applies shouldSkipScrapedItem per path. The caller
  /// owns the accounting for `dropped`. @p rescrapeMode is expected to be
  /// Skip or FillMissing — Overwrite / UpdateChanged intentionally visit
  /// every item, so callers gate on the mode before paying for the indexes
  /// (see start()).
  [[nodiscard]] static PreFilterResult
  preFilterAlreadyScraped(IDatabaseManager *db, const QString &collectionUuid,
                          const QString &artworkDir, const QSet<QString> &mediaTypeFilter,
                          bool fetchPrimaryCover, Scraper::RescrapeMode rescrapeMode,
                          bool writeMetadata, int skipRecentDays, const QStringList &paths);

  /// `db` may be nullptr — the runner still drives the scrape but
  /// applyScrapedItem skips DB writes (file writes still happen). The
  /// tests use that mode to exercise the state machine without a real
  /// QSqlDatabase. `provider` is held by shared_ptr so the runner can
  /// outlive the registry that built it (the user can dismiss the
  /// settings dialog while the batch keeps running). `collectionUuid`
  /// keys the metadata rows; `artworkDir` is the per-collection
  /// directory artwork bytes are written to (empty when only metadata
  /// is being persisted). `fetchPrimaryCover` toggles the per-item
  /// `"front"`-asset download (default on — see the class doc).
  /// `itemConcurrency` defaults to 1 (legacy strict-serial behaviour);
  /// pass 4-8 for Skyscraper-style item parallelism.
  /// `skipRecentDays` applies under both `RescrapeMode::Skip` and
  /// `RescrapeMode::FillMissing` and gates the pre-filter: 0 =
  /// always skip covered items (legacy), N > 0 = only skip items
  /// covered within the last N days (anything older flows back
  /// into the queue for refresh).
  // Kartend-m02z: takes the full ApplicationContext instead of a snapshot
  // of the DB pointer so the runner reads through ctx in dbMgr() and the
  // pointer stays consistent with ApplicationContext::managers across the
  // runner's lifetime.
  BatchScrapeRunner(const ApplicationContext *ctx, std::shared_ptr<MetadataLookupProvider> provider,
                    QString collectionUuid, QStringList paths, QString artworkDir,
                    bool fetchPrimaryCover = true,
                    Scraper::RescrapeMode rescrapeMode = Scraper::RescrapeMode::Overwrite,
                    int itemConcurrency = 1, int skipRecentDays = 0, QObject *parent = nullptr);
  ~BatchScrapeRunner() override;

  /// Restrict the per-item media-fetch pass to these asset types
  /// (case-insensitive matched against `MediaAsset::type`). When the
  /// filter is empty (default) the runner falls back to the legacy
  /// "front cover only" behaviour gated by `fetchPrimaryCover`. When
  /// non-empty AND `fetchPrimaryCover` is true the runner fetches
  /// every matching asset for each item in parallel and persists
  /// them all in one applyScrapedItem call.
  void setMediaTypeFilter(const QSet<QString> &types) { m_mediaTypeFilter = types; }

  /// Gate the metadata-text save. When false, every textual field on
  /// the ScrapedItem (title, description, publisher, etc.) is wiped
  /// before applyScrapedItem fires — the persistence layer's
  /// "preserve existing on empty" path then leaves whatever was
  /// previously in the DB untouched. Useful for media-only re-scrapes
  /// (user only wants new artwork; their hand-edited metadata stays).
  /// Default true.
  void setWriteMetadata(bool write) { m_writeMetadata = write; }

  /// Begin processing. The first lookup fires synchronously; the rest
  /// chain through the provider's callbacks. Safe to call from the
  /// main thread; emits its progress/finished signals on the main
  /// thread.
  void start();

  /// Live (in-progress) summary. The service queries this from
  /// `progress`/`itemCompleted` callbacks so mid-collection counts
  /// roll up into the dialog's Live view without waiting for the
  /// `finished` signal at end-of-collection.
  [[nodiscard]] Summary currentSummary() const { return m_summary; }

  /// Paths that have not yet reached a per-item terminal outcome this run:
  /// undispatched items plus those still mid-chain. Successes, user-skips,
  /// errors and not-founds are trimmed as they settle — by identity, not by
  /// queue position, so interleaved errors (or itemConcurrency > 1, where
  /// completion order != queue order) can't trim the wrong item. Work stopped
  /// by quota exhaustion (the erroring item and everything settling after the
  /// stop flag flipped) intentionally stays listed, so a persisted resume
  /// point retries exactly the items the quota killed. ScraperService
  /// snapshots this as the collection's persisted remaining list.
  [[nodiscard]] QStringList remainingPaths() const { return m_remainingPaths; }

  /// Total media bytes fetched so far in this collection. Used by
  /// the Live view to compute and display a rolling download rate
  /// (MiB/s). Only counts media payload — lookup / detail JSON
  /// responses don't contribute since they're tiny relative to
  /// covers / videos.
  [[nodiscard]] qint64 totalBytesDownloaded() const { return m_totalBytesDownloaded; }

  /// Stop after the in-flight items complete. Idempotent — calling
  /// twice is a no-op. The `finished` signal still fires (with
  /// whatever counts were accumulated) so callers don't need a
  /// separate "cancelled" handler.
  void cancel();

  /// Abort just the item whose stage the modal dialog is currently
  /// showing — the most recently started in-flight item, which is
  /// unambiguous at the default itemConcurrency==1. Flips only that
  /// item's cancel token (leaving m_cancelled false), so its in-flight
  /// ROM hash / archive extraction stops, it's counted as skipped, and
  /// the batch plus any other in-flight items keep running. No-op when
  /// nothing is in flight.
  void skipCurrentItem();

signals:
  /// Emitted as each item begins. `done` is the number of items
  /// completed so far (success + skip + error); `total` is the full
  /// count; `currentName` is the basename of the item that just
  /// started. With itemConcurrency > 1 multiple items are in flight
  /// at once, so consecutive `progress` emissions may carry the same
  /// `done` value with different `currentName` strings.
  void progress(int done, int total, const QString &currentName);
  /// Emitted by the provider through its stageReporter callback when a
  /// long-running lookup stage starts ("Hashing ROM…", "Extracting
  /// archive…") and again with an empty string when the stage ends.
  /// BatchScrapeProgressView routes this into its current-item label
  /// so a multi-minute PS2 .zip extraction doesn't look like a hang
  /// (Kartend-ou0a).
  void itemStageChanged(const QString &stage);
  /// Emitted after each item's applyScrapedItem completes (success
  /// path only). Lets the Scraper UI surface the just-scraped
  /// metadata + the on-disk paths of the media files the persistence
  /// layer wrote, without re-scanning the artwork directory. Skipped
  /// items and lookup failures do NOT fire this signal — the
  /// progress(done, total, name) signal still ticks for those.
  void itemCompleted(int done, int total, const Scraper::ScrapedItem &scraped,
                     const QStringList &writtenPaths);
  /// Fired exactly once after the last in-flight item completes or
  /// after cancel settles. Cancellation, normal completion, and
  /// zero-work runs all reach here so the caller can dismiss the
  /// progress dialog from one signal handler.
  void finished(const Summary &summary);
  /// Emitted after each item is accounted for, carrying the provider's
  /// latest per-account request quota. Only fired when the provider
  /// reports a valid quota (ScreenScraper after its first response);
  /// other providers never trigger it. Lets the dialog show a live
  /// "N / M requests today" readout.
  void quotaUpdated(const Scraper::QuotaStatus &quota);

public:
  /// Watchdog budget in ms. Default 10min — long enough that a legitimate hash
  /// of a multi-GB image over a slow network mount won't false-trip, short
  /// enough that a genuine wedge bounds the stall to minutes instead of
  /// freezing the batch. Overridable via KARTEND_SCRAPE_STEP_TIMEOUT_MS (tests
  /// set it tiny; pathological large-file setups can raise it). Public so
  /// ScraperService's entity media-write watchdog shares the same budget and
  /// env knob instead of growing a second, subtly-different timeout.
  [[nodiscard]] static int stepWatchdogMs();

  /// Consecutive HTTP 429 responses that escalate to a graceful queue stop.
  /// Public so ScraperService's entity and interactive paths share the same
  /// threshold as this runner's noteRateLimited429() instead of growing a
  /// second, subtly-different streak length (Kartend-jjyst.15).
  static constexpr int kConsecutive429StopThreshold = 3;

private:
  /// Per-item state captured by the async callback chain. Each item's
  /// lookup → detail → media → apply path operates on its own
  /// shared_ptr<ItemState> so multiple items can be in flight without
  /// stepping on each other.
  struct ItemState {
    QString path;
    int queueIndex = -1; ///< Position in the original m_paths queue.
    /// Per-item cooperative-cancellation flag handed to hash-based
    /// providers via LookupContext. skipCurrentItem() flips only this
    /// item's flag (m_cancelled stays false) so the displayed item's
    /// in-flight hash/extraction aborts while the rest of the batch
    /// keeps running; cancel() flips every in-flight item's flag.
    /// shared_ptr<atomic> so the worker keeps reading it even if the
    /// runner is destroyed mid-hash.
    std::shared_ptr<std::atomic<bool>> cancelToken = std::make_shared<std::atomic<bool>>(false);
  };

  /// Pre-filter the queue for the Skip and FillMissing rescrape
  /// modes. Under Skip, drops items with any metadata marker
  /// (`item_metadata.source` non-empty OR sidecar JSON on disk).
  /// Under FillMissing, drops items where every ticked field is
  /// already covered on disk — _metadata via the same metadata
  /// check, plus each media type from `m_mediaTypeFilter` (or the
  /// legacy front-cover-only fallback) via a basename-indexed scan
  /// of `{artworkDir}/{type}/`. `m_skipRecentDays > 0` gates the
  /// drop on the timestamp: only items refreshed within the window
  /// stay skipped, anything older flows back for refresh. Runs at
  /// start() before any provider hit so the user's tally tracks
  /// real work. Overwrite and UpdateChanged never call this — they
  /// visit every item and let the per-asset persistence gate
  /// decide.
  void filterAlreadyScraped();

  /// Top of the worker loop. Fills empty in-flight slots from the
  /// queue until either the queue is empty or the in-flight count
  /// equals the concurrency cap. Emits `finished` exactly once when
  /// the queue is drained AND no items remain in flight.
  void pump();
  /// Start the lookup/detail/apply chain for a single item. Each
  /// callback in the chain owns a copy of `state` so per-item data
  /// survives interleaving with other items.
  void startItem(const std::shared_ptr<ItemState> &state);
  /// Lookup-callback body for startItem: cancel/skip/error/quota guards,
  /// then auto-picks the first candidate and dispatches fetchDetail.
  void onLookupComplete(const std::shared_ptr<ItemState> &state,
                        const ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> &result);
  /// fetchDetail-callback body: cancel/skip/error/quota guards, then the
  /// media-fetch decision (legacy front-cover vs filtered set vs none).
  void onDetailComplete(const std::shared_ptr<ItemState> &state,
                        const ErrorUtils::Result<Scraper::ScrapedItem> &detailResult);
  /// Resolve which media assets to fetch for `scraped`, extracted from
  /// `startItem`'s fetchDetail callback. With a non-empty
  /// `m_mediaTypeFilter` returns every asset whose type matches;
  /// otherwise the legacy "first valid front cover only" fallback. The
  /// caller gates this on `m_fetchPrimaryCover` and dispatches the
  /// returned assets in parallel.
  [[nodiscard]] QList<Scraper::MediaAsset>
  resolveWantedMediaAssets(const Scraper::ScrapedItem &scraped) const;
  /// Per-item media aggregator: each parallel fetch decrements `pending`;
  /// the last to return commits the item via applyAndFinish exactly once.
  struct MediaAggregator {
    int pending = 0;
    QList<Scraper::PendingMediaWrite> writes;
    /// Asset types (lowercase) whose byte-fetch failed or returned empty.
    /// Stamped onto ScrapedItem::mediaFetchFailedThisRun at commit so the
    /// mediaAbsent merge doesn't prune a returned-but-undownloadable type's
    /// absent marker as if it were satisfied (Kartend-jjyst.1).
    QStringList failedTypes;
  };
  /// Dispatch every wanted asset in parallel onto a shared MediaAggregator.
  void fetchMediaAndFinish(const std::shared_ptr<ItemState> &state,
                           const Scraper::ScrapedItem &scraped,
                           const QList<Scraper::MediaAsset> &wantedAssets);
  /// One fetchMediaBytes callback: records bytes/quota, decrements the
  /// aggregator, and commits when the last parallel fetch returns.
  void onMediaBytesComplete(const std::shared_ptr<ItemState> &state,
                            const Scraper::ScrapedItem &scraped, const Scraper::MediaAsset &asset,
                            const std::shared_ptr<MediaAggregator> &agg,
                            const ErrorUtils::Result<QByteArray> &r);
  /// Cancel guard for the lookup/detail callbacks: if cancelled, finish the
  /// item and return true so the caller bails out of the chain.
  [[nodiscard]] bool cancelledFinish();
  /// Skip-token guard for the lookup/detail callbacks: if the user skipped
  /// this item, count + finish it and return true.
  [[nodiscard]] bool skippedByToken(const std::shared_ptr<ItemState> &state);
  /// The "++skipped; itemFinished()" pair shared by the no-candidate,
  /// quota-stopped, and skip-token paths.
  void skipAndFinish();
  /// Final step shared between the cover-fetched path and the
  /// cover-skipped fallback. Persists via `applyScrapedItem`, marks
  /// the slot as free, and pumps the queue.
  void applyAndFinish(const std::shared_ptr<ItemState> &state, const Scraper::ScrapedItem &scraped,
                      const QList<Scraper::PendingMediaWrite> &writes);
  /// Main-thread continuation of applyAndFinish (Kartend audit 2w4wz): runs
  /// when the QtConcurrent media write finishes — resolves the thumbnail
  /// paths and dispatches the DB save (or takes the null-DB synchronous path).
  void onMediaWriteFinished(const std::shared_ptr<ItemState> &state,
                            const Scraper::ScrapedItem &effective, const QString &baseName,
                            const Scraper::MediaWriteResult &writeRes);
  /// Thumbnail paths for the dialog's per-item ping: the freshly-written
  /// paths, or — when nothing was written this run — a probe of the canonical
  /// cover subdirs for an existing file (Kartend audit 2w4wz).
  [[nodiscard]] QStringList resolveThumbnailPaths(const QString &baseName,
                                                  const QStringList &writtenPaths) const;
  /// Record a per-item failure, mark the slot free, and pump. When
  /// `failedPath` is non-empty it is appended to `Summary::failedPaths` so the
  /// "re-scrape failed" affordance can re-queue exactly that item (Kartend-jjjo5).
  void recordError(const QString &reason, const QString &failedPath = QString());
  /// Per-item failure overload that also inspects the error's HTTP
  /// status: an HTTP 430 (SS daily request quota) or 431 (SS daily
  /// failed-lookup quota) flips `m_summary.quotaExhausted` and
  /// `m_quotaStopped` so `pump()` stops dispatching new items. The
  /// item's message is composed as "<itemName>: <message>" and
  /// forwarded to the contextless `recordError` for the count + the
  /// first-failures list. In-flight callbacks are deliberately NOT
  /// gated on `m_quotaStopped` — only new dispatch stops; whatever is
  /// already mid-chain runs to completion.
  void recordError(const QString &itemName, const ErrorUtils::ErrorContext &err);

  /// Per-item stall guard (Kartend audit xnm8a). The per-item chain only
  /// bounds its HTTP legs (HttpClient's 30s transfer timeout). The local
  /// I/O stages — the provider's ROM hash-read inside lookup(), the
  /// QtConcurrent media/sidecar write, and the ScrapeWriteWorker DB save —
  /// have no timeout of their own, so a slow or wedged storage mount blocks
  /// read()/write()/fsync() with no interrupt: the item never completes, its
  /// concurrency slot never frees, and the whole batch hangs forever at
  /// <100%.
  ///
  /// Handle returned by armStepWatchdog: the shared "done" flag the step's
  /// completion races against, plus the armed timer so a normal completion
  /// can retire it immediately. Previously completion only set *done and the
  /// timer ran out its full 10-minute budget before self-deleting — at two
  /// timers per item a long batch retained thousands of live QTimers (each
  /// capturing its item's shared ItemState) long after the items settled.
  struct StepWatchdog {
    std::shared_ptr<bool> done;
    /// Raw pointer, not QPointer: finish() is only reachable while the
    /// runner is alive (call sites guard on the QPointer<runner> first) and
    /// before the timeout fired (fired() check), and only the timeout path
    /// ever deletes the timer early — so the pointer cannot dangle there.
    QTimer *timer = nullptr;
    /// True when the watchdog already fired (or the handle is empty) — the
    /// step's completion callback must bail out instead of double-settling.
    [[nodiscard]] bool fired() const { return !done || *done; }
    /// Mark the step complete and stop + deleteLater the timer. Timeout and
    /// completion both run on the runner's thread, so there is no race; the
    /// *done flag makes the two paths mutually exclusive (no double delete).
    void finish();
  };

  /// armStepWatchdog arms a single-shot timer racing one such step's
  /// completion and returns a StepWatchdog handle. The step's own completion
  /// callback must, before doing anything else, `if (wd.fired()) return;
  /// wd.finish();` so a watchdog that already fired is a no-op and a normal
  /// completion retires the timer instead of leaving it to run out its
  /// budget. If the watchdog fires first it sets *done, runs the optional
  /// @p onTimeout cleanup (e.g. dropping the pending-write row so a late
  /// worker reply lands in the unknown-id path), then calls onStepTimedOut,
  /// which errors that one item and frees the slot so the batch advances —
  /// exactly like the HTTP timeout. The wedged future/callback is left to
  /// drain on its own (value-captures only), the same contract as the
  /// destructor's abandon-after-1s drain.
  [[nodiscard]] StepWatchdog armStepWatchdog(const std::shared_ptr<ItemState> &state,
                                             const QString &stageLabel,
                                             std::function<void()> onTimeout = {});
  /// Watchdog-fire continuation: count the stalled item as an error (or just
  /// drain it if the run was cancelled) and pump the queue.
  void onStepTimedOut(const std::shared_ptr<ItemState> &state, const QString &stageLabel);

  /// Mark one item complete (success / skip / error). When the queue
  /// is drained and the in-flight count returns to 0, emits
  /// `finished` exactly once.
  void itemFinished();

  /// Total items this run accounts for: the live queue plus any items
  /// `filterAlreadyScraped` removed up front (Skip rescrape mode).
  /// Those pre-skipped items are folded into m_summary.skipped, so
  /// reporting them in the `total` keeps the progress `done`/`total`
  /// and the final scraped+skipped+errors tally reconciled with the
  /// item count the caller started from.
  [[nodiscard]] int totalItemCount() const {
    return static_cast<int>(m_paths.size()) + m_preSkippedCount;
  }

  /// Lazy-init the worker thread + ScrapeWriteWorker. Called from start()
  /// when m_db is non-null. The null-DB carve-out skips this so the
  /// existing test fixtures (which run with nullptr m_db) keep working
  /// without a dangling worker thread.
  void ensureWriteWorkerStarted();
  /// Stop the worker thread (queued closeConnection + quit + bounded wait).
  /// Called ONLY from the destructor; cancel() deliberately leaves the
  /// worker running so queued writes for already-completed items still land
  /// (Kartend-8mx2q comment fix — the old wording invited "fixing" cancel
  /// into tearing the worker down mid-drain). Idempotent.
  void shutdownWriteWorker();
  /// Slot — receives `writeCompleted` signals from the worker thread via
  /// a queued connection. Looks up the in-flight ItemState, invalidates
  /// the metadata cache, ticks counters, emits itemCompleted, and pumps
  /// the queue.
  void onWriteCompleted(quint64 requestId, bool ok);

  // Kartend-m02z: m_db field replaced with ctx-routed access. dbMgr() reads
  // through m_ctx so future ApplicationContext::managers rebinds don't leave
  // the runner pointing at a stale pointer.
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] IDatabaseManager *dbMgr() const;

  std::shared_ptr<MetadataLookupProvider> m_provider;
  QString m_collectionUuid;
  QStringList m_paths;
  /// Resume bookkeeping behind remainingPaths(): seeded from m_paths when the
  /// run starts (post pre-filter), trimmed by identity at each terminal
  /// outcome except quota-stopped work and cancel drains.
  QStringList m_remainingPaths;
  QString m_artworkDir;
  bool m_fetchPrimaryCover = true;
  Scraper::RescrapeMode m_rescrapeMode = Scraper::RescrapeMode::Overwrite;
  int m_itemConcurrency = 1;
  /// "Skip if scraped within N days" window for Skip rescrape mode.
  /// 0 disables the time gate (legacy behaviour: skip every already-
  /// scraped item). Clamped 0..365 by the caller (SettingsManager).
  int m_skipRecentDays = 0;
  /// User-selected media types to fetch per item (e.g. "front",
  /// "screenshot", "fanart"). Lowercase. Empty = legacy front-only.
  QSet<QString> m_mediaTypeFilter;
  /// Company-scoped assets fetched THIS run, keyed
  /// `company_<scopeKey>/<type>` — the guard that makes the filter bypass in
  /// resolveWantedMediaAssets cost one fetch per company per run instead of
  /// one per game (the runner has no other shared-scope dedup; that lives in
  /// the interactive dialog's download dispatcher). Mutable: the resolve
  /// helper is const and this is memoisation, not state a caller observes.
  mutable QSet<QString> m_companyFetchedThisRun;
  bool m_writeMetadata = true;
  /// Running total of media bytes downloaded — incremented in each
  /// fetchMediaBytes callback regardless of whether the bytes get
  /// written to disk (they all counted toward the user's bandwidth).
  qint64 m_totalBytesDownloaded = 0;

  int m_queueCursor = 0; ///< Next index in m_paths to dispatch.
  int m_inFlight = 0;    ///< Items currently mid-chain.
  /// Items dropped by `filterAlreadyScraped` before the queue ran
  /// (Skip rescrape mode). Counted in m_summary.skipped; retained
  /// separately so `totalItemCount()` can report the pre-filter total.
  int m_preSkippedCount = 0;
  bool m_cancelled = false;
  /// Weak handle to the most recently started in-flight item — the one
  /// whose stage the modal dialog is showing. skipCurrentItem() targets
  /// it. weak_ptr so it silently expires once the item's chain releases
  /// its shared_ptr; lock() then yields null and the skip is a no-op.
  std::weak_ptr<ItemState> m_currentItem;
  /// Every started item, so cancel() can flip each in-flight item's
  /// per-item token (aborting concurrent hashes the way the old single
  /// batch-wide token did). Entries for finished items expire to null on
  /// lock(); the vector is bounded by the batch size and never pruned
  /// since the runner is transient (one batch per instance).
  std::vector<std::weak_ptr<ItemState>> m_inFlightItems;
  /// Set when an HTTP 430/431 quota-exhausted error arrived. Distinct
  /// from m_cancelled: this is checked ONLY in pump() to stop NEW
  /// dispatch — in-flight per-item callbacks ignore it and run to
  /// completion (the user wants whatever is already mid-flight to
  /// finish). m_cancelled, by contrast, makes in-flight callbacks
  /// abandon their item. The drain that emits finished() observes
  /// either flag.
  bool m_quotaStopped = false;
  bool m_finishedEmitted = false; ///< Guard against double-emit on cancel races.
  Summary m_summary;

  /// Circuit breaker for persistent NON-quota fatal responses (bad
  /// credentials 401/403, provider infrastructure down 423, blacklisted
  /// build 426): these fail every request identically, so after
  /// kFatalErrorBreakerThreshold consecutive errors with the SAME status the
  /// runner stops new dispatch exactly like a quota stop (shared machinery —
  /// in-flight items drain, un-dispatched work stays queued as the persisted
  /// resume point). Any success, not-found, or differently-coded error
  /// resets the streak.
  static constexpr int kFatalErrorBreakerThreshold = 5;
  int m_consecutiveFatalCount = 0;
  int m_lastFatalStatus = 0;
  void resetFatalStreak() {
    m_consecutiveFatalCount = 0;
    m_lastFatalStatus = 0;
    // A healthy response (or a differently-shaped error) also ends any
    // consecutive-429 run — the limiter demonstrably let a request through.
    m_consecutive429Count = 0;
  }

  /// Consecutive-429 escalation (Kartend-jjyst.3). A single HTTP 429 is
  /// transient throttling — the provider retry already waited out any
  /// Retry-After hint — so it no longer stops the queue the way a 430/431
  /// daily quota does (a seconds-long burst limit stranded unattended
  /// multi-collection runs). But a limiter that answers 429 to this many
  /// consecutive requests isn't letting up: noteRateLimited429() then stops
  /// new dispatch through the same machinery as a quota stop (in-flight
  /// items drain; un-dispatched work persists as the resume point). Any
  /// success, not-found, or differently-coded error resets the streak. The
  /// kConsecutive429StopThreshold constant lives in the public section so
  /// ScraperService's paths share it.
  int m_consecutive429Count = 0;
  void noteRateLimited429();

  /// Per-item state captured between dispatch to the write worker and
  /// the queued writeCompleted reply. With itemConcurrency > 1 several
  /// of these can be alive at once; the requestId keys them apart.
  struct PendingWrite {
    std::shared_ptr<ItemState> state;
    Scraper::ScrapedItem scraped;
    QStringList writtenPaths;
    int mediaWritten = 0;
    QString baseName;
    /// Carried from MediaWriteResult so onWriteCompleted can fold a
    /// failed sidecar write into the summary (Kartend audit hhr5x).
    bool sidecarFailed = false;
    /// Wall-clock when performWrite was queued onto the worker thread.
    /// Used by the perf-trace path (KARTEND_PERF_TRACE=1) to compute
    /// the dispatch→writeCompleted latency the user's main thread no
    /// longer pays. Drives the data for Kartend-5vwt item 3.
    /// `qint64{0}` = perf trace not active for this request.
    qint64 dispatchedAtMs = 0;
    /// Stall guard for the ScrapeWriteWorker save leg — the lookup and
    /// artwork-write stages already had one, but a save wedged on stalled
    /// storage used to pin the item's slot and hang the batch at <100%
    /// forever. onWriteCompleted retires it on the normal reply; on fire
    /// the row is dropped so the late reply is ignored (unknown id).
    StepWatchdog saveWatchdog;
  };
  QHash<quint64, PendingWrite> m_pendingWrites;
  quint64 m_nextWriteId = 0;

  /// Cooperative cancel for the media-write tasks this runner launches on
  /// the global QThreadPool (Kartend-vi76q). cancel() and the destructor
  /// flip it; the write lambda's preamble and writeMediaFiles' per-asset
  /// loop poll it, so a cancelled batch stops writing files promptly and
  /// the destructor's drain returns fast instead of waiting out every
  /// remaining multi-MB asset.
  std::shared_ptr<std::atomic<bool>> m_mediaWriteCancel =
      std::make_shared<std::atomic<bool>>(false);
  /// In-flight media-write futures (global-pool QtConcurrent::run tasks,
  /// value-captures only — no `this`). Pruned of finished entries at each
  /// dispatch; the destructor flips m_mediaWriteCancel and drains the
  /// remainder within a 2s budget (Kartend-8mx2q — the runner lives on the
  /// GUI thread, so an unbounded wait hung the UI on wedged storage).
  /// Tasks abandoned past the budget are safe: value-captures only, worst
  /// case the current asset finishes writing to disk post-dtor.
  QList<QFuture<Scraper::MediaWriteResult>> m_inFlightMediaWrites;

  // Worker thread + DB writer. Both are nullptr when m_db == nullptr
  // (test mode); the runner falls back to a synchronous "treat as
  // success" path that mirrors the legacy null-DB behaviour. When
  // m_db is non-null, the thread is started lazily at start() and torn
  // down in cancel() / dtor.
  QThread *m_writeThread = nullptr;
  ScrapeWriteWorker *m_writeWorker = nullptr;
  bool m_writeWorkerShutdownDone = false;
};

} // namespace Scraper

#endif // BATCHSCRAPERUNNER_H
