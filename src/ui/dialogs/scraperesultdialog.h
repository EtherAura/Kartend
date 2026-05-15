#ifndef SCRAPERESULTDIALOG_H
#define SCRAPERESULTDIALOG_H

#include <functional>
#include <memory>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "batchscraperunner.h"
#include "collectionutils.h"
#include "scrapepersistence.h"
#include "scraperservice.h"
#include "scrapertypes.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSplitter;
class QStackedWidget;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
QT_END_NAMESPACE

class IDatabaseManager;
class MetadataLookupProvider;
struct GeneralSettings;

/// Unified scraper dialog. Replaces the old per-item-only ScrapeResultDialog
/// and the separate batch-progress modal — both individual and batch
/// scrapes now flow through this one window.
///
/// Three top-level views (`m_modeStack` pages):
///   • SingleItem (legacy) — kept for the existing right-click → Scrape
///     interactive-picker flow that already passes fetched candidates.
///   • BatchProgress — progress-only view used by the unified flow's
///     auto-accept mode while items chew through a single collection.
///   • Unified — collection tree + items list + media-type checkboxes
///     + Auto / Interactive toggle + Cancel / Scrape buttons. Opened
///     from File → Scraper… and from the right-click "Scraper…" entry
///     (preselected to that one item).
///
/// In Unified-Auto mode, each checked collection's items are run
/// through a BatchScrapeRunner (one runner per collection, in tree
/// order); in Unified-Interactive mode the dialog walks the queue
/// itself and pops the SingleItem candidate picker per item before
/// moving on. Cancellation commits whatever's already landed.
class ScrapeResultDialog : public QDialog {
  Q_OBJECT
public:
  /// Outcome of a successful Apply. The `media` list mirrors the
  /// detail's media but only contains entries the user checked, with
  /// `bytes` (a non-Scraper::MediaAsset field, see below) populated.
  /// Caller is responsible for writing each MediaDownload's bytes
  /// to disk under its preferred filename.
  struct MediaDownload {
    Scraper::MediaAsset asset;
    QByteArray bytes;
  };
  struct Result {
    Scraper::ScrapedItem item;
    QList<MediaDownload> downloads;
  };

  /// Wiring for the unified flow. Caller (MainWindow) fills these in
  /// via setScraperContext before the dialog is shown when the flow
  /// is anything other than the legacy single-item right-click path.
  ///
  /// `providerBuilder` returns a fresh provider for a given collection
  /// index — implemented inside MainWindow because the registry needs
  /// per-call accessors for live settings/collection state.
  /// `applyResult` is the post-scrape persistence hook the dialog
  /// invokes per item in interactive mode (mirroring the existing
  /// right-click flow's applyScrapedItem path).
  struct ScraperContext {
    QList<CollectionConfig> *collections = nullptr;
    IDatabaseManager *databaseManager = nullptr;
    GeneralSettings *generalSettings = nullptr;
    std::function<std::shared_ptr<MetadataLookupProvider>(int collectionIndex)> providerBuilder;
    /// Invoked after each successful interactive-mode scrape so the
    /// caller can persist metadata + write media bytes to disk. Mirrors
    /// the existing `Scraper::applyScrapedItem` call in
    /// interactionmanagercontextmenu's runScrape lambda.
    std::function<void(int collectionIndex, const QString &filePath, const Result &result)>
        applyResult;
  };

  ScrapeResultDialog(MetadataLookupProvider *provider, QList<Scraper::ScrapeCandidate> candidates,
                     QWidget *parent = nullptr);
  ~ScrapeResultDialog() override;

  /// True while any ScrapeResultDialog instance is currently visible.
  /// Driven by show/hide events; lets the main window's input filter
  /// suppress wheel/click/arrow-key selection changes while the user
  /// is working in the scraper.
  [[nodiscard]] static bool isAnyInstanceVisible();

  /// Wire the unified-flow data callbacks. Required before
  /// `startUnifiedScrape` does anything useful.
  void setScraperContext(const ScraperContext &ctx);

