#ifndef KARTEND_UI_DIALOGS_SCRAPER_RESULT_SINGLEITEMVIEW_H
#define KARTEND_UI_DIALOGS_SCRAPER_RESULT_SINGLEITEMVIEW_H

// Single-item candidate-picker chrome + state extracted from
// ScrapeResultDialog (Kartend-xvci steps 2 + 4). Owns the widget tree and
// the per-flow state (m_provider, m_candidates, m_detailCache, m_currentRow,
// m_currentDetail, m_mediaRows). The host dialog drives the view through
// setProviderAndCandidates() / setApplyEnabled() and reads back via
// currentDetail() / hasDetail() / mediaRows(); the view emits
// detailLoaded / detailFailed / detailLoading for the host to flip its
// Apply button. Download orchestration (dispatchSelectedDownloads,
// downloadedBytes accumulation, exec()-return result) still lives on the
// host — only the candidate / detail / media-checkbox surface is owned
// here.

#include <QHash>
#include <QList>
#include <QPair>
#include <QWidget>

#include "scrapertypes.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QListWidget;
class QStackedWidget;
class QTextBrowser;
QT_END_NAMESPACE

class MetadataLookupProvider;

class SingleItemScrapeView : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SingleItemScrapeView)
public:
  explicit SingleItemScrapeView(QWidget *parent = nullptr);

  /// Three states of the right-pane detail stack. Replaces the
  /// file-local STACK_EMPTY/LOADING/DETAIL integers the dialog used to
  /// pass directly to QStackedWidget::setCurrentIndex.
  enum class DetailStackPage { Empty, Loading, Detail };
  void setDetailPage(DetailStackPage page);

  /// Bind the view to a provider + candidate list. Called by the
  /// ScrapeResultDialog ctor for the legacy single-item flow and (in a
  /// follow-up) by the unified-interactive flow each time a new picker
  /// session starts. Resets m_currentRow / m_currentDetail /
  /// m_detailCache; populates the candidate list widget so the host's
  /// candidate-list hide-when-single-result heuristic still works on
  /// candidateList()->count().
  void setProviderAndCandidates(MetadataLookupProvider *provider,
                                QList<Scraper::ScrapeCandidate> candidates);

  /// Detail of the candidate currently rendered in the right pane.
  /// Empty (title.isEmpty()) until a candidate is selected and its
  /// detail successfully fetched. Host reads this in onApply to build
  /// the result payload.
  [[nodiscard]] const Scraper::ScrapedItem &currentDetail() const { return m_currentDetail; }
  [[nodiscard]] bool hasDetail() const { return !m_currentDetail.title.isEmpty(); }

  /// Drop every media row without rebuilding (used by the host's
  /// interactive / lookup-result paths when fresh candidates land
  /// before a detail is selected).
  void clearMediaRows();

  /// Per-asset (checkbox, asset) pairs created when the active detail's
  /// media list is rendered. Host dialog iterates these in onApply to
  /// figure out which assets to fetch.
  [[nodiscard]] const QList<QPair<QCheckBox *, Scraper::MediaAsset>> &mediaRows() const {
    return m_mediaRows;
  }

  /// The active metadata provider. Kept available to the host so its
  /// onApply / dispatchSelectedDownloads path can call fetchMediaBytes
  /// without a second provider plumb-through. Pre-step-4 the host
  /// owned m_provider directly; the accessor preserves the call sites
  /// while the data moves into the view.
  [[nodiscard]] MetadataLookupProvider *provider() const { return m_provider; }

  // ── Widget accessors (intermediate bridge — see header note) ────────
  [[nodiscard]] QListWidget *candidateList() const { return m_candidateList; }
  [[nodiscard]] QLabel *statusLabel() const { return m_statusLabel; }
  [[nodiscard]] QLabel *healthLabel() const { return m_healthLabel; }

signals:
  /// Detail fetch (cache hit or network round-trip) for the active
  /// candidate completed successfully. Host wires this to enable the
  /// Apply button (subject to the health-blocks-apply gate).
  void detailLoaded(const Scraper::ScrapedItem &item);

  /// Detail fetch returned an error or the active row went out of
  /// range. Host disables Apply.
  void detailFailed();

  /// Detail fetch is in flight (provider->fetchDetail was issued).
  /// Host disables Apply until detailLoaded / detailFailed lands.
  void detailLoading();

private slots:
  /// Internal slot wired to the candidate list's currentRowChanged.
  /// Looks up the detail from the cache (or kicks off
  /// provider->fetchDetail) and emits detail{Loaded,Failed,Loading}
  /// accordingly. Pre-step-4 this lived on ScrapeResultDialog with the
  /// host driving the cache + provider; the move pulls that state into
  /// the view (Kartend-xvci step 4).
  void onCandidateSelected(int row);

private:
  void populateMediaCheckboxes(const Scraper::ScrapedItem &item);

  QListWidget *m_candidateList = nullptr;
  QStackedWidget *m_detailStack = nullptr;
  QTextBrowser *m_detailText = nullptr;
  QListWidget *m_mediaList = nullptr;
  QLabel *m_healthLabel = nullptr;
  QLabel *m_statusLabel = nullptr;
  QList<QPair<QCheckBox *, Scraper::MediaAsset>> m_mediaRows;

  // Per-flow state migrated from ScrapeResultDialog in Kartend-xvci step 4.
  MetadataLookupProvider *m_provider = nullptr;
  QList<Scraper::ScrapeCandidate> m_candidates;
  QHash<int, Scraper::ScrapedItem> m_detailCache;
  int m_currentRow = -1;
  Scraper::ScrapedItem m_currentDetail;
};

#endif
