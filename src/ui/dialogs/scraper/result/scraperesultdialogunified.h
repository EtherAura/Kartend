#ifndef SCRAPERESULTDIALOGUNIFIED_H
#define SCRAPERESULTDIALOGUNIFIED_H

#include "errorutils.h"
#include "scraperservice.h"
#include "scrapertypes.h"

#include <functional>
#include <memory>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QGroupBox;
class QLineEdit;
class QListWidgetItem;
class QTreeWidgetItem;
class QWidget;
QT_END_NAMESPACE

class MetadataLookupProvider;
class ScrapeResultDialog;

/// Owns the unified-flow behaviour previously embedded in ScrapeResultDialog:
/// the live-view panel builder, the auto / interactive queue walker, the
/// ScraperService signal handlers, and the live-metadata panel renderer.
/// Three sibling helpers — owned by the host alongside this one — hold the
/// state and logic that used to sit on ScrapeResultDialog: the collection-tree
/// + items-list selection model is ScrapeResultSelectionModel, the recent-media
/// thumbnail decode/append is ScrapeResultThumbnailLoader, and the per-150-ms
/// value marquee is ValueMarqueeTicker. Methods here read / write the host's
/// remaining unified state (m_scraperCtx, m_service, m_unifiedPage,
/// m_unifiedPhase, the live-metadata QLineEdit/QTextBrowser
/// children, etc.) and host UI widgets via the friend-class privilege declared
/// on ScrapeResultDialog. The host's other surfaces (legacy single-item
/// candidate picker, BatchScrapeRunner progress view) remain on
/// ScrapeResultDialog directly.
///
/// Coupling: takes the host ScrapeResultDialog via the constructor so the
/// helper can reach the shared state members the host's slot trampolines also
/// read. The unified flow's own runtime state stays on the host; the
/// collection-selection, thumbnail, and marquee responsibilities were lifted
/// into the sibling helpers above so each owns the data its methods mutate.
/// This pulls the unified flow out of scraperesultdialog.cpp while keeping the
/// SingleItem / Batch flows anchored on ScrapeResultDialog.
class ScrapeResultDialogUnified : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScrapeResultDialogUnified)

public:
  explicit ScrapeResultDialogUnified(ScrapeResultDialog *dlg);
  ~ScrapeResultDialogUnified() override;

  // ── Public-API entry (forwarded from host) ─────────────────────────────
  void startUnifiedScrape(int preCollectionIndex = -1, const QString &preItemPath = QString());

  // ── Slot bodies (forwarded from host) ──────────────────────────────────
  // Each method here is the body of a host slot of the same name. The host
  // keeps the slot declaration (Qt's connect() requires a QObject-derived
  // receiver) and forwards to these.
  void onScrapeClicked();
  void showScrapeErrorDetails();
  /// Re-queue exactly the items that errored this run (grouped by owning
  /// collection), reusing the run's mode + media options (Kartend-jjjo5).
  void rescrapeFailedItems();
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

  // ── Non-slot helpers ───────────────────────────────────────────────────
  void buildUnifiedPanel();
  /// Clears the run-scoped state this controller owns (rate window,
  /// interactive item cursor, shown-collection tracker). Only called by the
  /// host's resetRunState() so the whole dialog family resets through one
  /// entry point. Session-scoped state (custom-field key union, last known
  /// quota reset time) is deliberately left intact — see the member docs.
  void resetRunState();
  void setUnifiedSetupEnabled(bool enabled);
  void updateUnifiedProgressLabel();
  void finishCurrentApply();
  void applyScrapedItemToLive(const Scraper::ScrapedItem &item);
  void populateCustomFields(const QHash<QString, QString> &fields);

  /// Derive the media-asset list for the Unified-Interactive Apply path
  /// (Kartend-xvci step 5). The unified flow has no per-item media
  /// checkbox panel — the user picked types via the setup-view media-
  /// type checkboxes, and Apply has to filter the candidate detail's
  /// `media` list against that selection. Pre-step-5 the host's onApply
  /// inlined this branch; the move keeps unified-only state
  /// (m_mediaTypeChecks) inside the unified class.
  [[nodiscard]] QList<Scraper::MediaAsset>
  selectInteractiveMediaForApply(const Scraper::ScrapedItem &detail) const;

