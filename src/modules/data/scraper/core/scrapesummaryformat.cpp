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
    // Zero media, zero failures, but metadata landed: every asset list the
    // provider returned resolved to nothing under the requested types.
    // Without this line the run reads as a silent download problem.
    text += QLatin1Char('\n') +
            trc("The provider offered no media matching the selected artwork types.");
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
