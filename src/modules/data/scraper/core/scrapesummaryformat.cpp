#include "scrapesummaryformat.h"

#include <QCoreApplication>

namespace Scraper::SummaryFormat {

namespace {
// Kept under the ScraperController context: these strings were born in the
// controller's completion box and existing translations key off it.
QString trc(const char *text) {
  return QCoreApplication::translate("ScraperController", text);
}
} // namespace

QString completionText(const ScraperService::Summary &summary) {
  QString text = trc("Scrape complete.") + QStringLiteral("\n\n") +
                 trc("Scraped: %1\nSkipped: %2\nNot found: %3\nErrors: %4\nMedia written: %5")
                     .arg(summary.scraped)
                     .arg(summary.skipped)
                     .arg(summary.notFound)
                     .arg(summary.errors)
                     .arg(summary.mediaWritten);
  if (summary.mediaFetchFailures > 0 || summary.mediaWriteFailures > 0) {
    text += QLatin1Char('\n') + trc("Media failures: %1 fetch, %2 write")
                                    .arg(summary.mediaFetchFailures)
                                    .arg(summary.mediaWriteFailures);
  } else if (summary.scraped > 0 && summary.mediaWritten == 0) {
    // Zero media written and zero failures has TWO causes, and this used to
    // assert the first one unconditionally — so a re-scrape of an unchanged
    // collection reported "the provider offered no media" while 28 platform
    // files sat on disk (observed in the guest 2026-08-31). mediaUpToDate is
    // what separates them.
    if (summary.mediaUpToDate > 0) {
      // Media WAS returned and fetched; every file already on disk satisfied
      // the rescrape policy, so nothing needed rewriting. A success, and the
      // one case where "0 media" is the expected outcome.
      text += QLatin1Char('\n') +
              trc("All %1 artwork file(s) were already up to date.").arg(summary.mediaUpToDate);
    } else {
      // Nothing was written AND nothing was skipped — the asset lists really
      // did resolve to nothing under the requested types. Without this line
      // the run reads as a silent download problem.
      text += QLatin1Char('\n') +
              trc("The provider offered no media matching the selected artwork types.");
    }
  }
  if (summary.sidecarFailures > 0) {
    text +=
        QLatin1Char('\n') + trc("Metadata sidecar write failures: %1").arg(summary.sidecarFailures);
  }
  if (!summary.firstFailures.isEmpty()) {
    text += QStringLiteral("\n\n") +
            trc("First failures:\n%1").arg(summary.firstFailures.join(QChar('\n')));
  }
  return text;
}

} // namespace Scraper::SummaryFormat
