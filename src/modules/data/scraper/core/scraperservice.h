#ifndef SCRAPERSERVICE_H
#define SCRAPERSERVICE_H

#include <atomic>
#include <functional>
#include <memory>

#include <QFuture>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include "batchscraperunner.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "entityscrapecoordinator.h"
#include "metadatalookupprovider.h"
#include "scrapelock.h"
#include "scrapepersistence.h"
#include "scrapertypes.h"

class IDatabaseManager;
#include "applicationcontext_fwd.h"
class QTimer;
struct GeneralSettings;

namespace Scraper {

/// Long-lived scrape coordinator. Owns the active queue + BatchScrapeRunner
/// across the lifetime of the application, so the user can close the
/// Scraper dialog without stopping the run, persist the run across
/// program restarts, and resume an in-flight scrape on next launch.
///
/// Two execution modes:
///   • Auto      — every collection in the queue runs through a
///                 BatchScrapeRunner; the runner auto-picks the first
///                 candidate per item.
///   • Interactive — the service drives the per-item loop itself;
///                 fires `pickerNeeded(item, candidates)` for each
///                 item and waits for the dialog (or any UI) to call
///                 `applyPick(result)` or `skipPick()` before moving
///                 on. Closing the dialog mid-pick pauses the queue
///                 (state = Paused); the next `startOrResume()` or
///                 dialog re-entry kicks the picker back up.
///
/// Persistence: snapshots the active queue + cursor + summary-so-far
/// to `~/.config/Kartend/pending-scrape.json` after every item
/// completes. On startup, MainWindow asks `loadPendingState()`; the
/// returned PendingState surfaces a modal resume / discard prompt
/// (toggleable to silent auto-resume via GeneralSettings).
class ScraperService : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScraperService)
  /// Entity-scrape execution engine (friend + back-pointer pattern): the
  /// canonical run state stays on this class; the coordinator owns only the
  /// entity flow's code. See entityscrapecoordinator.h.
  friend class EntityScrapeCoordinator;