  /// Hand the dialog the long-lived ScraperService it should drive.
  /// Required for any unified-flow scrape that should survive
  /// dialog close / app restart. The dialog observes the service's
  /// signals to keep its Live view in sync; closing the dialog only
  /// hides it (the service keeps running). On re-entry the dialog
  /// reattaches to whatever state the service is in.
  void setScraperService(Scraper::ScraperService *service);

  /// Open the dialog in unified-setup mode. When `preCollectionIndex`
  /// is non-negative, that collection is pre-checked in the tree; when
  /// `preItemPath` is non-empty, only that one item inside the
  /// collection is pre-checked (every other item unchecked). Used by
  /// the right-click → Scraper… entry to scope down to a single item.
  void startUnifiedScrape(int preCollectionIndex = -1, const QString &preItemPath = QString());

  /// Artwork directories used to skip downloads for group/company-
  /// scoped assets that already sit on disk. The first entry is the
  /// active collection (where new shared assets will be written); the
  /// rest are sibling collections probed for cross-collection dedup
  /// — if Final Fantasy's "RPG theme" background was already fetched
  /// for a PS2 collection, scraping a PS1 game in the same theme can
  /// copy it over instead of re-downloading. Setter is optional; when
  /// not called, no dedup happens and every shared asset is fetched
  /// fresh.
  void setSharedAssetSearchPaths(const QStringList &paths);

  /// Per-game rescrape context. Lets the dialog send a hash hint to
  /// SS's mediaJeu.php (`crc=` / `md5=` / `sha1=`) for assets that
  /// already exist on disk under FillMissing or UpdateChanged modes
  /// — SS responds with a tiny "*OK" body when the local file
  /// matches its server-side copy, so the dialog can skip the actual
  /// download. Empty `artworkDir` or Overwrite mode disables the
  /// short-circuit; the dialog falls back to the unconditional fetch
  /// path.
  void setRescrapeContext(const QString &artworkDir, const QString &baseName,
                          Scraper::RescrapeMode rescrapeMode);

  /// Populated when the dialog accepted (exec() returned QDialog::Accepted).
  /// Empty when rejected.
  [[nodiscard]] Result result() const { return m_result; }

  /// Switch the dialog into batch-progress mode. The candidate /
  /// detail / media UI is hidden; a progress bar + per-item label +
  /// elapsed/ETA + counts panel takes its place. The dialog observes
  /// `runner`'s progress and finished signals and calls accept() when
  /// the runner emits finished. Cancel is wired to runner->cancel().
  ///
  /// Caller is responsible for calling `runner->start()` after this
  /// (or after `exec()`); the dialog only observes.
  void setBatchRunner(Scraper::BatchScrapeRunner *runner, const QString &collectionName,
                      int totalItems);

  /// Summary captured from the BatchScrapeRunner's `finished` signal.
  /// Only meaningful after exec() returns in batch mode.
  [[nodiscard]] Scraper::BatchScrapeRunner::Summary batchSummary() const { return m_batchSummary; }

signals:
  /// Emitted after a unified-flow scrape completes (auto or
  /// interactive) so the caller can refresh the grid / details pane /
  /// artwork cache. Sent before the dialog closes itself; the caller
  /// can show a summary box.
  void unifiedScrapeFinished(int totalScraped, int totalSkipped, int totalErrors,
                             const QStringList &firstFailures);

protected:
  /// In Unified mode we want X-click to behave like the Close button
  /// — hide the dialog without canceling the underlying ScraperService.
  /// Legacy single-item flow uses QDialog's default reject-on-close.
  void closeEvent(QCloseEvent *event) override;
  /// Stop the per-second live tick + per-150ms marquee timers when
  /// the dialog is hidden so a background scrape doesn't keep paying
  /// for invisible UI ticks (the scrape itself keeps running). The
  /// itemCompleted handler short-circuits its heavy pixmap-scale
  /// work the same way — see ScrapeResultDialog::isLiveViewVisible.
  void hideEvent(QHideEvent *event) override;
  void showEvent(QShowEvent *event) override;

private slots:
  void onCandidateSelected(int row);
  void onApply();
  void onCollectionTreeCurrentChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
  void onCollectionCheckChanged(QTreeWidgetItem *item, int column);
  void onItemCheckChanged(QListWidgetItem *item);
  void onScrapeClicked();

private:
  enum class Mode { SingleItem, Batch, Unified };
  enum class UnifiedPhase { Setup, AutoRunning, InteractiveLookingUp, InteractivePicking, Done };

