#ifndef SCRAPESUMMARYFORMAT_H
#define SCRAPESUMMARYFORMAT_H

#include <QString>

#include "scraperservice.h"

/// User-facing text for the post-run completion box, built from the run's
/// final ScraperService::Summary — the single authoritative source, so the
/// popup can never disagree with the live counts label. Pure so the media
/// accounting rules are unit-testable without a dialog or controller.
namespace Scraper::SummaryFormat {

/// The completion-box body. Beyond the item counts it always names the
/// media outcome: a run that scraped metadata but wrote ZERO media used to
/// read "0 media" with no reason anywhere — the fetch/write failure
/// counters existed on the summary but were surfaced nowhere, and the
/// "provider offered nothing matching the selected artwork types" outcome
/// (zero resolved assets, zero failures) was indistinguishable from a
/// download problem.
[[nodiscard]] QString completionText(const ScraperService::Summary &summary);

} // namespace Scraper::SummaryFormat

#endif // SCRAPESUMMARYFORMAT_H