public:
  enum class State { Idle, RunningAuto, RunningInteractive, PausedInteractive, Finishing };
  Q_ENUM(State)

  enum class Mode { Auto, Interactive };
  Q_ENUM(Mode)

  /// Single collection's job. Snapshotted to JSON during persistence.
  struct CollectionJob {
    int collectionIndex = -1;
    QString collectionUuid;
    QString collectionName;
    QString artworkDir;
    QStringList items; // remaining items only (completed ones are removed as they finish)
    /// When `entity.type` is a non-Game type, this job is a single
    /// platform/collection/category entity scrape (not a per-file batch); the
    /// service dispatches it via one provider->fetchEntity() call rather than a
    /// BatchScrapeRunner, sharing the same queue / resume / quota / result
    /// machinery (Kartend-ckepd.2). `items` is unused for an entity job.
    Scraper::EntityScrapeTarget entity;
    [[nodiscard]] bool isEntityJob() const {
      return entity.type != Scraper::ScrapeEntityType::Game;
    }
  };

  /// Aggregate running summary; mirrors BatchScrapeRunner::Summary but
  /// accumulates across the whole queue.
  struct Summary {
    int scraped = 0;
    int skipped = 0;
    int errors = 0;
    /// Items the provider has no entry for (HTTP 404 / empty "no match"),
    /// accumulated across the queue. Mirrors BatchScrapeRunner::Summary::
    /// notFound — counted apart from `errors` (Kartend-e8aag).
    int notFound = 0;
    /// Aggregate media-files written across every collection so the
    /// dialog's Live view can render "items: X · media: Y" instead
    /// of just an item count.
    int mediaWritten = 0;
    QStringList firstFailures;
    /// Full source path of an errored item, tagged with the index of the
    /// collection that owns it — enough for the dialog to rebuild a
    /// CollectionJob per owner and re-queue just the failures (Kartend-jjjo5).
    struct FailedItem {
      int collectionIndex = -1;
      QString path;
      /// Owning collection's stable UUID. Persisted alongside the index so a
      /// resume in a later session (where indices may have shifted) can
      /// re-resolve the live index instead of re-queueing against the wrong
      /// collection. Empty on legacy snapshots — those fall back to the index.
      QString collectionUuid;
      /// Discriminator: a game item (false, the default) vs. an entity item
      /// (true). Entity failures land in the SAME failedItems list a game
      /// re-scrape consumes, but they must be re-queued AS entity jobs — the
      /// `path`/`identity` alone can't rebuild the job, which needs the entity
      /// type + collectionIndex too. Without this flag rescrapeFailedItems()
      /// re-dispatched an entity failure as a bogus game lookup of its
      /// systemeid, losing the original entity target.
      bool isEntity = false;
      /// The entity target to reconstruct the entity job on re-queue. Only
      /// meaningful when `isEntity` is true; default-constructed (type == Game)
      /// for a game item. Round-tripped through pending-scrape.json so a resumed
      /// run keeps entity failures re-queueable AS entities.
      Scraper::EntityScrapeTarget entity;
    };
    /// Errored items across the whole queue (NOT notFound / skipped). Bounded
    /// like firstFailures. Drives the "re-scrape failed" affordance.
    QList<FailedItem> failedItems;
    /// Set when a collection's runner stopped because ScreenScraper's
    /// daily quota was exhausted (HTTP 430/431). The service then
    /// stops walking the queue and leaves the persisted resume point
    /// intact so the user can continue after the quota resets.
    bool quotaExhausted = false;
    /// Sidecar (.json) writes that failed across the whole queue —
    /// mirrors BatchScrapeRunner::Summary::sidecarFailures (audit hhr5x).
    int sidecarFailures = 0;
    /// Every per-item terminal outcome — used for progress / resume math.
    [[nodiscard]] int processedItems() const { return scraped + skipped + errors + notFound; }
  };

  /// Persistence snapshot returned by loadPendingState(). Empty
  /// (`isValid() == false`) when there's no pending state. Caller
  /// reconstitutes the runtime context (provider builder, settings)
  /// and calls startOrResume to take over the queue.
  struct PendingState {
    bool hasState = false;
    qint64 startedAtUnixMs = 0;
    Mode mode = Mode::Auto;
    bool writeMetadata = true;
    QSet<QString> mediaFilter;
    Summary summarySoFar;
    QList<CollectionJob> queue;
    [[nodiscard]] bool isValid() const { return hasState && !queue.isEmpty(); }
    [[nodiscard]] int totalRemaining() const;
  };

  /// Wiring needed to launch / resume a scrape. Caller (MainWindow)
  /// fills these in once at construction. Kartend-m02z: holds the full
  /// ApplicationContext instead of a snapshot of the DB pointer so the
  /// runner and other call sites stay in sync if ApplicationContext::managers
  /// is rebound.
  struct Context {
    const ApplicationContext *ctx = nullptr;
    GeneralSettings *generalSettings = nullptr;
    QList<CollectionConfig> *collections = nullptr;
    /// Builder for a fresh provider per collection index. Same shape
    /// as the dialog's context — keeps registry construction inside
    /// MainWindow where the live-settings closures already exist.
    std::function<std::shared_ptr<MetadataLookupProvider>(int collectionIndex)> providerBuilder;
  };

  explicit ScraperService(QObject *parent = nullptr);
  ~ScraperService() override;

  void setContext(const Context &ctx);

  /// Begin a fresh scrape. Walks the supplied jobs in order. Each
  /// `job.items` is the explicit list of paths to scrape (already
  /// filtered by the dialog). The service takes ownership of the
  /// orchestration; clients observe via signals.
  void startScrape(const QList<CollectionJob> &jobs, Mode mode, const QSet<QString> &mediaFilter,
                   bool writeMetadata);

  /// Resume from a previously-persisted state. Behaviour matches
  /// startScrape but the queue and counters come from `state` rather
  /// than the caller. No-op when `state.isValid() == false`.
  void resumeFromState(const PendingState &state);

  /// Cancel the active run. In-flight items drain; finished signal
  /// fires with whatever's been accumulated so far. No-op when idle.
  void cancel();

  /// Skip just the item the active auto-runner is currently working on
  /// (forwarded to BatchScrapeRunner::skipCurrentItem) — aborts its
  /// in-flight hash/extraction and counts it skipped while the rest of
  /// the run continues. No-op when no auto-runner is active.
  void skipCurrentItem();

  /// Pause an interactive run. The current picker waits; the next
  /// `applyPick` / `skipPick` / `resumePaused` resumes. Implicit on
  /// dialog-close mid-pick.
  void pauseInteractive();

  /// Resume a paused interactive run — re-fires the lookup for the
  /// current item so the UI gets a fresh `pickerNeeded`.
  void resumePaused();

  /// Interactive-mode commit. UI calls this when the user clicks
  /// Apply on the current item's picker. `applied` is the
  /// post-download result (already persisted to disk by the dialog
  /// layer — same path the auto-mode runner uses internally).
  void applyPick(const Scraper::ScrapedItem &item);

  /// Interactive-mode skip. UI calls this when the user dismisses
  /// the picker without applying (e.g. clicks Cancel on the picker
  /// only, not the whole scrape).
  void skipPick();

  [[nodiscard]] State state() const { return m_state; }
  [[nodiscard]] bool isActive() const { return m_state != State::Idle; }
  [[nodiscard]] Mode currentMode() const { return m_mode; }
  [[nodiscard]] Summary summary() const { return m_summary; }
  [[nodiscard]] int totalItems() const { return m_totalItemsAtStart; }
  [[nodiscard]] int itemsCompleted() const { return m_itemsCompleted; }
  [[nodiscard]] qint64 startedAtUnixMs() const { return m_startedAtMs; }
  /// Total media bytes fetched across every collection in the
  /// active run — used by the dialog's Live view to display a
  /// rolling MiB/s rate. Persistent across collection boundaries
  /// because the runner's per-collection counter resets.
  [[nodiscard]] qint64 totalBytesDownloaded() const { return m_totalBytesDownloaded; }
  [[nodiscard]] QSet<QString> mediaFilter() const { return m_mediaFilter; }
  [[nodiscard]] bool writeMetadata() const { return m_writeMetadata; }
  /// Current collection name + currently-scraping item path (best-
  /// effort for the Live view caption). Empty when between items.
  [[nodiscard]] QString currentCollectionName() const { return m_currentCollectionName; }
  [[nodiscard]] QString currentItemPath() const { return m_currentItemPath; }
  /// Collection index of the job currently being processed. Returns
  /// -1 when idle / between jobs. Used by interactive callers that
  /// need the index to persist results.
  [[nodiscard]] int currentCollectionIndex() const {
    if (m_queueCursor < 0 || m_queueCursor >= m_queue.size()) return -1;
    return m_queue[m_queueCursor].collectionIndex;
  }
  /// Recent on-disk media file paths the persistence layer wrote for
  /// the last few completed items. Bounded to ~kRecentMediaCapacity
  /// most-recent entries; consumers use these as thumbnail sources.
  [[nodiscard]] QStringList recentMediaPaths() const { return m_recentMediaPaths; }
  /// Last item's scraped metadata — for the Live view's "currently
  /// scraping" panel. Empty (default-constructed) before any item
  /// has completed.
  [[nodiscard]] Scraper::ScrapedItem lastScrapedItem() const { return m_lastScrapedItem; }

  /// Load and clear any persisted state. Returns the snapshot for the
  /// caller to surface a resume prompt. After this call the file is
  /// gone; if the caller declines to resume, simply don't call
  /// `resumeFromState` and the state stays cleared.
  ///
  /// Pass `consumeOnLoad = false` to peek without clearing — caller
  /// can resume or discard explicitly via the file methods below.
  PendingState loadPendingState(bool consumeOnLoad = false);

  /// Explicit discard of any persisted state file. Used by the
  /// "Discard" branch of the resume prompt.
  void discardPendingState();