  void buildUi();
  void buildBatchPanel();
  void buildUnifiedPanel();
  void populateMediaCheckboxes(const Scraper::ScrapedItem &item);
  void downloadNextSelectedMedia();
  /// Format the elapsed/ETA strings shared by both modes. `etaMs` is
  /// the projected milliseconds remaining; pass <= 0 to display "—".
  [[nodiscard]] static QString formatDuration(qint64 ms);
  /// Update the single-item or batch progress status line. Centralises
  /// the "items done / total · rate · ETA" formatting so the two modes
  /// stay visually consistent.
  void updateSingleItemProgress(int completed);
  void updateBatchProgress(int done, int total, const QString &currentName);

  // ── Unified flow helpers ────────────────────────────────────────
  void populateCollectionTree();
  void rebuildItemsList(int collectionIndex);
  void setUnifiedSetupEnabled(bool enabled);
  void updateUnifiedProgressLabel();
  /// Total items checked across every checked collection — the
  /// denominator for progress + ETA computations.
  [[nodiscard]] int totalCheckedItemCount() const;
  /// Walk the collection queue, run the next not-yet-scraped one.
  /// Each call either kicks off the next collection or fires the
  /// "all done" summary path.
  void startNextCollectionInQueue();
  /// Drive auto-mode for a single collection: build the runner, attach
  /// signals, switch to BatchProgress page, start the runner.
  void runAutoCollection(int collectionIndex, const QStringList &items);
  /// Drive interactive-mode for a single collection: walk its items
  /// one at a time through the candidate picker.
  void runInteractiveCollection(int collectionIndex, const QStringList &items);
  void interactiveNextItem();
  void interactiveOnLookupResult(ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result);
  void interactiveOnApplied();
  void interactiveOnSkipped();
  /// Single-item Apply's completion path branches here so the unified
  /// interactive flow can advance to the next item instead of closing
  /// the dialog. Default behaviour (legacy single-item path): accept().
  void finishCurrentApply();

  MetadataLookupProvider *m_provider = nullptr;
  QList<Scraper::ScrapeCandidate> m_candidates;
  // Cached detail per candidate row so re-clicking doesn't refetch.
  QHash<int, Scraper::ScrapedItem> m_detailCache;
  // Loading state for the currently-selected candidate.
  int m_currentRow = -1;
  Scraper::ScrapedItem m_currentDetail;
  // Per-media-row checkbox + asset pairing for the active detail.
  QList<QPair<QCheckBox *, Scraper::MediaAsset>> m_mediaRows;

  QListWidget *m_candidateList = nullptr;
  QStackedWidget *m_detailStack = nullptr; // 0 = empty, 1 = loading, 2 = detail
  QLabel *m_emptyLabel = nullptr;
  QLabel *m_loadingLabel = nullptr;
  QTextBrowser *m_detailText = nullptr;
  QListWidget *m_mediaList = nullptr;
  QPushButton *m_applyButton = nullptr;
  QPushButton *m_scrapeButton = nullptr;
  QLabel *m_statusLabel = nullptr; // mid-Apply progress message
  /// Outer mode-swap: page 0 hosts the existing single-item splitter
  /// (candidate / detail / media); page 1 hosts the batch-progress
  /// panel; page 2 hosts the unified-setup panel.
  QStackedWidget *m_modeStack = nullptr;
  QWidget *m_singleItemPage = nullptr;
  QWidget *m_batchPage = nullptr;
  QWidget *m_unifiedPage = nullptr;
  QLabel *m_batchHeaderLabel = nullptr;
  QLabel *m_batchCurrentLabel = nullptr;
  QProgressBar *m_batchProgressBar = nullptr;
  QLabel *m_batchTimingLabel = nullptr;
  QLabel *m_batchCountsLabel = nullptr;

