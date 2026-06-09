#ifndef SCRAPERRETRYPOLICY_H
#define SCRAPERRETRYPOLICY_H

#include "errorutils.h"

/// Kartend-1rtrt: pure, network-free policy for bounded retry of transient
/// scraper failures. Separated from the provider so the classification and
/// backoff math are unit-testable without a live HTTP transport — the
/// timer-driven re-dispatch that consumes them lives in the provider.
namespace Scraper::RetryPolicy {

/// Default bound on retry *attempts after the first* (so total tries =
/// kDefaultMaxRetries + 1). Kept small: SS quota counters tick per request,
/// so we trade at most a couple of extra requests for recovering an item that
/// a single transient 5xx/timeout would otherwise drop permanently.
inline constexpr int kDefaultMaxRetries = 2;
inline constexpr int kDefaultBaseDelayMs = 500;
inline constexpr int kDefaultMaxDelayMs = 30000;

/// True when @p err is the kind of failure a retry can plausibly recover:
///   - a transfer timeout (surfaced as OperationCancelled with no HTTP status),
///   - a server-side 5xx, or
///   - SS's 423 "infrastructure down".
/// Everything else (4xx auth/quota/not-found, malformed request, parse
/// failures) is permanent for this request and must not be retried — retrying
/// a 430 daily-quota error just burns more quota.
[[nodiscard]] bool isTransient(const ErrorUtils::ErrorContext &err);

/// Delay before the next attempt. Honours the server's Retry-After when
/// present (@p retryAfterSeconds > 0), clamped to @p maxDelayMs so a hostile
/// or fat-fingered header can't stall a batch for minutes. Otherwise uses
/// exponential backoff: baseDelayMs * 2^attempt (attempt is 0-based: the delay
/// before the 1st retry uses attempt==0), clamped to maxDelayMs.
[[nodiscard]] int retryDelayMs(int attempt, int retryAfterSeconds,
                               int baseDelayMs = kDefaultBaseDelayMs,
                               int maxDelayMs = kDefaultMaxDelayMs);

} // namespace Scraper::RetryPolicy

#endif // SCRAPERRETRYPOLICY_H