signals:
  void scrapeStarted(int totalItems);
  /// Fired as each item begins its provider lookup. `done` is the
  /// count completed across all collections (not per-collection).
  /// `currentName` is the item's basename for the UI caption.
  void itemBegan(int done, int total, const QString &collectionName, const QString &currentName);
  /// Fired by the provider when the current item enters a long-running
  /// stage (hashing an ISO, extracting an archive, awaiting the SS API
  /// response). `stage` is a short user-facing string ("Hashing ROM…",
  /// "Extracting archive…", "Querying ScreenScraper…") or empty when
  /// the stage clears. UI consumers route this into the progress view's
  /// per-item status line so the scrape doesn't appear hung during a
  /// multi-minute disc-image extraction (Kartend-ou0a).
  void itemStageChanged(const QString &stage);
  /// Fired after an item's applyScrapedItem returns. `scraped` is
  /// the provider's result for the Live view's metadata panel;
  /// `mediaPaths` are the on-disk files the persistence layer
  /// wrote so the UI can render thumbnails without re-fetching.
  void itemCompleted(int done, int total, const Scraper::ScrapedItem &scraped,
                     const QStringList &mediaPaths);
  /// Interactive-only: fired when the next item's candidates are
  /// ready and the UI should present its picker. Service blocks
  /// until applyPick / skipPick / pauseInteractive arrives.
  void pickerNeeded(const QString &itemPath, const QString &itemName,
                    const QList<Scraper::ScrapeCandidate> &candidates,
                    std::shared_ptr<MetadataLookupProvider> provider, const QString &artworkDir);
  void scrapePaused();
  void scrapeResumed();
  void scrapeFinished(const Summary &summary);
  /// Relays the active runner's `quotaUpdated` straight through so the
  /// dialog can show a live ScreenScraper request-quota readout. Only
  /// fires for providers that report a valid quota.
  void quotaUpdated(const Scraper::QuotaStatus &quota);

