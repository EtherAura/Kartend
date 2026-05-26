#ifndef SCRAPER_SCRAPELOGGING_H
#define SCRAPER_SCRAPELOGGING_H

#include <QLoggingCategory>

// Shared logging categories for the scraper pipeline.
//
// Multiple translation units share these category names (network HTTP
// client, the scrape result dialog's unified + single flows, the persistence
// layer). Keeping one Q_LOGGING_CATEGORY definition per TU works because
// Qt dedupes by name, but it scatters the source of truth and makes
// runtime filter rules harder to reason about. Declaring once here and
// defining once in scrapelogging.cpp keeps the registration single-rooted.
//
// Filter at runtime via QT_LOGGING_RULES, e.g.:
//   QT_LOGGING_RULES="kartend.scrape.timings=true"
//
// Per-module categories that don't cross TU boundaries (lcScraperHttp,
// lcScraperService, lcScrape, lcScrapeWriteWorker) stay next to their
// owning class and are not declared here.
Q_DECLARE_LOGGING_CATEGORY(lcScrapeTimings)

#endif // SCRAPER_SCRAPELOGGING_H
