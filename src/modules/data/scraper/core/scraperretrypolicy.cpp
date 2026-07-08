#include "scraperretrypolicy.h"

namespace Scraper::RetryPolicy {

bool isTransient(const ErrorUtils::ErrorContext &err) {
  // A network transfer timeout surfaces as OperationCancelled with no HTTP
  // status (httpStatus <= 0) — the request never got a response, so a retry
  // can plausibly recover it. Deliberate local cancellations do NOT share
  // that code: HttpClient::clearPending resolves queued requests with
  // RequestQueueCleared precisely so a batch cancel() — whose queued lookups'
  // callbacks ARE this retry gate — can't schedule retries that re-issue real
  // network requests for a killed run (Kartend-jjyst.2).
  if (err.httpStatus <= 0) {
    return err.code == ErrorUtils::ErrorCode::OperationCancelled;
  }
  if (err.httpStatus == 423) return true; // SS "infrastructure down"
  // Transient throttling (RFC 6585 429): when the server supplied a
  // Retry-After hint, waiting it out is exactly what it asked for —
  // retryDelayMs honours the hint, clamped so a hostile header can't stall
  // the batch. Without the hint we can't tell a seconds-long burst limit
  // from a daily cap, so the error falls through to the caller; the batch
  // runner escalates to a queue stop only on repeated consecutive 429s
  // (Kartend-jjyst.3). SS's non-standard 430/431 daily quotas stay
  // permanent — retrying them just burns more quota.
  if (err.httpStatus == 429) return err.retryAfterSeconds > 0;
  return err.httpStatus >= 500 && err.httpStatus <= 599; // server-side 5xx
}

int retryDelayMs(int attempt, int retryAfterSeconds, int baseDelayMs, int maxDelayMs) {
  if (maxDelayMs < 0) maxDelayMs = 0;
  if (retryAfterSeconds > 0) {
    // qint64 math so a large header value can't overflow int before clamping.
    const qint64 ms = static_cast<qint64>(retryAfterSeconds) * 1000;
    return ms > maxDelayMs ? maxDelayMs : static_cast<int>(ms);
  }
  if (baseDelayMs <= 0) return 0;
  const int shift = attempt < 0 ? 0 : (attempt > 30 ? 30 : attempt);
  const qint64 delay = static_cast<qint64>(baseDelayMs) << shift;
  return delay > maxDelayMs ? maxDelayMs : static_cast<int>(delay);
}

} // namespace Scraper::RetryPolicy