private:
  /// Translates the user's media-type checkboxes (m_dlg->m_mediaTypeChecks)
  /// into the runner's lowercased filter set, splitting the synthetic
  /// "_metadata" entry off into @p writeMetadata. Used by onScrapeClicked.
  [[nodiscard]] QSet<QString> buildMediaFilter(bool &writeMetadata) const;

  // ── buildUnifiedPanel section builders (Kartend-etbol) ──────────────────
  // buildUnifiedPanel was a single 421-line method; these split it into one
  // helper per UI section, each returning the widget it built so the parent
  // assembles them into the page's root layout in the original order. Each
  // helper constructs the same widget tree and wires the same signals it did
  // inline — pure structural extraction, no behavior change.
  //
  // Collection tree (left) + items list (right), wrapped in the splitter that
  // is tracked as m_dlg->m_unifiedSplitterContainer.
  [[nodiscard]] QWidget *buildCollectionAndItemsPanel();
  // Media-type checkbox group (m_dlg->m_mediaTypesGroup, via
  // MediaTypeCheckboxBuilder) and the Auto / Interactive mode-radio row
  // (m_dlg->m_modeRowContainer). Returned via out-params because the two are
  // sibling widgets added to the root in sequence, not nested.
  void buildMediaTypesGroup(QGroupBox *&mediaTypesGroup, QWidget *&modeRowContainer);
  // The "Currently scraping" live-metadata group (m_dlg->m_liveMetadataGroup):
  // interactive candidate row, title/description, and the typed-field chip
  // flow. Hidden on return (shown only while a scrape is live).
  [[nodiscard]] QGroupBox *buildLiveMetadataPanel();
  // Recent-media thumbnail filmstrip (m_dlg->m_liveThumbsGroup) plus the
  // progress bar + current / timing / counts / quota status labels. Returned
  // via out-params; all are added to the root in order and start hidden.
  void buildProgressLabels(QGroupBox *&thumbsGroup, QWidget *&currentLabel, QWidget *&progressBar,
                           QWidget *&timingLabel, QWidget *&countsLabel, QWidget *&quotaLabel);

  // ── Live-view state (Kartend-4qx7m) ─────────────────────────────────────
  // The dialog drives a single ScraperService; the service owns the run
  // queue, progress counters, and failure list (read back through its
  // summary()/itemsCompleted()/etc.). Only two pieces of view-local state
  // remain here: the download-rate sampling window and the path of the item
  // the interactive picker is currently showing.

  /// Sliding window of (timestampMs, cumulativeBytes) samples used
  /// to compute a recent download rate for the Live view. Pruned to
  /// the last ~10 seconds of samples on every update so the rate
  /// readout reflects current activity, not the all-run average
  /// (which gets dragged down by long lookup-API idle stretches).
  QList<QPair<qint64, qint64>> m_rateSamples;

  /// The item path the service-driven interactive picker is currently on:
  /// set in onServicePickerNeeded, read by finishCurrentApply when the user
  /// applies. A single-element list — the service emits one pickerNeeded
  /// per item, so this is just "the item on screen right now".
  QStringList m_interactiveItems;

  /// Collection name currently shown in the host's m_unifiedCurrentLabel.
  /// Tracked so the itemBegan handler can refresh the label whenever the
  /// scrape moves to a new collection — without re-setting it per-item when
  /// batchItemConcurrency > 1 starts several items at once. Cleared by
  /// resetRunState so a new run's first itemBegan always refreshes the label
  /// even when it opens on the same collection the last run ended on.
  QString m_shownCollectionName;

  /// Local-time "HH:mm" the SS quota next resets at, captured from the most
  /// recent quotaUpdated signal. Reused by the quota-exhausted
  /// scrapeFinished message so it can name the reset time without
  /// re-deriving it. Empty until the first quota update; deliberately
  /// session-sticky (NOT part of resetRunState) — a rerun that dies on the
  /// still-exhausted quota before any quota update should keep naming the
  /// last known reset time.
  QString m_lastQuotaResetText;

  // ── Live-metadata custom-field cells (session-scoped) ───────────────────
  /// Union of every custom-field key seen across this scrape session.
  /// `populateCustomFields` adds new keys then renders the union — so the
  /// section always shows every possible field, with empty values for keys
  /// not present in the current scraped item.
  QSet<QString> m_allSeenCustomKeys;
  /// Persistent per-key QLineEdit cells inside the host's
  /// m_liveExtrasContainer. Created once (either at panel build time for the
  /// pre-seeded "known" keys, or lazily when a new key first appears) and
  /// reused across items — populateCustomFields just rewrites the .text on
  /// each cell, so the section's widget count + layout stay rock-stable
  /// instead of being torn down and rebuilt every item.
  QHash<QString, QLineEdit *> m_customFieldEdits;
  /// Number of fixed typed-field chips occupying the first N slots of
  /// m_liveExtrasContainer's FlowLayout. Custom-field chips are appended
  /// after these and torn down / rebuilt from index m_typedChipCount
  /// onward, leaving the typed chips untouched.
  int m_typedChipCount = 0;

  ScrapeResultDialog *m_dlg = nullptr;
};

#endif // SCRAPERESULTDIALOGUNIFIED_H
