#ifndef SCRAPERESULTDIALOGUNIFIED_H
#define SCRAPERESULTDIALOGUNIFIED_H

#include "errorutils.h"
#include "scraperservice.h"
#include "scrapertypes.h"

#include <functional>
#include <memory>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QGroupBox;
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
/// m_unifiedQueue, m_unifiedPhase, the live-metadata QLineEdit/QTextBrowser
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
  void setUnifiedSetupEnabled(bool enabled);
  void updateUnifiedProgressLabel();
  void startNextCollectionInQueue();
  void runAutoCollection(int collectionIndex, const QStringList &items);
  void runInteractiveCollection(int collectionIndex, const QStringList &items);
  void interactiveNextItem();
  void interactiveOnLookupResult(ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result);
  void interactiveOnApplied();
  void interactiveOnSkipped();
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

  /// Narrow accessor for the host (Kartend-unlta). The dialog's Cancel
  /// handler flips this flag for the legacy in-dialog orchestration path so
  /// the next queue hop stops; the queue-walking logic that reads it lives
  /// here, so the flag itself now lives here too. The host no longer touches
  /// the member directly.
  void requestUnifiedCancel() { m_unifiedCancelled = true; }

private:
  /// Translates the user's media-type checkboxes (m_dlg->m_mediaTypeChecks)
  /// into the runner's lowercased filter set, splitting the synthetic
  /// "_metadata" entry off into @p writeMetadata. Shared by onScrapeClicked
  /// + runAutoCollection.
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
  void buildProgressLabels(QGroupBox *&thumbsGroup, QWidget *&currentLabel,
                           QWidget *&progressBar, QWidget *&timingLabel, QWidget *&countsLabel,
                           QWidget *&quotaLabel);

  // ── Unified-flow orchestration state (Kartend-unlta) ────────────────────
  // Relocated off ScrapeResultDialog's header onto this controller, which
  // already owns the queue walker + interactive driver that mutate it. The
  // host used to store this state and let this class reach in via friendship;
  // now the state lives where its logic does. The host reads none of it
  // directly except the cancel flag, exposed via requestUnifiedCancel().

  /// Snapshot at scrape time: queue of (collectionIndex, items) tuples
  /// to process in sequence. AutoRunning + Interactive both walk this.
  struct CollectionJob {
    int collectionIndex;
    QString collectionName;
    QStringList items;
  };
  QList<CollectionJob> m_unifiedQueue;
  int m_unifiedQueueCursor = 0;
  qint64 m_unifiedStartMs = 0;
  /// Sliding window of (timestampMs, cumulativeBytes) samples used
  /// to compute a recent download rate for the Live view. Pruned to
  /// the last ~10 seconds of samples on every update so the rate
  /// readout reflects current activity, not the all-run average
  /// (which gets dragged down by long lookup-API idle stretches).
  QList<QPair<qint64, qint64>> m_rateSamples;
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

  ScrapeResultDialog *m_dlg = nullptr;
};

#endif // SCRAPERESULTDIALOGUNIFIED_H
