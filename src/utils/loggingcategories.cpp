#include "loggingcategories.h"

// Default category levels:
// - perftrace and searchdiag are diagnostic; off by default.
// - scanflow remains warning-level so existing operator-visible flow traces
//   continue to surface in release logs.
Q_LOGGING_CATEGORY(lcPerfTrace, "kartend.perftrace", QtWarningMsg)
Q_LOGGING_CATEGORY(lcSearchDiag, "kartend.searchdiag", QtWarningMsg)
Q_LOGGING_CATEGORY(lcScanFlow, "kartend.scanflow", QtDebugMsg)
// lcErrors is defined inline in errorutils.h (Kartend-v3v).
