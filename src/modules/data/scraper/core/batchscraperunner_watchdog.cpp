// Sibling TU: per-item step watchdogs + error/rate-limit accounting for
// BatchScrapeRunner. Lives here: stepWatchdogMs / StepWatchdog::finish /
// armStepWatchdog / onStepTimedOut (the stall guards for the unbounded local
// I/O legs), noteRateLimited429 (consecutive-429 escalation), and both
// recordError overloads (per-item failure accounting, quota stop, and the
// fatal-error circuit breaker). The core queue pump and lookup/detail chain
// stay in batchscraperunner.cpp.
#include "batchscraperunner.h"

#include <utility>

#include <QFileInfo>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

namespace Scraper {

// Defined in batchscraperunner.cpp (same pattern as cachemanager.cpp /
// cachemanagerdisk.cpp share lcCacheManager).
Q_DECLARE_LOGGING_CATEGORY(lcBatchScrape)

int BatchScrapeRunner::stepWatchdogMs() {
  // Read fresh (a getenv + parse, ~2x per item — negligible) rather than
  // cached, so a test can drive the budget to a few milliseconds per case and
  // a user can retune it without a restart. 10min default; env override lets a
  // user with multi-GB images on a slow mount raise it if a legitimate hash
  // ever approaches the ceiling (Kartend audit xnm8a).
  bool ok = false;
  const int v = qEnvironmentVariableIntValue("KARTEND_SCRAPE_STEP_TIMEOUT_MS", &ok);
  return (ok && v > 0) ? v : 600000;
}

void BatchScrapeRunner::StepWatchdog::finish() {
  if (done) {
    *done = true;
  }
  if (timer) {
    timer->stop();
    timer->deleteLater();
    timer = nullptr;
  }
}

BatchScrapeRunner::StepWatchdog
BatchScrapeRunner::armStepWatchdog(const std::shared_ptr<ItemState> &state,
                                   const QString &stageLabel, std::function<void()> onTimeout) {
  auto done = std::make_shared<bool>(false);
  // Parented to `this`, so a runner torn down before the timer fires destroys
  // it cleanly (it never fires post-dtor — same guarantee as the media-write
  // watchers). On a normal completion the step's callback calls the handle's
  // finish(), which sets *done and stops + deleteLater()s the timer right
  // away — previously only *done was set and every timer ran out its full
  // budget (10min default) before self-deleting, retaining two live QTimers
  // plus their captured ItemStates per item across a long batch.
  auto *timer = new QTimer(this);
  timer->setSingleShot(true);
  QPointer<BatchScrapeRunner> self(this);
  connect(timer, &QTimer::timeout, this,
          [self, timer, done, state, stageLabel, onTimeout = std::move(onTimeout)]() {
            timer->deleteLater();
            if (self.isNull() || *done) return; // step already completed normally
            *done = true;
            if (onTimeout) {
              onTimeout();
            }
            self->onStepTimedOut(state, stageLabel);
          });
  timer->start(stepWatchdogMs());
  return StepWatchdog{done, timer};
}

void BatchScrapeRunner::onStepTimedOut(const std::shared_ptr<ItemState> &state,
                                       const QString &stageLabel) {
  // An unbounded local step (provider ROM hash-read / artwork+sidecar write /
  // ScrapeWriteWorker DB save) didn't finish within the watchdog budget —
  // almost always a slow or wedged storage mount. The blocked syscall can't
  // be interrupted, so the worker thread / QtConcurrent future is left to
  // drain on its own (value-captures only, like the destructor's abandon
  // path) while we free this item's slot and let the batch advance instead
  // of freezing forever (Kartend audit xnm8a).
  if (m_cancelled) {
    itemFinished();
    return;
  }
  qCWarning(lcBatchScrape) << "BatchScrapeRunner:" << stageLabel << "for"
                           << QFileInfo(state->path).fileName() << "exceeded" << stepWatchdogMs()
                           << "ms; erroring the item and advancing (storage may be unresponsive)";
  recordError(QStringLiteral("%1: %2 timed out (storage unresponsive?)")
                  .arg(QFileInfo(state->path).fileName(), stageLabel),
              state->path);
}

void BatchScrapeRunner::noteRateLimited429() {
  ++m_consecutive429Count;
  if (m_consecutive429Count < kConsecutive429StopThreshold || m_quotaStopped) return;
  // The limiter answered 429 to every recent request — it isn't a burst
  // we can ride out. Stop new dispatch via the quota-stop machinery so the
  // un-run work persists as the resume point (Kartend-jjyst.3).
  m_summary.quotaExhausted = true;
  m_quotaStopped = true;
  qCWarning(lcBatchScrape) << "BatchScrapeRunner:" << m_consecutive429Count
                           << "consecutive HTTP 429 rate-limit responses — stopping dispatch; "
                              "un-dispatched items stay queued for resume";
  if (m_summary.firstFailures.size() < kMaxReportedFailures) {
    m_summary.firstFailures.append(
        QStringLiteral("Stopped after %1 consecutive HTTP 429 rate-limit responses — the "
                       "provider is throttling this run; remaining items were left queued "
                       "for resume")
            .arg(m_consecutive429Count));
  }
}

void BatchScrapeRunner::recordError(const QString &reason, const QString &failedPath) {
  ++m_summary.errors;
  if (m_summary.firstFailures.size() < kMaxReportedFailures) {
    m_summary.firstFailures.append(reason);
  }
  // Kartend-jjjo5: retain the full path of every errored item so the dialog can
  // offer "re-scrape failed". Not-found / skipped items never reach here, so
  // they're excluded by construction. Bounded like firstFailures.
  if (!failedPath.isEmpty() && m_summary.failedPaths.size() < kMaxReportedFailures) {
    m_summary.failedPaths.append(failedPath);
  }
  // A genuine error is a terminal verdict (re-runnable via "re-scrape
  // failed"), so it leaves the resume list. Once the quota stop has flipped,
  // errors are kept instead: the quota-erroring item itself and anything
  // failing in its wake never really got a shot, and the persisted resume
  // point should retry them after the quota resets.
  if (!failedPath.isEmpty() && !m_quotaStopped) {
    m_remainingPaths.removeOne(failedPath);
  }
  itemFinished();
}

void BatchScrapeRunner::recordError(const QString &itemPath, const ErrorUtils::ErrorContext &err) {
  const QString itemName = QFileInfo(itemPath).fileName();
  // Kartend-e8aag: a provider "not found" (the remote DB genuinely has no
  // entry — an HTTP 404, or a miss a provider tagged RemoteResourceNotFound)
  // is a routine outcome, not a failure. Count it apart from errors and keep
  // it out of the failure list (and out of failedPaths — not-found items won't
  // succeed on retry against the same provider).
  if (err.code == ErrorUtils::ErrorCode::RemoteResourceNotFound || err.httpStatus == 404) {
    ++m_summary.notFound;
    // The provider answered normally — a healthy outcome for breaker purposes.
    resetFatalStreak();
    // Terminal like the empty-candidates branch: a not-found won't succeed on
    // retry, so it leaves the resume list.
    m_remainingPaths.removeOne(itemPath);
    itemFinished();
    return;
  }
  // Kartend-oa1ry: a quota-exhaustion response — as classified by the
  // provider that made the request (isQuotaExhausted; the base default covers
  // 429 plus ScreenScraper's non-standard 430/431) — means every remaining
  // item would just burn against an exhausted quota. Flag it so pump() stops
  // dispatching new items; in-flight items still finish (they're not gated on
  // m_quotaStopped).
  if (m_provider && m_provider->isQuotaExhausted(err)) {
    m_summary.quotaExhausted = true;
    m_quotaStopped = true;
  } else if (err.httpStatus == 429) {
    // A 429 landing here means the provider's transient retry either waited
    // out a Retry-After hint and still got throttled, or had no hint to wait
    // on. It's burst throttling, not daily-quota exhaustion — a single one no
    // longer halts the whole multi-collection run (Kartend-jjyst.3); the
    // queue stops only after kConsecutive429StopThreshold in a row. A 429 is
    // also a differently-shaped error for the fatal breaker below, so its
    // streak resets here — without resetting the 429 streak itself (which
    // resetFatalStreak() would).
    m_consecutiveFatalCount = 0;
    m_lastFatalStatus = 0;
    noteRateLimited429();
  } else if (err.httpStatus == 401 || err.httpStatus == 403 || err.httpStatus == 423 ||
             err.httpStatus == 426) {
    // A non-429 status breaks any consecutive-429 run (see resetFatalStreak;
    // this branch tracks its own streak instead of calling it).
    m_consecutive429Count = 0;
    // Circuit breaker (see the member doc): persistent auth/infra failures
    // fail every request identically — stop dispatch after N consecutive
    // identical statuses instead of firing one doomed request per item (each
    // failed lookup also deepens ScreenScraper's failed-lookup ban). Shares
    // the quota-stop machinery, so the un-dispatched work stays queued as
    // the persisted resume point for after the user fixes the cause.
    m_consecutiveFatalCount =
        (err.httpStatus == m_lastFatalStatus) ? m_consecutiveFatalCount + 1 : 1;
    m_lastFatalStatus = err.httpStatus;
    if (m_consecutiveFatalCount >= kFatalErrorBreakerThreshold && !m_quotaStopped) {
      m_summary.quotaExhausted = true;
      m_quotaStopped = true;
      qCWarning(lcBatchScrape) << "BatchScrapeRunner:" << m_consecutiveFatalCount
                               << "consecutive HTTP" << err.httpStatus
                               << "failures — stopping dispatch (check provider credentials / "
                                  "status); un-dispatched items stay queued for resume";
      if (m_summary.firstFailures.size() < kMaxReportedFailures) {
        m_summary.firstFailures.append(
            QStringLiteral("Stopped after %1 consecutive HTTP %2 failures — check provider "
                           "credentials / status; remaining items were left queued for resume")
                .arg(m_consecutiveFatalCount)
                .arg(err.httpStatus));
      }
    }
  } else {
    // A differently-shaped error breaks the "identical fatal" streak.
    resetFatalStreak();
  }
  // Kartend-e6oyu: record the enriched one-line summary (status + a server
  // detail snippet) rather than the bare err.message, so the failure list is
  // diagnosable instead of a wall of "HTTP request failed".
  // Kartend-jjjo5: pass the full path so the errored item can be re-queued.
  recordError(QStringLiteral("%1: %2").arg(itemName, err.userFacingSummary()), itemPath);
}

} // namespace Scraper
