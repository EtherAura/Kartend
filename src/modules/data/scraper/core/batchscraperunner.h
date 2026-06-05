#ifndef BATCHSCRAPERUNNER_H
#define BATCHSCRAPERUNNER_H

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "metadatalookupprovider.h"
#include "scrapepersistence.h"

class IDatabaseManager;
class QThread;
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
    int skipped = 0; ///< Items with no provider candidates.
    int errors = 0;  ///< Items where lookup / fetchDetail / apply failed.
    /// Aggregate count of media files written by applyScrapedItem
    /// across all items — covers / screenshots / videos / etc.
    /// Ticks per-write, not per-item, so the Live view can show
    /// "12 items, 47 media" rather than just the item count.
    int mediaWritten = 0;
    /// First N (≤5) per-item failure messages — for the summary box.
    /// Bounded so a 10k-item rescrape with a broken provider doesn't
    /// produce an unreadable wall of text.
    QStringList firstFailures;
    /// Set when an item failed with an HTTP 430/431 — ScreenScraper's
    /// daily request / failed-lookup quota is exhausted. The runner
    /// stops dispatching new items (in-flight ones still finish); the
    /// caller uses this to leave a resume point instead of advancing
    /// to the next collection and burning the rest of the queue
    /// against an exhausted quota.
    bool quotaExhausted = false;
  };

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
  /// Final step shared between the cover-fetched path and the
  /// cover-skipped fallback. Persists via `applyScrapedItem`, marks
  /// the slot as free, and pumps the queue.
  void applyAndFinish(const std::shared_ptr<ItemState> &state, const Scraper::ScrapedItem &scraped,
                      const QList<Scraper::PendingMediaWrite> &writes);
  /// Record a per-item failure, mark the slot free, and pump.
  void recordError(const QString &reason);
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
  /// Called from cancel() and the destructor; idempotent.
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

  /// Per-item state captured between dispatch to the write worker and
  /// the queued writeCompleted reply. With itemConcurrency > 1 several
  /// of these can be alive at once; the requestId keys them apart.
  struct PendingWrite {
    std::shared_ptr<ItemState> state;
    Scraper::ScrapedItem scraped;
    QStringList writtenPaths;
    int mediaWritten = 0;
    QString baseName;
    /// Wall-clock when performWrite was queued onto the worker thread.
    /// Used by the perf-trace path (KARTEND_PERF_TRACE=1) to compute
    /// the dispatch→writeCompleted latency the user's main thread no
    /// longer pays. Drives the data for Kartend-5vwt item 3.
    /// `qint64{0}` = perf trace not active for this request.
    qint64 dispatchedAtMs = 0;
  };
  QHash<quint64, PendingWrite> m_pendingWrites;
  quint64 m_nextWriteId = 0;

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
