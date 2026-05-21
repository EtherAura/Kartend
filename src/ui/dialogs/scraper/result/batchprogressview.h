#ifndef KARTEND_UI_DIALOGS_SCRAPER_RESULT_BATCHPROGRESSVIEW_H
#define KARTEND_UI_DIALOGS_SCRAPER_RESULT_BATCHPROGRESSVIEW_H

// Batch-mode progress view extracted from ScrapeResultDialog (Kartend-xvci).
// The dialog used to glue together SingleItem, BatchProgress and Unified
// modes in a single 2778 LOC class with one m_modeStack; this widget peels
// the BatchProgress page out so the dialog can host it as one of three
// composable view-children. The view owns the batch UI (header, current-
// item label, progress bar, timing, counts) and its own per-run timing /
// totals state. It observes a caller-owned BatchScrapeRunner and emits
// `finished` so the host dialog can accept() exec().

#include <QWidget>

#include "batchscraperunner.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QProgressBar;
QT_END_NAMESPACE

/// Progress-only view for a BatchScrapeRunner. Shows a header
/// ("Batch scraping '<collection>'…"), the current item being scraped,
/// a progress bar, an elapsed/ETA timing line, and a scraped/skipped/
/// errors counts line. Bound to a runner via setRunner; the runner is
/// non-owning — caller controls its lifetime (typically lives past the
/// dialog's exec() so the runner can finish draining in-flight callbacks
/// after a Cancel).
class BatchScrapeProgressView : public QWidget {
  Q_OBJECT
public:
  explicit BatchScrapeProgressView(QWidget *parent = nullptr);

  /// Bind the view to `runner`. The view paints the preparing state,
  /// observes the runner's progress + finished signals, and emits its
  /// own `finished` signal forwarding the runner's Summary. Caller is
  /// responsible for runner->start() afterwards; the view only observes.
  ///
  /// Pass nullptr to leave the view in its current state without
  /// rewiring (used by the dialog's unified-auto path which observes
  /// its own runner separately while still wanting to display the
  /// view's chrome).
  void setRunner(Scraper::BatchScrapeRunner *runner, const QString &collectionName, int totalItems);

  /// Summary captured from the bound runner's `finished` signal. Only
  /// meaningful after `finished` has been emitted.
  [[nodiscard]] Scraper::BatchScrapeRunner::Summary summary() const { return m_summary; }

signals:
  /// Emitted when the bound runner emits `finished`. Carries the same
  /// Summary the runner reported.
  void finished(Scraper::BatchScrapeRunner::Summary summary);

private:
  void onProgress(int done, int total, const QString &currentName);

  QLabel *m_headerLabel = nullptr;
  QLabel *m_currentLabel = nullptr;
  QProgressBar *m_progressBar = nullptr;
  QLabel *m_timingLabel = nullptr;
  QLabel *m_countsLabel = nullptr;

  Scraper::BatchScrapeRunner *m_runner = nullptr;
  Scraper::BatchScrapeRunner::Summary m_summary;
  qint64 m_startMs = 0;
  int m_totalItems = 0;
};

#endif
