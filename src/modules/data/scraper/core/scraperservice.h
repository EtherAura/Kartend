#ifndef SCRAPERSERVICE_H
#define SCRAPERSERVICE_H

#include <functional>
#include <memory>

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "batchscraperunner.h"
#include "collectionutils.h"
#include "metadatalookupprovider.h"
#include "scrapertypes.h"

class IDatabaseManager;
struct ApplicationContext;
class QLockFile;
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
  };

  /// Aggregate running summary; mirrors BatchScrapeRunner::Summary but
  /// accumulates across the whole queue.
  struct Summary {
    int scraped = 0;
    int skipped = 0;
    int errors = 0;
    /// Aggregate media-files written across every collection so the
    /// dialog's Live view can render "items: X · media: Y" instead
    /// of just an item count.
    int mediaWritten = 0;
    QStringList firstFailures;
    /// Set when a collection's runner stopped because ScreenScraper's
    /// daily quota was exhausted (HTTP 430/431). The service then
    /// stops walking the queue and leaves the persisted resume point
    /// intact so the user can continue after the quota resets.
    bool quotaExhausted = false;
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
  [[nodiscard]] static QString pendingStateFilePath();
  /// Sibling lock file (`pending-scrape.json.lock`) used to mark the
  /// pending-scrape state as owned by a live process.
  [[nodiscard]] static QString pendingStateLockFilePath();
  /// Take the pending-scrape ownership lock for this run. Returns true
  /// when acquired (or already held). Returns false when another live
  /// Kartend instance already owns the scrape — the caller then runs
  /// without persisting resumable state. A lock left by a crashed
  /// owner is stale (its PID is dead) and gets reclaimed here.
  [[nodiscard]] bool acquireScrapeLock();
  /// Drop the ownership lock and remove its on-disk file. Safe to call
  /// when no lock is held.
  void releaseScrapeLock();
  /// True when `pending-scrape.json` belongs to a still-running
  /// Kartend instance — i.e. a second instance must NOT offer to
  /// resume that scrape. False when nobody holds it or the previous
  /// owner crashed (stale lock).
  [[nodiscard]] static bool pendingScrapeOwnedByLiveInstance();
  void appendRecentMedia(const QStringList &paths);
  [[nodiscard]] int countQueueRemaining() const;

  Context m_ctx;
  State m_state = State::Idle;
  Mode m_mode = Mode::Auto;
  QSet<QString> m_mediaFilter;
  bool m_writeMetadata = true;
  QList<CollectionJob> m_queue;
  int m_queueCursor = 0; ///< Index into m_queue of the active collection.
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
  QTimer *m_persistTimer = nullptr;
  bool m_persistDirty = false;

  // Multi-instance guard. Held for the lifetime of an active run so a
  // second Kartend instance can tell the scrape is live and skips its
  // resume prompt. `m_ownsStateFile` gates every write to / removal of
  // `pending-scrape.json`: false means another live instance owns the
  // file, so this run must leave it untouched.
  std::unique_ptr<QLockFile> m_scrapeLock;
  bool m_ownsStateFile = false;
};

} // namespace Scraper

#endif // SCRAPERSERVICE_H
