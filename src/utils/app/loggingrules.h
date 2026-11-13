#ifndef KARTEND_LOGGINGRULES_H
#define KARTEND_LOGGINGRULES_H

#include <QString>

/// Single owner of Qt's API logging-rule set (QLoggingCategory::setFilterRules).
///
/// Qt keeps exactly ONE API rule set, so every setFilterRules call replaces
/// the previous one wholesale. Kartend has two independent producers — the
/// startup diagnostic rules derived from the environment and the scrape
/// logging toggle — so both go through here and the applied set is always
/// "startup rules, then scrape rules" (later lines win in Qt). Rules from
/// QT_LOGGING_RULES are still evaluated after the API set by Qt itself, so
/// they keep overriding both (Kartend-37t5r).
///
/// Call from the main thread only.
namespace LoggingRules {

/// Builds the startup diagnostic rules from KARTEND_LOG_RULES and the legacy
/// KARTEND_PERF_TRACE / KARTEND_SEARCH_DIAG / KARTEND_SCAN_DIAG bridges,
/// remembers them and re-applies the combined rule set.
void applyStartupRules();

/// Replaces the scrape-logging rules (appended after the startup rules) and
/// re-applies the combined rule set.
void setScrapeRules(const QString &rules);

} // namespace LoggingRules

#endif // KARTEND_LOGGINGRULES_H