private:
  void pump();
  void startNextCollection();
  void startAutoCollection();
  void startInteractiveItem();
  /// Stop the whole queue because the provider's quota is exhausted, leaving
  /// the current cursor position (and therefore the un-finished jobs) in the
  /// persisted state as the resume point — mirrors onAutoFinished's quota
  /// branch. Shared by the interactive and entity paths.
  void stopForQuotaExhaustion();
  void onAutoItemBegan(int doneInCol, int totalInCol, const QString &name);
  void onAutoItemCompleted(int doneInCol, int totalInCol, const Scraper::ScrapedItem &scraped,
                           const QStringList &mediaPaths);
  void onAutoFinished(const BatchScrapeRunner::Summary &summary);
  /// Roll the active runner's per-collection Summary (counts +
  /// firstFailures) onto the pre-collection snapshot into m_summary.
  /// SET semantics so it is idempotent across the itemBegan /
  /// itemCompleted / finished hooks that all call it.
  void rollRunnerSummaryIntoSummary(const BatchScrapeRunner::Summary &runnerSummary);
  void interactiveLookupComplete(ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result);
  void persistState();
  /// Coalesces high-frequency persist calls (item completions during
  /// large auto-scrape jobs) onto a single debounced disk write so
  /// per-item bookkeeping stays O(1) on the main thread instead of
  /// O(remaining-queue) per item. Final flush happens on pause /
  /// cancel / finish / destruction so a crash mid-scrape still loses
  /// at most the debounce window of progress.
  void schedulePersist();
  /// Force any pending debounced persist to disk immediately.
  void flushPendingPersist();
  void clearStateFile();
  void appendRecentMedia(const QStringList &paths);
  [[nodiscard]] int countQueueRemaining() const;

  Context m_ctx;
  State m_state = State::Idle;
  Mode m_mode = Mode::Auto;
  QSet<QString> m_mediaFilter;
  bool m_writeMetadata = true;
  QList<CollectionJob> m_queue;
  int m_queueCursor = 0; ///< Index into m_queue of the active collection.
  /// Bumped on every run start (startScrape / resumeFromState) and on cancel().
  /// The entity-fetch callback captures the generation it was issued under and
  /// no-ops if it no longer matches — so a stale fetch that resolves after a
  /// cancel-then-restart can't mutate the new run's summary/cursor (the entity
  /// path has no m_autoRunner to detach the way the batch path does). Kartend-
  /// ckepd.2 review.
  quint64 m_runGeneration = 0;
  Summary m_summary;
  int m_totalItemsAtStart = 0;
  int m_itemsCompleted = 0;
  qint64 m_startedAtMs = 0;
  QString m_currentCollectionName;
  QString m_currentItemPath;
  Scraper::ScrapedItem m_lastScrapedItem;
  QStringList m_recentMediaPaths;
  static constexpr int kRecentMediaCapacity = 12;

  // Auto-mode runner. Lifecycle: created per active collection in
  // startAutoCollection, deletedLater on its finished signal.
  BatchScrapeRunner *m_autoRunner = nullptr;
  /// Snapshot of m_itemsCompleted at the moment the active collection
  /// started. The runner's `progress` signal carries `doneInCol`
  /// (count completed within this collection, including skip/error);
  /// we derive the global itemsCompleted as
  /// m_priorAtCollectionStart + doneInCol so the progress bar
  /// advances even when every item errors out before the first
  /// itemCompleted signal can fire.
  int m_priorAtCollectionStart = 0;
  /// Items in the current collection at start, used to backfill
  /// itemsCompleted at runner-finish time (covers the tail-end
  /// items the progress signal can't account for).
  int m_currentCollectionItemCount = 0;
  /// Snapshot of m_summary (scraped/skipped/errors) at the moment
  /// the active collection started. m_summary stays in sync with the
  /// runner's current state via
  /// m_summary = m_summaryAtCollectionStart + runner.currentSummary()
  /// updated on every progress/itemCompleted hop — so counts tick
  /// mid-collection instead of jumping at end.
  Summary m_summaryAtCollectionStart;
  /// Cumulative media bytes across every collection processed so
  /// far. Updated from the runner's per-collection counter on each
  /// progress/itemCompleted hop; persists across collection switches
  /// since the runner resets when a new one is constructed.
  qint64 m_totalBytesDownloaded = 0;
  qint64 m_bytesAtCollectionStart = 0;

  // Interactive-mode state.
  std::shared_ptr<MetadataLookupProvider> m_interactiveProvider;
  QString m_pausedItemPath; ///< Item the user was picking when paused/closed.

  // Debounced persistence. m_persistTimer is single-shot; each
  // schedulePersist() (re)starts the timer and sets m_persistDirty.
  // Timeout calls flushPendingPersist() which does the real write.
  QPointer<QTimer> m_persistTimer;
  bool m_persistDirty = false;

  // Entity media-write drain (mirrors BatchScrapeRunner's m_mediaWriteCancel /
  // m_inFlightMediaWrites). The write lambdas capture values + this shared
  // token only — never `this` — so an abandoned write past the destructor's
  // bounded drain can't UAF. A fresh token is allocated per run start so a
  // cancel can't poison the next run's writes.
  std::shared_ptr<std::atomic<bool>> m_entityWriteCancel =
      std::make_shared<std::atomic<bool>>(false);
  /// In-flight entity media writes on the global QThreadPool. Pruned of
  /// finished entries on each dispatch; the destructor flips the cancel token
  /// and drains this list with a bounded wait.
  QList<QFuture<Scraper::MediaWriteResult>> m_inFlightEntityWrites;
  /// Entity-flow engine (see the friend declaration above). Value member, so
  /// its lifetime is exactly this service's — async callbacks guard on a
  /// QPointer to the service and re-enter through this member.
  EntityScrapeCoordinator m_entityCoordinator{this};

  // Multi-instance guard. Held for the lifetime of an active run so a
  // second Kartend instance can tell the scrape is live and skips its
  // resume prompt. `m_ownsStateFile` gates every write to / removal of
  // `pending-scrape.json`: false means another live instance owns the
  // file, so this run must leave it untouched. Lock mechanics live in
  // ScrapeLock; the state-file JSON lives in ScrapePendingState.
  ScrapeLock m_scrapeLock;
  bool m_ownsStateFile = false;
};

} // namespace Scraper

#endif // SCRAPERSERVICE_H
