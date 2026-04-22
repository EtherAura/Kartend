#ifndef KARTEND_LOGGING_CATEGORIES_H
#define KARTEND_LOGGING_CATEGORIES_H

#include <QLoggingCategory>

// Cross-cutting diagnostic logging categories used by multiple modules.
// Per-module categories (lcScrollManager, lcArtworkManager, etc.) live next
// to their owning class.
//
// Default filter levels (set in loggingcategories.cpp):
//   kartend.perftrace  -> off (debug-only)
//   kartend.searchdiag -> off (debug-only)
//   kartend.scanflow   -> warning (always-on; used to trace scan/load flow)
//
// Enable at runtime with QT_LOGGING_RULES, e.g.:
//   QT_LOGGING_RULES="kartend.perftrace=true;kartend.searchdiag=true"
//
// Or via the legacy env vars (bridged in main.cpp):
//   KARTEND_PERF_TRACE=1, KARTEND_SEARCH_DIAG=1
Q_DECLARE_LOGGING_CATEGORY(lcPerfTrace)
Q_DECLARE_LOGGING_CATEGORY(lcSearchDiag)
Q_DECLARE_LOGGING_CATEGORY(lcScanFlow)

#endif // KARTEND_LOGGING_CATEGORIES_H
