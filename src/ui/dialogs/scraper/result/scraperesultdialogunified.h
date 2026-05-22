#ifndef SCRAPERESULTDIALOGUNIFIED_H
#define SCRAPERESULTDIALOGUNIFIED_H

#include "errorutils.h"
#include "scraperservice.h"
#include "scrapertypes.h"

#include <functional>
#include <memory>
#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QListWidgetItem;
class QTreeWidgetItem;
QT_END_NAMESPACE

class MetadataLookupProvider;
class ScrapeResultDialog;

/// Owns the unified-flow behaviour previously embedded in ScrapeResultDialog:
/// collection-tree population, per-collection item lists, the auto / interactive
/// queue walker, the ScraperService signal handlers, the live-metadata panel
/// renderer, and the per-150-ms marquee timer. Methods here read / write host
/// state (m_scraperCtx, m_service, m_unifiedPage, m_collectionTree,
/// m_unifiedItemsList, m_unifiedQueue, m_unifiedPhase, the live-metadata
/// QLineEdit/QTextBrowser children, etc.) and host UI widgets via the
/// friend-class privilege declared on ScrapeResultDialog. The host's other
/// surfaces (legacy single-item candidate picker, BatchScrapeRunner progress
/// view) remain on ScrapeResultDialog directly.
///
/// Coupling: takes the host ScrapeResultDialog via the constructor so the
/// helper can reach the shared state members the host's slot trampolines also
/// read. State is intentionally NOT moved off the host — it stays where the
/// existing access sites already reference it (host slots, setScraperService's
/// connect table, the constructor's UI builder). This class is a method-
/// organising extraction (Kartend-izpz, the 3fkz step-3 split) that pulls the
/// ~1100 LOC of unified flow out of the 2566-LOC scraperesultdialog.cpp while
/// keeping the SingleItem / Batch flows anchored on ScrapeResultDialog.
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
  void onCollectionTreeCurrentChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
  void onCollectionCheckChanged(QTreeWidgetItem *item, int column);
  void onItemCheckChanged(QListWidgetItem *item);
  void onScrapeClicked();
  void showScrapeErrorDetails();
  void onServiceScrapeStarted(int total);
  void onServiceItemBegan(int done, int total, const QString &collectionName, const QString &name);
  void onServiceItemCompleted(int done, int total, const Scraper::ScrapedItem &scraped,
                              const QStringList &mediaPaths);
  void onServicePickerNeeded(const QString &itemPath, const QString &itemName,
                             const QList<Scraper::ScrapeCandidate> &candidates,
                             std::shared_ptr<MetadataLookupProvider> provider,
                             const QString &artworkDir);
  void onServiceScrapeFinished(const Scraper::ScraperService::Summary &s);
  void onServiceScrapePaused();
  void onServiceQuotaUpdated(const Scraper::QuotaStatus &quota);

  // ── Non-slot helpers ───────────────────────────────────────────────────
  void buildUnifiedPanel();
  void populateCollectionTree();
  void rebuildItemsList(int collectionIndex);
  void applyCollectionCheckState(int collectionIndex, bool checked);
  void setUnifiedSetupEnabled(bool enabled);
  void updateUnifiedProgressLabel();
  [[nodiscard]] int totalCheckedItemCount() const;
  void startNextCollectionInQueue();
  void runAutoCollection(int collectionIndex, const QStringList &items);
  void runInteractiveCollection(int collectionIndex, const QStringList &items);
  void interactiveNextItem();
  void interactiveOnLookupResult(ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result);
  void interactiveOnApplied();
  void interactiveOnSkipped();
  void finishCurrentApply();
  void applyScrapedItemToLive(const Scraper::ScrapedItem &item);
  void appendThumbAsync(const QString &path);
  void tickValueMarquees();
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
  ScrapeResultDialog *m_dlg = nullptr;
};

#endif // SCRAPERESULTDIALOGUNIFIED_H
