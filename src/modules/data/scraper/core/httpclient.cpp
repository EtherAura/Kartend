#include "httpclient.h"

#include <utility>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>

#include "scrapelogging.h"

// Always-on logging for HTTP layer health: SSL config on first request,
// non-trivial socket-level errors. Kept distinct from lcScrapeTimings so it
// survives the timings-category's default Warning-only filter.
// (lcScrapeTimings itself is defined in scrapelogging.cpp — shared with the
// scrape result dialog so a single QT_LOGGING_RULES rule covers both surfaces.)
Q_LOGGING_CATEGORY(lcScraperHttp, "kartend.scraper.http")

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
constexpr const char *kSensitiveQueryKeys[] = {"devpassword", "sspassword", "password",
                                               "passwd",      "apikey",     "api_key",
                                               "token",       "ssid",       "devid"};

QString redactedUrlForLog(const QUrl &url) {
  QUrl sanitized = url;
  QUrlQuery query(sanitized);
  bool changed = false;
  for (const char *key : kSensitiveQueryKeys) {
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

/// Masks the VALUES of @p url's credential-bearing query parameters wherever
/// they appear in @p text. Qt's QNetworkReply::errorString() embeds the full
/// request URL ("Error transferring <url> - server replied: ..."), so an
/// error fold that starts from errorString() carries devpassword /
/// sspassword verbatim — and that string flows into ErrorContext::details,
/// userFacingSummary()'s first-line pick, the always-on kartend.errors log,
/// the scrape-failure UI list, and the persisted resume file. Replacing the
/// values (raw and percent-encoded forms) rather than re-parsing the URL out
/// of prose also covers a server body that echoes a credential back.
QString redactCredentialValues(QString text, const QUrl &url) {
  const QUrlQuery query(url);
  for (const char *key : kSensitiveQueryKeys) {
    const QString k = QLatin1String(key);
    if (!query.hasQueryItem(k)) continue;
    for (const QString &value : {query.queryItemValue(k, QUrl::FullyDecoded),
                                 query.queryItemValue(k, QUrl::FullyEncoded)}) {
      // Single-character values would shotgun the whole string; real
      // credentials are longer.
      if (value.size() >= 2) text.replace(value, QStringLiteral("<redacted>"));
    }
  }
  return text;
}

/// True if @p host equals one of @p allowedSuffixes or is a subdomain of
/// one. The leading-dot boundary is load-bearing: matching plain
/// endsWith("screenscraper.fr") would also accept the look-alikes
/// "evilscreenscraper.fr" and "screenscraper.fr.attacker.test", which is
/// exactly the bypass this guard exists to stop. QUrl lowercases host,
/// but the compares stay case-insensitive defensively.
bool hostMatchesAllowlist(const QString &host, const QStringList &allowedSuffixes) {
  for (const QString &suffix : allowedSuffixes) {
    if (host.compare(suffix, Qt::CaseInsensitive) == 0) {
      return true;
    }
    const QString dotSuffix = QLatin1Char('.') + suffix;
    if (host.size() > dotSuffix.size() && host.endsWith(dotSuffix, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}
} // namespace

HttpClient *HttpClient::instance() {
  Q_ASSERT_X(QCoreApplication::instance() != nullptr, "HttpClient::instance",
             "HttpClient::instance() called before QApplication construction or "
             "after QCoreApplication::quit() completed. The singleton's lifetime "
             "is tied to QApplication — see HttpClient header for rationale.");
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
  // Kartend-s3hvv: see HttpClient::get — main-thread-only invariant for the
  // unguarded rate-limit map.
  Q_ASSERT_X(thread() == QThread::currentThread(), "HttpClient::setRateLimit",
             "HttpClient must be driven from its own (main) thread");
  // Release-mode guard (the assert above compiles out under NDEBUG): a
  // wrong-thread write would race the main thread's reads of m_rateLimits.
  // Refuse loudly instead — dropping a policy update is recoverable, a
  // corrupted QHash is not.
  if (thread() != QThread::currentThread()) {
    qCCritical(lcScraperHttp)
        << "HttpClient::setRateLimit called off its owning thread — ignoring update for" << host;
    return;
  }
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
  // Kartend-s3hvv: see HttpClient::get — main-thread-only invariant for the
  // unguarded queue map.
  Q_ASSERT_X(thread() == QThread::currentThread(), "HttpClient::clearPending",
             "HttpClient must be driven from its own (main) thread");
  // Release-mode guard (the assert above compiles out under NDEBUG): a
  // wrong-thread exchange would race the main thread's queue mutations and the
  // callbacks would fire on the wrong thread. Refuse loudly — a skipped clear
  // leaves requests to complete normally, a corrupted QHash is not recoverable.
  if (thread() != QThread::currentThread()) {
    qCCritical(lcScraperHttp) << "HttpClient::clearPending called off its owning thread — ignoring";
    return;
  }
  // Fire each queued (not-yet-dispatched) request's callback with a cancelled
  // error before dropping it, so a caller waiting on the callback — e.g. a
  // BatchScrapeRunner media-aggregator's --pending count or any in-flight tally
  // — always resolves instead of hanging on a silently-discarded request
  // (Kartend audit nujso). Move the queues out first so a callback that
  // re-enters (enqueues a new request, or calls clearPending again) operates on
  // the already-emptied member instead of the container being iterated.
  //
  // m_inFlight is deliberately untouched — already-dispatched replies still
  // complete and fire their callbacks, and drainHost() sees the right count
  // after they decrement it.
  const QHash<QString, QQueue<PendingRequest>> dropped = std::exchange(m_queues, {});
  for (const QQueue<PendingRequest> &queue : dropped) {
    for (const PendingRequest &req : queue) {
      if (req.callback) {
        req.callback(ErrorContext::error(ErrorCode::OperationCancelled,
                                         "Request cancelled: HTTP queue cleared",
                                         "Scraper::HttpClient::clearPending"));
      }
    }
  }
}

namespace {
// One-shot SSL config snapshot, run lazily on the first HttpClient::get() so
// users diagnosing scraper connectivity have a record of which OpenSSL build,
// CA bundle, and verify policy is in effect on this machine. Empty CA bundle
// surfaces as qWarning so a missing system cert store doesn't fail silently
// when QNetworkAccessManager falls back to no-verify on some Qt versions.
void logSslConfigOnce() {
  static bool logged = false;
  if (logged) {
    return;
  }
  logged = true;
  const QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
  qCInfo(lcScraperHttp).nospace() << "SSL supportsSsl=" << QSslSocket::supportsSsl()
                                  << " runtimeVersion=" << QSslSocket::sslLibraryVersionString()
                                  << " buildVersion=" << QSslSocket::sslLibraryBuildVersionString()
                                  << " peerVerifyMode=" << cfg.peerVerifyMode()
                                  << " caCertCount=" << cfg.caCertificates().size();
  if (cfg.caCertificates().isEmpty()) {
    qCWarning(lcScraperHttp)
        << "SSL default CA certificate store is empty — scraper HTTPS requests "
           "may fail or fall back to no-verify. Install your distribution's "
           "ca-certificates package (Debian/Ubuntu) or update Qt's openssl backend.";
  }
}
} // namespace

void HttpClient::get(const QUrl &url, const RawHeaders &headers, ResponseCallback callback,
                     qint64 maxResponseBytes, const QString &expectedContentTypePrefix,
                     const QStringList &allowedHostSuffixes) {
  // Kartend-s3hvv: the per-host queue/in-flight maps are unguarded by design —
  // every caller runs on HttpClient's (main) thread. Lock that invariant in
  // with a debug assert, mirroring QueryManager::assertOwnerThread, so a future
  // worker-thread caller fails loudly instead of silently corrupting the maps.
  Q_ASSERT_X(thread() == QThread::currentThread(), "HttpClient::get",
             "HttpClient must be driven from its own (main) thread");
  // Release-mode guard (the assert above compiles out under NDEBUG): a
  // wrong-thread get() would race the main thread on every per-host map and
  // silently corrupt them. Refuse the request loudly instead, resolving the
  // callback (on the CALLING thread, as an exception to the main-thread
  // delivery contract — the caller is already off-contract) so a waiting
  // caller gets an error rather than a hang.
  if (thread() != QThread::currentThread()) {
    qCCritical(lcScraperHttp)
        << "HttpClient::get called off its owning thread — refusing request to host:" << url.host();
    if (callback) {
      callback(ErrorContext::error(ErrorCode::InvalidArgument,
                                   "HttpClient::get called off its owning thread",
                                   "Scraper::HttpClient::get"));
    }
    return;
  }
  logSslConfigOnce();
  if (!url.isValid()) {
    if (callback) {
      callback(ErrorContext::error(ErrorCode::InvalidArgument, "Invalid request URL",
                                   "Scraper::HttpClient::get"));
    }
    return;
  }
  // SSRF guard (Kartend-pugp.1): this is the single choke point for every
  // scraper request, and some request URLs are response-derived and thus
  // untrusted — most notably ScreenScraper's medias[].url, which arrives
  // here verbatim via fetchMediaBytes. Refuse any non-HTTPS scheme so a
  // hostile response can't steer a request at file:// / qrc:// / data:// /
  // ftp:// or plaintext http:// against an internal/link-local host. Every
  // provider builds on an https base, so this rejects nothing legitimate.
  // QUrl normalises the scheme to lowercase, so an exact compare suffices.
  // host()/scheme() are logged but the path+query are not — those can carry
  // local file paths or the credential query params redactedUrlForLog masks.
  if (url.scheme() != QLatin1String("https")) {
    qCWarning(lcScraperHttp) << "Refusing non-HTTPS scraper request URL — scheme:" << url.scheme()
                             << "host:" << url.host();
    if (callback) {
      callback(ErrorContext::error(ErrorCode::InvalidArgument, "Refused non-HTTPS request URL",
                                   "Scraper::HttpClient::get"));
    }
    return;
  }
  // Host allowlist (Kartend-pugp.2): defense-in-depth on top of the https
  // guard for callers whose URL is response-derived (medias[].url). When a
  // caller pins an allowlist, refuse an initial host outside it here at the
  // same choke point; send() additionally constrains redirects to the same
  // set. Empty allowlist (every other caller) leaves the request unrestricted.
  if (!allowedHostSuffixes.isEmpty() && !hostMatchesAllowlist(url.host(), allowedHostSuffixes)) {
    qCWarning(lcScraperHttp) << "Refusing scraper request to host outside allowlist — host:"
                             << url.host();
    if (callback) {
      callback(ErrorContext::error(ErrorCode::InvalidArgument,
                                   "Refused request URL outside host allowlist",
                                   "Scraper::HttpClient::get"));
    }
    return;
  }
  PendingRequest req{url,
                     headers,
                     std::move(callback),
                     maxResponseBytes,
                     expectedContentTypePrefix,
                     allowedHostSuffixes};
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
  // Raw headers verbatim, in caller order. User-Agent and any
  // credential header (e.g. Authorization: Bearer) come through here;
  // we deliberately never log them, so a token in a header never
  // reaches scrape.log the way a query-string secret would.
  for (const QPair<QByteArray, QByteArray> &header : request.headers) {
    qreq.setRawHeader(header.first, header.second);
  }
  // Redirect handling. The default NoLessSafeRedirectPolicy follows a 3xx to
  // any https host. For host-pinned callers (Kartend-pugp.2) that is exactly
  // the SSRF pivot we must block, so switch to UserVerifiedRedirectPolicy:
  // QNAM then *pauses* on each redirect and waits for redirectAllowed() before
  // contacting the new host, letting the redirected slot below reject an
  // off-allowlist target before any connection is made (a plain abort() under
  // an auto-following policy would race the request to the new host).
  const QStringList allowedHostSuffixes = request.allowedHostSuffixes;
  const bool restrictRedirects = !allowedHostSuffixes.isEmpty();
  qreq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                    restrictRedirects ? QNetworkRequest::UserVerifiedRedirectPolicy
                                      : QNetworkRequest::NoLessSafeRedirectPolicy);
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

  // Verified-redirect guard (Kartend-pugp.2). Under UserVerifiedRedirectPolicy
  // QNAM holds each redirect until we either let it proceed or abort. Allow a
  // hop only to a host inside the caller's allowlist; otherwise abort before
  // the new host is contacted and flag it so the finished slot reports a
  // refused redirect (InvalidArgument) rather than a generic timeout. Shared
  // with the finished slot the same way sizeCapExceeded distinguishes an
  // oversize abort from a transfer-timeout abort.
  auto redirectBlocked = std::make_shared<bool>(false);
  if (restrictRedirects) {
    connect(reply, &QNetworkReply::redirected, this,
            [reply, redirectBlocked, allowedHostSuffixes, host, urlPath](const QUrl &target) {
              if (hostMatchesAllowlist(target.host(), allowedHostSuffixes)) {
                emit reply->redirectAllowed();
                return;
              }
              *redirectBlocked = true;
              qCWarning(lcScraperHttp)
                  << "Refusing scraper redirect to host outside allowlist — from:" << host
                  << urlPath << "to host:" << target.host();
              reply->abort();
            });
  }

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, host, callback = std::move(callback), perReq, urlPath, sizeCapExceeded,
           redirectBlocked, expectedContentTypePrefix]() mutable {
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
                // Bound the error-body read as defense-in-depth: the success
                // path is capped via downloadProgress, but the error path has
                // no independent guard. 64 KiB is ample for a server error
                // message (Kartend audit SEC-03).
                constexpr qint64 kMaxErrorBodyBytes = 64 * 1024;
                const QByteArray body = reply->read(kMaxErrorBodyBytes);
                QString details = reply->errorString();
                if (!body.isEmpty()) {
                  // The body is provider-controlled text headed for
                  // ErrorContext::details and, from there, the always-on
                  // kartend.errors log — flatten it to one bounded line so
                  // embedded CR/LF can't forge log entries. The structural
                  // '\n' before "Response:" is ours, and keeps
                  // userFacingSummary()'s first-line pick on errorString().
                  details += QStringLiteral("\nResponse: ") +
                             ErrorUtils::sanitizedSingleLine(QString::fromUtf8(body));
                }
                // errorString() embeds the full request URL, and a server
                // body can echo credentials back — mask credential query
                // values before this string fans out to details / summaries /
                // logs / the resume file (see redactCredentialValues). Both
                // the original and the (possibly redirected) final URL can
                // carry the credentials.
                details = redactCredentialValues(std::move(details), reply->request().url());
                details = redactCredentialValues(std::move(details), reply->url());
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
                // A refused redirect (Kartend-pugp.2) also surfaces as
                // OperationCanceledError via our abort() in the redirected
                // slot; the redirectBlocked flag tells it apart from the
                // size-cap and transfer-timeout aborts so the user sees the
                // real cause instead of a misleading "timed out".
                const bool refusedRedirect =
                    reply->error() == QNetworkReply::OperationCanceledError && *redirectBlocked;
                const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError &&
                                      !*sizeCapExceeded && !*redirectBlocked;
                ErrorCode errorCode = ErrorCode::DatabaseQueryFailed;
                QString errorMsg = QStringLiteral("HTTP request failed");
                if (oversizedResponse) {
                  errorCode = ErrorCode::ResponseTooLarge;
                  errorMsg = QStringLiteral("Response exceeded size cap; aborted");
                } else if (refusedRedirect) {
                  errorCode = ErrorCode::InvalidArgument;
                  errorMsg = QStringLiteral("Refused redirect to host outside allowlist");
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
