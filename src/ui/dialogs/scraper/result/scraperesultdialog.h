#ifndef SCRAPERESULTDIALOG_H
#define SCRAPERESULTDIALOG_H

#include <functional>
#include <memory>
#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QString>
#include <QTimer>

#include "batchscraperunner.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "scrapepersistence.h"
#include "scraperservice.h"
#include "scrapertypes.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QDialogButtonBox;
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

class BatchScrapeProgressView;
class IDatabaseManager;
namespace Scraper {
class ScrapeDownloadDispatcher;
}
#include "applicationcontext_fwd.h"
class MetadataLookupProvider;
class ScrapeResultDialogUnified;
class ScrapeResultSelectionModel;
class ScrapeResultThumbnailLoader;
class SingleItemScrapeView;
class ValueMarqueeTicker;
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
  Q_DISABLE_COPY_MOVE(ScrapeResultDialog)

  // ScrapeResultDialogUnified (Kartend-izpz, 3fkz step 3) owns the
  // unified-flow logic — queue walker, live metadata panel, ScraperService
  // signal handlers. ScrapeResultDialogUnified still reaches the host's widgets
  // through this friend declaration.
  friend class ScrapeResultDialogUnified;
  // Kartend-kggn8 / Kartend-hhv2u: ValueMarqueeTicker, ScrapeResultThumbnailLoader,
  // and ScrapeResultSelectionModel no longer need friendship — each is handed the
  // specific widgets / context it drives (the selection model via
  // setView()/setContext()) instead of reaching through a back-pointer.

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
    // Kartend-m02z: full app context (replaces the cached IDatabaseManager
    // pointer) so the BatchScrapeRunner this struct constructs can read
    // through ctx and stay in sync with ApplicationContext::managers.
    const ApplicationContext *ctx = nullptr;
    GeneralSettings *generalSettings = nullptr;
    std::function<std::shared_ptr<MetadataLookupProvider>(int collectionIndex)> providerBuilder;
    /// Invoked after each successful interactive-mode scrape so the
    /// caller can persist metadata + write media bytes to disk. Mirrors
    /// the existing `Scraper::applyScrapedItem` call in
    /// interactionmanager_contextmenu's runScrape lambda.
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
  /// reattaches to whatever state the service is in. Rebinding is
  /// structurally duplicate-proof: a same-service call is a no-op and
  /// a service change first disconnects every old-service connection.
  void setScraperService(Scraper::ScraperService *service);

  /// Single open-time rebinding entry for the reused dialog: binds the
  /// data callbacks and the long-lived service in one call. Safe to call
  /// on every open — each connection the open path relies on is either
  /// made once at construction (buildUi) or guarded structurally inside
  /// setScraperService, so re-opening can never stack duplicates.
  void bindForOpen(const ScraperContext &ctx, Scraper::ScraperService *service);

  /// Open the dialog in unified-setup mode. When `preCollectionIndex`
  /// is non-negative, that collection is pre-checked in the tree; when
  /// `preItemPath` is non-empty, only that one item inside the
  /// collection is pre-checked (every other item unchecked). Used by
  /// the right-click → Scraper… entry to scope down to a single item.
  void startUnifiedScrape(int preCollectionIndex = -1, const QString &preItemPath = QString());

  /// Kartend-ckepd.6: launch a one-shot Platform entity scrape for the given
  /// collection (right-click → "Scrape platform artwork"). No item grid — the
  /// provider resolves the collection's ScreenScraper systemeid and its platform
  /// artwork is fetched; progress/errors surface through this dialog.
  void startPlatformEntityScrape(int collectionIndex);

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
  [[nodiscard]] Scraper::BatchScrapeRunner::Summary batchSummary() const;

