#ifndef KARTEND_SCRAPELOGGER_H
#define KARTEND_SCRAPELOGGER_H

#include <QString>

/// Optional scrape diagnostic logging, driven by the
/// `ScraperOptions::scrapeLogging` setting.
///
/// When enabled it raises every `kartend.scrape*` logging category
/// (scraper service, persistence, HTTP timings, result dialog) to
/// debug+info verbosity and tees their output to a size-capped file
/// under the app config directory. A GUI-launched build has no visible
/// stderr, so without the file a scrape that crashes mid-run leaves no
/// diagnosable record — `scrape.log` is that record (and survives the
/// crash because every line is flushed).
///
/// Warning/critical messages are never touched: they are on by default
/// and stay on whether logging is enabled or not.
namespace ScrapeLogger {

/// Turn scrape logging on or off. Idempotent — a call that matches the
/// current state is a cheap no-op, so it is safe to invoke on every
/// settings save. The first enable installs a chained Qt message
/// handler; the handler then stays installed (as a passthrough when
/// off) to avoid an install/uninstall race. Call from the main thread.
///
/// INVARIANT — ScrapeLogger must be the *outermost* Qt message handler.
/// The first enable captures whatever handler is active at that moment and
/// chains every message to it (so normal stderr logging keeps working),
/// then never uninstalls. Any other process-wide qInstallMessageHandler the
/// app relies on must therefore be installed *before* ScrapeLogger's first
/// enable, so ScrapeLogger chains down to it. A global handler installed
/// *after* sits on top of ScrapeLogger: if it chains to its predecessor
/// scrape logging still runs, but if it replaces without chaining it
/// silently shadows both the scrape file capture and the stderr passthrough
/// ScrapeLogger owns.
void setEnabled(bool enabled);

/// True when scrape logging is currently active.
[[nodiscard]] bool isEnabled();

/// Absolute path of the scrape log file (`scrape.log` in the app
/// config directory). Valid whether or not logging is enabled; the
/// rotated previous generation lives alongside it as `scrape.log.old`.
[[nodiscard]] QString logFilePath();

} // namespace ScrapeLogger

#endif // KARTEND_SCRAPELOGGER_H