  // ── Unified-page widgets ────────────────────────────────────────
  QTreeWidget *m_collectionTree = nullptr;
  QLabel *m_itemsHeaderLabel = nullptr;
  QListWidget *m_unifiedItemsList = nullptr;
  QGroupBox *m_mediaTypesGroup = nullptr;
  QHash<QString, QCheckBox *> m_mediaTypeChecks;
  QRadioButton *m_modeAutoRadio = nullptr;
  QRadioButton *m_modeInteractiveRadio = nullptr;
  QProgressBar *m_unifiedProgressBar = nullptr;
  QLabel *m_unifiedTimingLabel = nullptr;
  QLabel *m_unifiedCountsLabel = nullptr;
  QLabel *m_unifiedCurrentLabel = nullptr;
  // Live-view widgets (shown when ScraperService is active). Layered
  // into the unified page; visibility toggled by setUnifiedSetupEnabled.
  QGroupBox *m_liveMetadataGroup = nullptr;
  /// Single-line fields use QLineEdit (read-only) so they share the
  /// same sunken text-field look as the Description QTextBrowser
  /// below. Visual consistency across the whole "Currently scraping"
  /// panel — every value sits inside a framed text widget instead
  /// of some sitting bare on the form's background.
  QLineEdit *m_liveMetadataTitle = nullptr;
  QLineEdit *m_liveMetadataPublisher = nullptr;
  QLineEdit *m_liveMetadataReleased = nullptr;
  QLineEdit *m_liveMetadataGenre = nullptr;
  QLineEdit *m_liveMetadataDeveloper = nullptr;
  QLineEdit *m_liveMetadataPlayers = nullptr;
  QLineEdit *m_liveMetadataContentRating = nullptr;
  QLineEdit *m_liveMetadataRuntime = nullptr;
  QLineEdit *m_liveMetadataTags = nullptr;
  QLineEdit *m_liveMetadataSource = nullptr;
  QTextBrowser *m_liveMetadataDescription = nullptr;
  /// Container that holds dynamically-rebuilt rows of custom-field
  /// label+value pairs (five pairs per row, matching the typed-fields
  /// grid above). Re-populated by `populateCustomFields` on every
  /// `itemCompleted` against the union of every key ever seen so the
  /// section size stays stable instead of growing as new providers /
  /// items contribute new keys.
  QWidget *m_liveExtrasContainer = nullptr;
  /// Union of every custom-field key seen across this scrape session.
  /// `populateCustomFields` adds new keys then renders the union — so
  /// the section always shows every possible field, with empty values
  /// for keys not present in the current scraped item.
  QSet<QString> m_allSeenCustomKeys;
  /// Persistent per-key QLineEdit cells inside m_liveExtrasContainer.
  /// Created once (either at panel build time for the pre-seeded
  /// "known" keys, or lazily when a new key first appears) and reused
  /// across items — populateCustomFields just rewrites the .text on
  /// each cell, so the section's widget count + layout stay rock-
  /// stable instead of being torn down and rebuilt every item.
  QHash<QString, QLineEdit *> m_customFieldEdits;
  /// Number of fixed typed-field chips occupying the first N slots
  /// of m_liveExtrasContainer's FlowLayout. Custom-field chips are
  /// appended after these and torn down / rebuilt from index
  /// m_typedChipCount onward, leaving the typed chips untouched.
  int m_typedChipCount = 0;
  /// Build (or rebuild) the custom-fields rows inside
  /// m_liveExtrasContainer. Each pair lays out a label+QLineEdit
  /// cell in the same 5-pair-per-row pattern as the typed-fields
  /// grid above.
  void populateCustomFields(const QHash<QString, QString> &fields);
  QGroupBox *m_liveThumbsGroup = nullptr;
  QListWidget *m_liveThumbsStrip = nullptr;
  QPushButton *m_closeButton = nullptr;
  /// Setup-only widgets hidden while a scrape is in flight so the
  /// Live view (metadata + thumbnails + progress) gets the full
  /// vertical space.
  QWidget *m_unifiedSplitterContainer = nullptr;
  QWidget *m_modeRowContainer = nullptr;
  /// Candidate picker row shown only during interactive scraping
  /// while the unified live view is the active page. Holds a label
  /// + QComboBox listing candidates for the current item; selecting
  /// a row re-fetches detail and refreshes the live metadata fields.
  QWidget *m_interactiveCandidateRow = nullptr;
  class QComboBox *m_interactiveCandidateCombo = nullptr;
  /// Populate every QLineEdit/QTextBrowser in the live metadata
  /// panel from `item`. Shared between the auto-mode `itemCompleted`
  /// signal and the interactive-mode candidate-detail callback so
  /// both paths render to identical widgets.
  void applyScrapedItemToLive(const Scraper::ScrapedItem &item);
  /// Decode + scale a thumbnail off the UI thread, then append it to
  /// the live thumb strip on the main thread. Replaces a synchronous
  /// `QPixmap(path) + Qt::SmoothTransformation` per item — that ran
  /// on the UI thread and was the dominant per-item stall under high
  /// scrape concurrency. The watcher is parented to `this` so a
  /// dialog close cleans up any in-flight decodes safely.
  void appendThumbAsync(const QString &path);
  /// Ticks every ~150 ms while a scrape is running. Walks every
  /// QLineEdit value cell in the metadata panel; if text overflows
  /// the cell width, scrolls the cursor one character to the right
  /// (which scrolls the visible text leftward), then pauses at the
  /// end before snapping back to position 0. No reverse animation
  /// — direction is always L→R, then wrap.
  void tickValueMarquees();
  /// Per-cell pause counter: while a value is parked at the
  /// rightmost scroll position, this counts down before the cell
  /// snaps back to cursor 0. Cells without entries are in the
  /// advancing phase.
  QHash<class QLineEdit *, int> m_marqueePauseTicks;
  class QTimer *m_marqueeTimer = nullptr;
  /// Interactive-mode helper: fetches detail for the candidate at
  /// `idx` from m_candidates and pipes the result into
  /// applyScrapedItemToLive. Enables the Apply button on success.
  void interactiveFetchDetail(int idx);

