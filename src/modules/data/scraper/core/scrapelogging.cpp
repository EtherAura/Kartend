#include "scrapelogging.h"

// Default level is QtWarningMsg — emits qCInfo / qCDebug only when the
// runtime filter explicitly enables them. Matches the prior per-TU
// definitions in httpclient.cpp / scraperesultdialog*.cpp before they
// collapsed into this single root.
Q_LOGGING_CATEGORY(lcScrapeTimings, "kartend.scrape.timings", QtWarningMsg)
