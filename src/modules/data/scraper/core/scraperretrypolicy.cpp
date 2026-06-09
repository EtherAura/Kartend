#include "scraperretrypolicy.h"

namespace Scraper::RetryPolicy {

bool isTransient(const ErrorUtils::ErrorContext &err) {
  // A network transfer timeout surfaces as OperationCancelled with no HTTP
  // status (httpStatus <= 0) — the request never got a response. SS's own
  // user-initiated cancel uses the same code, but that path doesn't run
  // through the provider's HTTP error handler, so a 0-status OperationCancelled
  // reaching the retry gate is a genuine transient transport failure.
  if (err.httpStatus <= 0) {
    return err.code == ErrorUtils::ErrorCode::OperationCancelled;
  }
  if (err.httpStatus == 423) return true;                // SS "infrastructure down"
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