  /// Provider-supplied health/load message surfaced before Apply.
  /// Populated from MetadataLookupProvider::fetchHealthStatus on
  /// dialog construction; hidden when empty so providers without a
  /// health endpoint don't reserve dead screen space.
  QLabel *m_healthLabel = nullptr;
  /// True when the provider reported an upstream-closed condition
  /// the user can't fix from here (SS leecher tier closed, etc.).
  /// Apply stays disabled even after a candidate is selected.
  bool m_healthBlocksApply = false;

  Result m_result;
  // Total assets the user picked for download in the active Apply
  // cycle, used to display "downloaded N of M" progress without
  // recomputing it on every completion.
  int m_downloadsTotal = 0;
  // Outstanding downloads in flight. The dialog fires every selected
  // asset at once and lets HttpClient pace them according to the
  // host's throttle + concurrency policy; we just count completions
  // and accept() when the last one lands.
  int m_downloadsPending = 0;
  // Running totals for the download-rate readout in the status label.
  // m_downloadedBytes accumulates the size of every completed reply
  // (errors counted as zero bytes); m_downloadStartMs is the wallclock
  // anchor we divide against to compute MB/s.
  qint64 m_downloadedBytes = 0;
  qint64 m_downloadStartMs = 0;
  // Active + sibling collection artwork roots, walked for cross-
  // collection dedup of group/company-scoped assets. First entry is
  // the active collection; the rest are siblings probed in order.
  QStringList m_sharedSearchPaths;
  /// Per-game rescrape context (see setRescrapeContext). Empty
  /// `m_rescrapeArtworkDir` means "no short-circuit" — every selected
  /// asset takes the unconditional fetch path.
  QString m_rescrapeArtworkDir;
  QString m_rescrapeBaseName;
  Scraper::RescrapeMode m_rescrapeMode = Scraper::RescrapeMode::Overwrite;