signals:
  /// Emitted after a unified-flow scrape completes (auto or
  /// interactive) so the caller can refresh the grid / details pane /
  /// artwork cache. Sent before the dialog closes itself; the caller
  /// can show a summary box.
  void unifiedScrapeFinished(int totalScraped, int totalSkipped, int totalErrors, int totalNotFound,
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
  void onApply();
  void onScrapeClicked();
  /// Opens a dialog listing the recorded scrape failure messages.
  /// Wired to the clickable error count in the unified counts label.
  void showScrapeErrorDetails();

  // ScraperService signal handlers (Kartend-3fkz step 2). Each was an
  // inline lambda inside setScraperService — extracted to named slots so
  // setScraperService becomes a connect table and the bodies are
  // reviewable in isolation.
  void onServiceScrapeStarted(int total);
  void onServiceItemBegan(int done, int total, const QString &collectionName, const QString &name);
  void onServiceItemCompleted(int done, int total, const Scraper::ScrapedItem &scraped,
                              const QStringList &mediaPaths);
  void onServicePickerNeeded(const QString &itemPath, const QString &itemName,
                             const QList<Scraper::ScrapeCandidate> &candidates,
                             const std::shared_ptr<MetadataLookupProvider> &provider,
                             const QString &artworkDir);
  void onServiceScrapeFinished(const Scraper::ScraperService::Summary &s);
  void onServiceScrapePaused();
  void onServiceQuotaUpdated(const Scraper::QuotaStatus &quota);

private:
  enum class Mode { SingleItem, Batch, Unified };
  enum class UnifiedPhase { Setup, AutoRunning, InteractiveLookingUp, InteractivePicking, Done };

  void buildUi();
  /// Reset every run-scoped member in one place. Called by the open path
  /// (startUnifiedScrape's fresh-setup branch, i.e. whenever the dialog is
  /// not re-attaching to a live run) and again on each scrapeStarted, so no
  /// state from a previous run leaks across a hide-and-reopen or into the
  /// next run. Session-scoped state (the custom-field key union, the last
  /// known quota reset time) is deliberately left intact.
  void resetRunState();
  /// Skip the item currently being scraped in the Unified flow —
  /// dispatches to the active ScraperService (its auto-runner) or, in
  /// the legacy in-dialog path, the directly-bound BatchScrapeRunner.
  /// Wired to m_skipItemButton.
  void skipCurrentScrapeItem();
  /// Configure + start the non-UI Scraper::ScrapeDownloadDispatcher for the
  /// assets the user just confirmed via Apply (Kartend-3fkz step 5,
  /// Kartend-dpehr). The dispatcher owns the dedup/CRC/async-fetch logic; this
  /// only forwards its progressed()/finished() signals to the UI + m_result.
  void dispatchSelectedDownloads(const QList<Scraper::MediaAsset> &selected,
                                 const std::shared_ptr<QElapsedTimer> &applyTimer);
  /// Format the elapsed/ETA strings shared by both modes. `etaMs` is
  /// the projected milliseconds remaining; pass <= 0 to display "—".
  [[nodiscard]] static QString formatDuration(qint64 ms);
  /// Update the single-item or batch progress status line. Centralises
  /// the "items done / total · rate · ETA" formatting so the two modes
  /// stay visually consistent.
  void updateSingleItemProgress(int completed);

  // Unified-flow logic is split across four controllers (all friends):
  // ScrapeResultDialogUnified (queue walker + live metadata panel +
  // ScraperService handlers), ScrapeResultSelectionModel (collection-tree /
  // items-list selection state), ScrapeResultThumbnailLoader (recent-media
  // strip), and ValueMarqueeTicker (the per-150ms chip scroll). Kartend-unlta:
  // each now owns its own slice of state — the unified controller's
  // queue/totals/interactive/rate state moved off this host onto it. The host
  // keeps only the shell state the legacy flows read (mode/phase/result/batch).
  // The onScrapeClicked slot above is a 1-line trampoline into m_unified.

  // m_provider, m_candidates, m_detailCache, m_currentRow, m_currentDetail
  // moved into SingleItemScrapeView in Kartend-xvci step 4. The view drives
  // candidate selection + detail fetching; the host reads currentDetail()
  // in onApply and toggles m_applyButton on detailLoaded / detailFailed.

  QPushButton *m_applyButton = nullptr;
  // Apply / Scrape / Close are all added to the one QDialogButtonBox in
  // buildUi (construct-once, unified pair starts hidden); no stored box
  // handle is needed now that nothing adds buttons after construction.
  QPushButton *m_scrapeButton = nullptr;
  /// Outer mode-swap: page 0 hosts the existing single-item splitter
  /// (candidate / detail / media); page 1 hosts the batch-progress
  /// panel; page 2 hosts the unified-setup panel.
  QStackedWidget *m_modeStack = nullptr;
  SingleItemScrapeView *m_singleItemView = nullptr;
  BatchScrapeProgressView *m_batchView = nullptr;
  QWidget *m_unifiedPage = nullptr;

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
  /// Live ScreenScraper request-quota readout ("N / M requests today
  /// · resets HH:MM"). Hidden by default and whenever the active
  /// provider reports no quota (every non-SS provider, and SS before
  /// its first response); shown only once a valid quota arrives via
  /// the service's quotaUpdated signal during a live scrape.
  QLabel *m_unifiedQuotaLabel = nullptr;
  // m_lastQuotaResetText and m_shownCollectionName moved onto
  // ScrapeResultDialogUnified — only its handlers ever read or wrote them.
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
  /// items contribute new keys. The key union, the persistent per-key
  /// cells, and the typed-chip boundary index moved onto
  /// ScrapeResultDialogUnified, which owns every read and write.
  QWidget *m_liveExtrasContainer = nullptr;
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

  /// Provider-supplied health/load message surfaced before Apply.
  /// Populated from MetadataLookupProvider::fetchHealthStatus on
  /// dialog construction; hidden when empty so providers without a
  /// health endpoint don't reserve dead screen space. Owned by
  /// m_singleItemView; this flag tracks whether Apply should stay
  /// disabled regardless of candidate selection.
  /// True when the provider reported an upstream-closed condition
  /// the user can't fix from here (SS leecher tier closed, etc.).
  /// Apply stays disabled even after a candidate is selected.
  bool m_healthBlocksApply = false;

  Result m_result;
  // Total assets the user picked for download in the active Apply
  // cycle, used to display "downloaded N of M" progress without
  // recomputing it on every completion.
  int m_downloadsTotal = 0;
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
  /// Per-instance guard so this dialog contributes to g_visibleInstanceCount
  /// at most once, even if Qt delivers unpaired show/hide events (Qt 6.8
  /// double-fires showEvent on an already-visible window). See showEvent.
  bool m_countedVisible = false;
  /// Non-owning — caller's BatchScrapeRunner. Lifetime extends past
  /// the dialog's exec() because the caller wires deletion to the
  /// runner's `finished` signal after the dialog handles it.
  Scraper::BatchScrapeRunner *m_batchRunner = nullptr;
  /// Kartend-dpehr: owns the download orchestration (dedup/CRC/async fetch).
  /// Parented to the dialog (created lazily in dispatchSelectedDownloads), so a
  /// dialog destroyed mid-download takes the dispatcher and its QPointer-guarded
  /// fetch callbacks with it.
  Scraper::ScrapeDownloadDispatcher *m_downloadDispatcher = nullptr;
  /// In-dialog stage label (Kartend-ou0a). Shows "Hashing ROM…",
  /// "Extracting archive for hash ID…", "Looking up…" while the
  /// provider is working. Hidden when empty so fast scrapes don't
  /// reserve vertical space for it. Driven by ScraperService's
  /// itemStageChanged + BatchScrapeRunner's itemStageChanged.
  QLabel *m_stageLabel = nullptr;
  /// "Skip this item" shown in lockstep with m_stageLabel so the user
  /// can abandon the current item during a long hash/extraction without
  /// cancelling the whole run. Dispatches to the active ScraperService
  /// (auto-runner) or the directly-bound BatchScrapeRunner.
  QPushButton *m_skipItemButton = nullptr;

  // ── Unified-flow state ──────────────────────────────────────────
  ScraperContext m_scraperCtx;
  Scraper::ScraperService *m_service = nullptr; ///< Non-owning; lives on MainWindow.
  // Kartend-unlta: the scrape-orchestration state (queue + cursor, aggregate
  // totals, interactive-mode iterator, the rate-sampling window, and the
  // cancel flag) moved onto ScrapeResultDialogUnified, which already owns the
  // queue walker + interactive driver that mutate it. What stays here is the
  // state the dialog shell itself reads directly: the phase, the live-tick
  // timer, and the service/context handles above.
  UnifiedPhase m_unifiedPhase = UnifiedPhase::Setup;
  /// 1-second tick that keeps the Live view's timing/rate readout
  /// fresh between item-event signals (a slow download can leave the
  /// label stale otherwise). Started when the service goes active,
  /// stopped on scrapeFinished. Interval + timeout connection are wired
  /// exactly once, in buildUi — every run/show/hide path only ever
  /// calls start()/stop() on it.
  QTimer m_liveTickTimer;

  /// Owns the unified-flow queue walker, live-metadata panel renderer, and
  /// ScraperService signal handlers. Constructed once in the ctor with a
  /// back-pointer to `this`. Kartend-unlta: it also now owns the unified
  /// orchestration state (queue/totals/interactive/rate) that used to sit on
  /// this host. The legacy SingleItem / Batch paths keep their direct access
  /// to the state that remains here (mode, phase, batch runner, result).
  std::unique_ptr<ScrapeResultDialogUnified> m_unified;
  /// Owns the collection-tree + items-list selection state (which
  /// collections/items are checked, the per-collection item cache, and the
  /// owning-collection map). Built before buildUnifiedPanel wires the tree
  /// signals to it.
  std::unique_ptr<ScrapeResultSelectionModel> m_selectionModel;
  /// Decodes + appends recent-media thumbnails to the live filmstrip off
  /// the UI thread.
  std::unique_ptr<ScrapeResultThumbnailLoader> m_thumbLoader;
  /// Drives the per-150ms left-to-right scroll of overflowing value chips
  /// in the live metadata panel.
  std::unique_ptr<ValueMarqueeTicker> m_marqueeTicker;
};

#endif // SCRAPERESULTDIALOG_H
