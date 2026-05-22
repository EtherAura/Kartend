#include "httpclient.h"

#include <utility>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

// Diagnostic log for scrape-speed work. Stays silent at runtime by
// default — every ENQ / START / FINISH below would otherwise stream
// to stderr synchronously on the main thread, and with high
// batchItemConcurrency that's thousands of writes/sec that starve
// the Qt event loop and freeze the UI. Enable via
// `QT_LOGGING_RULES=kartend.scrape.timings.debug=true` when diagnosing.
Q_LOGGING_CATEGORY(lcScrapeTimings, "kartend.scrape.timings", QtWarningMsg)

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace Scraper {

namespace {
/// Transfer (inactivity) timeout for every scraper HTTP request.
/// QNetworkAccessManager never aborts a stalled connection on its own,
/// so a reply whose socket goes silent would otherwise never emit
/// finished() — its response callback never runs, the per-item chain
/// in BatchScrapeRunner never completes, and because rate-limited
/// providers run with maxConcurrent=1 every later request queued for
/// that host is blocked behind it. The whole batch then wedges with
/// the result dialog's ETA still climbing. setTransferTimeout makes Qt
/// abort a request after this many ms of zero byte movement; the abort
/// surfaces as finished() + OperationCanceledError and flows through
/// send()'s existing error path, so the item is recorded as an error
/// and the batch keeps going. 30s of total inactivity is an
/// unambiguous stall — a slow but progressing download keeps resetting
/// the timer because bytes are still arriving.
constexpr int kTransferTimeoutMs = 30000;

/// Builds a log-safe URL string with credential-bearing query parameters
/// masked. Scraper request URLs carry ScreenScraper `devpassword` /
/// `sspassword` (and account ids) in the query string; logging them
/// verbatim leaks working credentials into scrape.log. START / FINISH log
/// only url.path() and are unaffected — this is for the ENQ line, which
/// needs the query string to show media presets.
QString redactedUrlForLog(const QUrl &url) {
  static const char *const kSensitiveKeys[] = {"devpassword", "sspassword", "password",
                                               "passwd",      "apikey",     "api_key",
                                               "token",       "ssid",       "devid"};
  QUrl sanitized = url;
  QUrlQuery query(sanitized);
  bool changed = false;
  for (const char *key : kSensitiveKeys) {
    const QString k = QLatin1String(key);
    if (query.hasQueryItem(k)) {
      query.removeAllQueryItems(k);
      query.addQueryItem(k, QStringLiteral("<redacted>"));
      changed = true;
    }
  }
  if (changed) {
    sanitized.setQuery(query);
  }
  return sanitized
      .toString(QUrl::RemoveScheme | QUrl::RemoveUserInfo | QUrl::RemovePort | QUrl::RemoveFragment)
      .left(200);
}
} // namespace

HttpClient *HttpClient::instance() {
  static HttpClient *s_instance = nullptr;
  if (!s_instance) {
    // Parent to qApp so the QObject lives for the application lifetime
    // and gets cleaned up at shutdown without us tracking it.
    s_instance = new HttpClient(QCoreApplication::instance());
  }
  return s_instance;
}

HttpClient::HttpClient(QObject *parent) : QObject(parent) {
  m_qnam = new QNetworkAccessManager(this);
  // Without this a stalled reply never completes and pins its host's
  // concurrency slot forever (see kTransferTimeoutMs).
  m_qnam->setTransferTimeout(kTransferTimeoutMs);
}

HttpClient::~HttpClient() = default;

void HttpClient::setRateLimit(const QString &host, int intervalMs, int maxConcurrent) {
  if (intervalMs <= 0 && maxConcurrent <= 0) {
    m_rateLimits.remove(host);
    return;
  }
  HostPolicy policy;
  policy.intervalMs = std::max(0, intervalMs);
  policy.maxConcurrent = std::max(1, maxConcurrent);
  m_rateLimits.insert(host, policy);
}

void HttpClient::clearPending() {
  m_queues.clear();
  // Don't touch m_inFlight — the in-flight replies will still fire and
  // we want drainHost() to see the right count after they decrement.
}

void HttpClient::get(const QUrl &url, const QString &userAgent, ResponseCallback callback,
                     qint64 maxResponseBytes, const QString &expectedContentTypePrefix) {
  if (!url.isValid()) {
    if (callback) {
      callback(ErrorContext::error(ErrorCode::InvalidArgument, "Invalid request URL",
                                   "Scraper::HttpClient::get"));
    }
    return;
  }
  PendingRequest req{url, userAgent, std::move(callback), maxResponseBytes,
                     expectedContentTypePrefix};
  const QString host = url.host();
  // Timestamped trace so we can see (a) when the caller enqueued vs.
  // (b) when the request actually starts vs. (c) when the reply lands.
  // The path-truncation keeps the line short; full URL would dominate
  // the log.
  // Include the full query string (truncated) so we can verify that
  // mediaMaxDim is actually being applied to image URLs. Without
  // this, the path alone hides `maxwidth=N&maxheight=N` and we can't
  // tell from the log whether the preset took effect.
  // toString() walks the whole URL — only pay for it when the
  // category is actually enabled, otherwise this string-build runs
  // for every queued request even when no logs are written.
  if (lcScrapeTimings().isDebugEnabled()) {
    const QString urlForLog = redactedUrlForLog(url);
    qCDebug(lcScrapeTimings) << "ENQ" << host << urlForLog
                             << "queue=" << m_queues.value(host).size()
                             << "inflight=" << m_inFlight.value(host, 0);
  }
  enqueue(host, std::move(req));
}

void HttpClient::enqueue(const QString &host, PendingRequest request) {
  // QQueue::enqueue takes const T& (no rvalue overload), so std::move
  // here would be a no-op — use the underlying QList::append rvalue
  // overload directly to actually move the PendingRequest's QNetworkRequest
  // + QByteArray payload instead of copying them.
  m_queues[host].append(std::move(request));
  drainHost(host);
}

void HttpClient::drainHost(const QString &host) {
  auto qit = m_queues.find(host);
  if (qit == m_queues.end() || qit->isEmpty()) {
    return;
  }
  const HostPolicy policy = m_rateLimits.value(host, HostPolicy{});
  while (!qit->isEmpty()) {
    // Concurrency gate: don't exceed the host's allowed in-flight cap.
    const int inFlight = m_inFlight.value(host, 0);
    if (inFlight >= policy.maxConcurrent) {
      return;
    }
    // Inter-start spacing: this is what makes 1 thread @ 250ms feel
    // different from N threads @ 250ms. Each slot is paced
    // independently because we only check the *last* start; with
    // concurrency > 1 the burst pattern is N requests in quick
    // succession, then a wait, then N more. That matches what the
    // upstream usually allows: "n concurrent threads, each capped at
    // m req/s" → bursts of N, paced at intervalMs per burst step.
    auto &timer = m_lastStartTimer[host];
    if (policy.intervalMs > 0 && timer.isValid()) {
      const qint64 elapsed = timer.elapsed();
      if (elapsed < policy.intervalMs) {
        const qint64 wait = policy.intervalMs - elapsed;
        // Only schedule a wakeup once per pending wait. The flag is
        // cleared inside the singleShot lambda (when the timer fires)
        // rather than at the top of drainHost — otherwise every
        // back-to-back enqueue would clear the flag, re-schedule
        // another timer, and end up serialising 1 PACE log + 1
        // QTimer instance per enqueue. (Diagnosed via the user's
        // /tmp/scrape.log: 26 PACE lines and 26 stacked timers
        // between consecutive STARTs.)
        if (!m_drainScheduled.value(host, false)) {
          m_drainScheduled.insert(host, true);
          qCDebug(lcScrapeTimings) << "PACE" << host << "wait" << wait << "ms"
                                   << "queue=" << qit->size() << "inflight=" << inFlight;
          // `wait` is the computed per-host pacing interval (server rate-limit
          // backoff). Single scheduled drain per host enforces the gap; the
          // drainScheduled flag (set just above, cleared on fire) is what
          // collapses bursty enqueues into one timer instead of N stacked ones.
          QTimer::singleShot(static_cast<int>(wait), this, [this, host]() {
            m_drainScheduled.insert(host, false);
            drainHost(host);
          });
        }
        return;
      }
    }
    PendingRequest req = qit->dequeue();
    m_inFlight.insert(host, inFlight + 1);
    timer.start();
    qCDebug(lcScrapeTimings) << "START" << host << req.url.path().left(80) << "inflight->"
                             << (inFlight + 1) << "remaining_queue=" << qit->size();
    send(host, std::move(req));
  }
}

void HttpClient::send(const QString &host, PendingRequest request) {
  QNetworkRequest qreq(request.url);
  if (!request.userAgent.isEmpty()) {
    qreq.setHeader(QNetworkRequest::UserAgentHeader, request.userAgent);
  }
  qreq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                    QNetworkRequest::NoLessSafeRedirectPolicy);
  // Explicit HTTP/2 opt-in so concurrent media downloads multiplex over
  // a single TCP+TLS connection. Without this Qt picks per-server, and
  // SS's CDN advertises h2 in its ALPN — but several Qt versions still
  // fall back to HTTP/1.1 for follow-up requests if the negotiation
  // races. Forcing the attribute up front guarantees multiplexing, which
  // matters because TCP-fairness collapse between N parallel HTTP/1.1
  // streams to one host was the bandwidth-degradation we saw at
  // concurrency=8 (log: individual replies stretched from 5s to 145s).
  qreq.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

  QNetworkReply *reply = m_qnam->get(qreq);
  // Capture by value: callback is move-only-friendly via std::function,
  // and url/host stay live for the slot.
  ResponseCallback callback = std::move(request.callback);
  // Per-request timer so we can attribute the wallclock duration of
  // the actual network roundtrip (distinct from any time spent
  // queued behind the throttle).
  auto perReq = std::make_shared<QElapsedTimer>();
  perReq->start();
  const QString urlPath = request.url.path().left(80);
  const qint64 maxResponseBytes = request.maxResponseBytes;
  // Captured by value into the finished slot so the MIME check runs on
  // the response Content-Type (Kartend-9ryx). Empty => no check.
  const QString expectedContentTypePrefix = request.expectedContentTypePrefix;

  // Response-size cap: a hostile or buggy server can stream gigabytes
  // unboundedly otherwise. Hook downloadProgress and abort the reply if
  // received bytes cross the cap. Shared flag lets the finished slot
  // distinguish a cap-induced abort (ResponseTooLarge) from the
  // transfer-timeout abort QNAM itself raises (OperationCancelled), so
  // the error message and ErrorCode line up with the actual cause.
  auto sizeCapExceeded = std::make_shared<bool>(false);
  if (maxResponseBytes > 0) {
    connect(reply, &QNetworkReply::downloadProgress, this,
            [reply, sizeCapExceeded, maxResponseBytes, host, urlPath](qint64 received,
                                                                      qint64 /*total*/) {
              if (received > maxResponseBytes && !*sizeCapExceeded) {
                *sizeCapExceeded = true;
                qCWarning(lcScrapeTimings) << "ABORT-OVERSIZE" << host << urlPath
                                           << "received=" << received << "cap=" << maxResponseBytes;
                reply->abort();
              }
            });
  }

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, host, callback = std::move(callback), perReq, urlPath, sizeCapExceeded,
           expectedContentTypePrefix]() mutable {
            const qint64 ms = perReq->elapsed();
            // Definitive HTTP/2 readout. The connect-side attribute we
            // set asks Qt to *try* h2, but the actual negotiation can
            // fall back to HTTP/1.1 if the server's ALPN or our Qt
            // build don't agree. Knowing the truth changes the
            // diagnosis: h2=false means N parallel media downloads
            // open N TCP connections (with QNAM's 6-per-host cap) and
            // suffer TCP-fairness collapse on a thin link; h2=true
            // means they all multiplex over one connection and the
            // remaining slowness is purely bandwidth.
            // Skip the attribute fetches + size lookup when nobody's
            // listening — same hot-path concern as the ENQ string-build.
            if (lcScrapeTimings().isDebugEnabled()) {
              const bool h2 = reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
              const qint64 size = reply->size();
              qCDebug(lcScrapeTimings) << "FINISH" << host << urlPath << "took" << ms << "ms"
                                       << "size=" << size << "h2=" << h2
                                       << "err=" << (reply->error() != QNetworkReply::NoError);
            }
            if (callback) {
              if (reply->error() != QNetworkReply::NoError) {
                // Many APIs send a useful plain-text or JSON error body
                // alongside a 4xx/5xx (ScreenScraper sends "Erreur de
                // login : ..." with HTTP 403, MusicBrainz returns a
                // JSON error object, etc.) — surface it in details so
                // the user sees what the server actually said, not just
                // the generic "request failed" Qt produces from the
                // status code alone.
                const QByteArray body = reply->readAll();
                QString details = reply->errorString();
                if (!body.isEmpty()) {
                  details += QStringLiteral("\nResponse: ") + QString::fromUtf8(body).trimmed();
                }
                // Capture the HTTP status code (when the transport got
                // far enough to receive one) and any Retry-After header.
                // ScreenScraperProvider re-maps these into actionable
                // user guidance; without the structured fields it would
                // have to grep the details string.
                const int httpStatus =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                int retryAfter = 0;
                if (reply->hasRawHeader("Retry-After")) {
                  bool ok = false;
                  const int parsed =
                      QString::fromLatin1(reply->rawHeader("Retry-After")).toInt(&ok);
                  if (ok && parsed > 0) retryAfter = parsed;
                }
                // OperationCanceledError surfaces from two distinct
                // sources here: (1) the transfer timeout that QNAM
                // enforces (kTransferTimeoutMs), and (2) our own
                // reply->abort() from the response-size cap above. The
                // sizeCapExceeded flag is the only way to tell them
                // apart — distinguish them so the user sees the right
                // diagnosis (stalled connection vs. oversized response)
                // and so ScreenScraperProvider's retry logic, which
                // treats OperationCancelled as a transient timeout,
                // doesn't loop on a server that's deliberately flooding.
                const bool oversizedResponse =
                    reply->error() == QNetworkReply::OperationCanceledError && *sizeCapExceeded;
                const bool timedOut =
                    reply->error() == QNetworkReply::OperationCanceledError && !*sizeCapExceeded;
                ErrorCode errorCode = ErrorCode::DatabaseQueryFailed;
                QString errorMsg = QStringLiteral("HTTP request failed");
                if (oversizedResponse) {
                  errorCode = ErrorCode::ResponseTooLarge;
                  errorMsg = QStringLiteral("Response exceeded size cap; aborted");
                } else if (timedOut) {
                  errorCode = ErrorCode::OperationCancelled;
                  errorMsg = QStringLiteral("Network request timed out");
                }
                auto ctx = ErrorContext::error(errorCode, errorMsg, "Scraper::HttpClient::send")
                               .withDetails(details)
                               .withHttpStatus(httpStatus);
                if (retryAfter > 0) ctx.withRetryAfter(retryAfter);
                callback(ctx);
              } else if (!expectedContentTypePrefix.isEmpty()) {
                // MIME allowlist guard (Kartend-9ryx): if the caller
                // declared an expected Content-Type prefix (e.g.
                // "image/" for media fetches), verify the server
                // honoured it before handing the body to the decode
                // path. Hostile or misconfigured upstreams that serve
                // HTML / JSON / executable bytes under an image URL
                // are rejected here instead of being routed to
                // QImage::loadFromData below.
                const QString contentType =
                    reply->header(QNetworkRequest::ContentTypeHeader).toString();
                if (!contentType.startsWith(expectedContentTypePrefix, Qt::CaseInsensitive)) {
                  auto ctx =
                      ErrorContext::error(ErrorCode::InvalidArgument,
                                          QStringLiteral("Unexpected response Content-Type"),
                                          "Scraper::HttpClient::send")
                          .withDetails(QStringLiteral("Expected prefix: %1, got: %2")
                                           .arg(expectedContentTypePrefix,
                                                contentType.isEmpty() ? QStringLiteral("(none)")
                                                                      : contentType));
                  callback(ctx);
                } else {
                  callback(reply->readAll());
                }
              } else {
                callback(reply->readAll());
              }
            }
            reply->deleteLater();
            // Drop the in-flight count and immediately try to dispatch
            // the next queued request for this host. drainHost re-checks
            // the inter-start gate so we don't outrun the throttle.
            const int updated = std::max(0, m_inFlight.value(host, 1) - 1);
            m_inFlight.insert(host, updated);
            drainHost(host);
          });
}

} // namespace Scraper