  Mode m_mode = Mode::SingleItem;
  /// Non-owning — caller's BatchScrapeRunner. Lifetime extends past
  /// the dialog's exec() because the caller wires deletion to the
  /// runner's `finished` signal after the dialog handles it.
  Scraper::BatchScrapeRunner *m_batchRunner = nullptr;
  Scraper::BatchScrapeRunner::Summary m_batchSummary;
  /// Wall-clock anchor for batch ETA. Set when setBatchRunner is
  /// called (which is "just before exec/start").
  qint64 m_batchStartMs = 0;
  int m_batchTotalItems = 0;
  QString m_batchCollectionName;

  // ── Unified-flow state ──────────────────────────────────────────
  ScraperContext m_scraperCtx;
  Scraper::ScraperService *m_service = nullptr; ///< Non-owning; lives on MainWindow.
  /// Per-collection-index inclusion sets. When a collection's tree
  /// checkbox is on, this list dictates which item paths to scrape
  /// from that collection (defaults to "all" when the collection is
  /// first checked). Persists across collection clicks so the user
  /// can freely switch between collections without losing per-item
  /// selections.
  QHash<int, QStringList> m_itemSelectionByCollection;
  /// All item paths per collection — populated lazily as the user
  /// clicks a collection in the tree (or via DB fetch). Cached so
  /// re-clicking a collection doesn't re-hit the database.
  QHash<int, QStringList> m_itemsCacheByCollection;
  /// Tree row → collection index map, populated when the tree is built.
  QHash<QTreeWidgetItem *, int> m_treeItemToCollectionIndex;
  /// Snapshot at scrape time: queue of (collectionIndex, items) tuples
  /// to process in sequence. AutoRunning + Interactive both walk this.
  struct CollectionJob {
    int collectionIndex;
    QString collectionName;
    QStringList items;
  };
  QList<CollectionJob> m_unifiedQueue;
  int m_unifiedQueueCursor = 0;
  UnifiedPhase m_unifiedPhase = UnifiedPhase::Setup;
  qint64 m_unifiedStartMs = 0;
  /// Sliding window of (timestampMs, cumulativeBytes) samples used
  /// to compute a recent download rate for the Live view. Pruned to
  /// the last ~10 seconds of samples on every update so the rate
  /// readout reflects current activity, not the all-run average
  /// (which gets dragged down by long lookup-API idle stretches).
  QList<QPair<qint64, qint64>> m_rateSamples;
  /// 1-second tick that keeps the Live view's timing/rate readout
  /// fresh between item-event signals (a slow download can leave the
  /// label stale otherwise). Started when the service goes active,
  /// stopped on scrapeFinished.
  class QTimer *m_liveTickTimer = nullptr;
  int m_unifiedItemsCompletedAcross = 0;
  int m_unifiedScrapedTotal = 0;
  int m_unifiedSkippedTotal = 0;
  int m_unifiedErrorsTotal = 0;
  QStringList m_unifiedFailures;
  /// Active provider for the current collection (interactive mode).
  std::shared_ptr<MetadataLookupProvider> m_interactiveProvider;
  /// Iterator state for interactive mode: items list for current
  /// collection, cursor into it.
  QStringList m_interactiveItems;
  int m_interactiveCursor = 0;
  int m_interactiveCollectionIndex = -1;
  bool m_unifiedCancelled = false;
};

#endif // SCRAPERESULTDIALOG_H
