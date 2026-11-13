// Single owner of the Qt API logging-rule set — see loggingrules.h.
#include "loggingrules.h"

#include <QByteArray>
#include <QLatin1Char>
#include <QLoggingCategory>
#include <QMutex>
#include <QStringList>
#include <QtGlobal>

namespace {

// Both are only written from the main thread (main() at startup,
// ScrapeLogger::setEnabled on settings load/save); the mutex just keeps the
// read-compose-apply step atomic should that ever change.
QMutex g_rulesMutex;
QString g_startupRules;
QString g_scrapeRules;

// Caller must hold g_rulesMutex. Startup rules first, scrape rules last, so
// the scrape toggle keeps the final say over its own kartend.scrape*
// categories exactly as when it replaced the whole rule set.
void applyLocked() {
  QStringList parts;
  if (!g_startupRules.isEmpty()) parts << g_startupRules;
  if (!g_scrapeRules.isEmpty()) parts << g_scrapeRules;
  QLoggingCategory::setFilterRules(parts.join(QLatin1Char('\n')));
}

} // namespace

namespace LoggingRules {

void applyStartupRules() {
  // Optional runtime logging configuration.
  // If set, this overrides Qt's default filtering rules.
  // Example: KARTEND_LOG_RULES="kartend.*=true" ./kartend
  QString rules;
  const QByteArray logRules = qgetenv("KARTEND_LOG_RULES");
  if (!logRules.isEmpty()) {
    rules = QString::fromUtf8(logRules);
  }

  // Bridge legacy diagnostic env vars to logging-category rules so existing
  // KARTEND_PERF_TRACE=1 / KARTEND_SEARCH_DIAG=1 / KARTEND_SCAN_DIAG=1
  // invocations continue to work after diag prints were converted from
  // raw Qt logging macros to qCDebug(<category>).
  QStringList bridgedRules;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    bridgedRules << QStringLiteral("kartend.perftrace.debug=true");
  }
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    bridgedRules << QStringLiteral("kartend.searchdiag.debug=true");
  }
  if (qEnvironmentVariableIsSet("KARTEND_SCAN_DIAG")) {
    bridgedRules << QStringLiteral("kartend.scanflow.debug=true");
  }
  if (!bridgedRules.isEmpty()) {
    // Same effect as the two consecutive setFilterRules calls main() used
    // to make: the bridged set replaces KARTEND_LOG_RULES when both are set.
    rules = bridgedRules.join(QLatin1Char('\n'));
  }

  QMutexLocker lock(&g_rulesMutex);
  g_startupRules = rules;
  applyLocked();
}

void setScrapeRules(const QString &rules) {
  QMutexLocker lock(&g_rulesMutex);
  g_scrapeRules = rules;
  applyLocked();
}

} // namespace LoggingRules
