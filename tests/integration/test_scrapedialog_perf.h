#ifndef TEST_SCRAPEDIALOG_PERF_H
#define TEST_SCRAPEDIALOG_PERF_H

#include <QObject>

// Performance-regression guard for the scrape-result candidate/detail
// population path (Kartend-jkei). The prior investigation measured only the
// ScrapeResultDialogUnified constructor with candidates=0 (8.3ms) — but the
// candidate list arrives later through SingleItemScrapeView::
// setProviderAndCandidates, and the per-candidate detail + media-checkbox
// render runs from that path. This test drives the populated path with a large
// synthetic result set (the bd's "100+ items x 50 media assets") behind a
// synchronous fake provider, so a future O(n^2) or per-asset stall in the
// arrival path is caught instead of shipping as a field freeze.
class TestScrapeDialogPerf : public QObject {
  Q_OBJECT
private slots:
  void largeCandidateListPopulatesWithinBudget();
};

#endif // TEST_SCRAPEDIALOG_PERF_H
