// Unit tests for Scraper::RetryPolicy (Kartend-1rtrt) — the pure
// transient-classification + backoff math behind the provider's bounded
// jeuInfos retry. No network: these lock the decisions the timer-driven
// re-dispatch in ScreenScraperProvider consumes.
#include <QtTest/QtTest>

#include "errorutils.h"
#include "scraperretrypolicy.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
namespace RP = Scraper::RetryPolicy;

namespace {
// Build an HTTP-status error the way mapScreenScraperHttpError-adjacent code
// does: a generic failure carrying the upstream status.
ErrorContext httpError(int status, int retryAfter = 0) {
  auto e =
      ErrorContext::error(ErrorCode::DatabaseQueryFailed, QStringLiteral("x"), QStringLiteral("t"))
          .withHttpStatus(status);
  if (retryAfter > 0) e.withRetryAfter(retryAfter);
  return e;
}
} // namespace

class TestScraperRetryPolicy : public QObject {
  Q_OBJECT
private slots:
  void isTransient_timeoutIsRetried();
  void isTransient_queueClearedCancelIsNotRetried();
  void isTransient_serverErrorsAreRetried();
  void isTransient_serviceLockedIsRetried();
  void isTransient_rateLimitWithRetryAfterIsRetried();
  void isTransient_rateLimitWithoutRetryAfterIsNotRetried();
  void isTransient_clientErrorsAreNotRetried();
  void isTransient_zeroStatusNonCancelIsNotRetried();
  void retryDelay_honoursRetryAfterClampedToMax();
  void retryDelay_exponentialBackoffWhenNoRetryAfter();
  void retryDelay_backoffClampedToMax();
};

void TestScraperRetryPolicy::isTransient_timeoutIsRetried() {
  // A transfer timeout surfaces as OperationCancelled with no HTTP status.
  auto timeout = ErrorContext::error(ErrorCode::OperationCancelled, QStringLiteral("timed out"),
                                     QStringLiteral("t"));
  QVERIFY(timeout.httpStatus <= 0);
  QVERIFY(RP::isTransient(timeout));
}

void TestScraperRetryPolicy::isTransient_queueClearedCancelIsNotRetried() {
  // Kartend-jjyst.2: HttpClient::clearPending resolves each queued request
  // with RequestQueueCleared — a deliberate local cancel, byte-identical to a
  // transfer timeout in every other field (no HTTP status). It must NOT be
  // transient: for queued jeuInfos lookups this callback IS the provider's
  // retry gate, and retrying would re-issue real network requests (burning
  // ScreenScraper quota) for a batch the user just cancelled.
  auto cleared = ErrorContext::error(ErrorCode::RequestQueueCleared,
                                     QStringLiteral("Request cancelled: HTTP queue cleared"),
                                     QStringLiteral("Scraper::HttpClient::clearPending"));
  QVERIFY(cleared.httpStatus <= 0);
  QVERIFY(!RP::isTransient(cleared));
}

void TestScraperRetryPolicy::isTransient_serverErrorsAreRetried() {
  for (int status : {500, 502, 503, 504, 599}) {
    QVERIFY2(RP::isTransient(httpError(status)),
             qPrintable(QStringLiteral("HTTP %1 should be transient").arg(status)));
  }
}

void TestScraperRetryPolicy::isTransient_serviceLockedIsRetried() {
  // SS uses 423 for "infrastructure down" — recoverable on a later attempt.
  QVERIFY(RP::isTransient(httpError(423)));
}

void TestScraperRetryPolicy::isTransient_rateLimitWithRetryAfterIsRetried() {
  // Kartend-jjyst.3: a 429 carrying a Retry-After hint is burst throttling
  // the server explicitly asked us to wait out — transient, with the delay
  // taken from the hint (clamped; see retryDelay_honoursRetryAfterClampedToMax).
  QVERIFY(RP::isTransient(httpError(429, /*retryAfter=*/5)));
  QCOMPARE(RP::retryDelayMs(0, /*retryAfterSeconds=*/5), 5000);
}

void TestScraperRetryPolicy::isTransient_rateLimitWithoutRetryAfterIsNotRetried() {
  // A hint-less 429 could be a daily cap — blind-retrying it can't help.
  // It falls through to the batch runner, which stops the queue only on
  // repeated consecutive 429s (Kartend-jjyst.3).
  QVERIFY(!RP::isTransient(httpError(429)));
}

void TestScraperRetryPolicy::isTransient_clientErrorsAreNotRetried() {
  // Auth/quota/not-found/malformed are permanent for this request; retrying a
  // 430 daily-quota error would only burn more quota. (429 is the one 4xx
  // with a transient shape — covered by the rateLimit tests above.)
  for (int status : {400, 401, 403, 404, 426, 430, 431}) {
    QVERIFY2(!RP::isTransient(httpError(status)),
             qPrintable(QStringLiteral("HTTP %1 must not be retried").arg(status)));
  }
  // 430/431 stay permanent even when the server (nonsensically) supplies a
  // Retry-After — only 429 honours the hint.
  QVERIFY(!RP::isTransient(httpError(430, /*retryAfter=*/5)));
  QVERIFY(!RP::isTransient(httpError(431, /*retryAfter=*/5)));
}

void TestScraperRetryPolicy::isTransient_zeroStatusNonCancelIsNotRetried() {
  // A no-status error that isn't a cancellation/timeout (e.g. a local validation
  // failure before the request left) is not a transport transient.
  auto local = ErrorContext::error(ErrorCode::InvalidArgument, QStringLiteral("bad url"),
                                   QStringLiteral("t"));
  QVERIFY(!RP::isTransient(local));
}

void TestScraperRetryPolicy::retryDelay_honoursRetryAfterClampedToMax() {
  // Retry-After wins over backoff and is expressed in seconds.
  QCOMPARE(RP::retryDelayMs(0, /*retryAfterSeconds=*/3), 3000);
  QCOMPARE(RP::retryDelayMs(1, /*retryAfterSeconds=*/2), 2000);
  // A hostile/huge Retry-After is clamped so it can't stall the batch.
  QCOMPARE(RP::retryDelayMs(0, /*retryAfterSeconds=*/100000, /*base=*/500, /*max=*/30000), 30000);
}

void TestScraperRetryPolicy::retryDelay_exponentialBackoffWhenNoRetryAfter() {
  // base * 2^attempt, attempt 0-based.
  QCOMPARE(RP::retryDelayMs(0, 0, /*base=*/500, /*max=*/30000), 500);
  QCOMPARE(RP::retryDelayMs(1, 0, /*base=*/500, /*max=*/30000), 1000);
  QCOMPARE(RP::retryDelayMs(2, 0, /*base=*/500, /*max=*/30000), 2000);
}

void TestScraperRetryPolicy::retryDelay_backoffClampedToMax() {
  // A high attempt count can't overflow or exceed the cap.
  QCOMPARE(RP::retryDelayMs(20, 0, /*base=*/500, /*max=*/30000), 30000);
  QCOMPARE(RP::retryDelayMs(1000, 0, /*base=*/500, /*max=*/30000), 30000);
}

QTEST_MAIN(TestScraperRetryPolicy)
#include "test_scraperretrypolicy.moc"
