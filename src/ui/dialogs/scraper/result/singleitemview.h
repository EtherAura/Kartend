#ifndef KARTEND_UI_DIALOGS_SCRAPER_RESULT_SINGLEITEMVIEW_H
#define KARTEND_UI_DIALOGS_SCRAPER_RESULT_SINGLEITEMVIEW_H

// Single-item candidate-picker chrome extracted from ScrapeResultDialog
// (Kartend-xvci step 2). Owns the widget tree that the legacy single-item
// flow (right-click → Scrape) drives and that the unified-interactive
// flow reuses per-item. State (m_provider, m_candidates, m_detailCache,
// m_currentRow, m_mediaRows, etc.) and methods (onCandidateSelected,
// populateMediaCheckboxes, onApply, downloadNextSelectedMedia,
// updateSingleItemProgress) still live on the host dialog and reach in
// via the accessors below — they're slated to land in this view in a
// follow-up step. The accessor surface is a temporary intermediate
// bridge, not the end-state API.

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

class SingleItemScrapeView : public QWidget {
  Q_OBJECT
public:
  explicit SingleItemScrapeView(QWidget *parent = nullptr);

  /// Three states of the right-pane detail stack. Replaces the
  /// file-local STACK_EMPTY/LOADING/DETAIL integers the dialog used to
  /// pass directly to QStackedWidget::setCurrentIndex.
  enum class DetailStackPage { Empty, Loading, Detail };
  void setDetailPage(DetailStackPage page);

  /// Populate the right-pane media-checkbox panel from `item`. Each
  /// asset becomes a labelled QCheckBox pre-checked on (the legacy
  /// behaviour — user unchecks what they don't want). Clears any
  /// previous rows. The host dialog's Apply path walks
  /// `mediaRows()` to derive the per-asset download queue.
  void populateMediaCheckboxes(const Scraper::ScrapedItem &item);

  /// Drop every media row without rebuilding (used by the host's
  /// interactive / lookup-result paths when fresh candidates land
  /// before a detail is selected).
  void clearMediaRows();

  /// Per-asset (checkbox, asset) pairs created by
  /// `populateMediaCheckboxes`. Host dialog iterates these in onApply
  /// to figure out which assets to fetch.
  [[nodiscard]] const QList<QPair<QCheckBox *, Scraper::MediaAsset>> &mediaRows() const {
    return m_mediaRows;
  }

  // ── Widget accessors (intermediate bridge — see header note) ────────
  [[nodiscard]] QListWidget *candidateList() const { return m_candidateList; }
  [[nodiscard]] QTextBrowser *detailText() const { return m_detailText; }
  [[nodiscard]] QListWidget *mediaList() const { return m_mediaList; }
  [[nodiscard]] QLabel *statusLabel() const { return m_statusLabel; }
  [[nodiscard]] QLabel *healthLabel() const { return m_healthLabel; }

signals:
  /// Forwards the candidate list's currentRowChanged signal so the
  /// host can wire its onCandidateSelected slot without reaching for
  /// the inner widget.
  void candidateRowChanged(int row);

private:
  QListWidget *m_candidateList = nullptr;
  QStackedWidget *m_detailStack = nullptr;
  QTextBrowser *m_detailText = nullptr;
  QListWidget *m_mediaList = nullptr;
  QLabel *m_healthLabel = nullptr;
  QLabel *m_statusLabel = nullptr;
  QList<QPair<QCheckBox *, Scraper::MediaAsset>> m_mediaRows;
};

#endif
