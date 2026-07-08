#include "providerbase.h"

#include <QLoggingCategory>
#include <QTimer>

// Base-level provider plumbing shared by every API-backed scraper; the
// transient-retry loop below logs here rather than under a per-provider
// category.
Q_LOGGING_CATEGORY(lcProviderBase, "kartend.scraper.provider")

ProviderBase::~ProviderBase() {
  // Stop + delete every pending transient-retry timer (bounded-teardown
  // convention; Kartend audit vrqzk). Their lambdas capture `this` raw, so
  // they must not fire after we're gone. A retry that already fired removed
  // itself in its lambda, so it isn't here.
  for (QTimer *t : std::as_const(m_inFlightRetryTimers)) {
    t->stop();
    delete t;
  }
  m_inFlightRetryTimers.clear();
}

QString ProviderBase::userAgent() {
  // APP_VERSION is injected at compile time via target_compile_definitions
  // in the top-level CMakeLists.txt.
  return QStringLiteral("Kartend/%1 (https://github.com/EtherAura/Kartend)")
      .arg(QString::fromLatin1(APP_VERSION));
}

Scraper::HttpClient::RawHeaders ProviderBase::userAgentHeader() {
  return {{QByteArrayLiteral("User-Agent"), userAgent().toUtf8()}};
}

void ProviderBase::registerThrottles(
    std::initializer_list<std::pair<const char *, int>> hostLimits) {
  Scraper::HttpClient *client = Scraper::HttpClient::instance();
  for (const auto &[host, intervalMs] : hostLimits) {
    client->setRateLimit(QString::fromLatin1(host), intervalMs);
  }
}

ProviderBase::TestFetchFunction ProviderBase::s_testFetch;

void ProviderBase::setFetchFunctionForTesting(TestFetchFunction fetch) {
  s_testFetch = std::move(fetch);
}

void ProviderBase::getWithRetry(const Scraper::HttpClient::RawHeaders &headers, const QUrl &url,
                                Scraper::HttpClient::ResponseCallback onResponse,
                                qint64 maxResponseBytes, const QString &requiredContentTypePrefix,
                                const QStringList &allowedHostSuffixes, RetryOptions retry) {
  dispatchWithRetry(headers, url, std::move(onResponse), maxResponseBytes,
                    requiredContentTypePrefix, allowedHostSuffixes, retry, /*attempt=*/0);
}

void ProviderBase::dispatchWithRetry(const Scraper::HttpClient::RawHeaders &headers,
                                     const QUrl &url,
                                     Scraper::HttpClient::ResponseCallback onResponse,
                                     qint64 maxResponseBytes,
                                     const QString &requiredContentTypePrefix,
                                     const QStringList &allowedHostSuffixes, RetryOptions retry,
                                     int attempt) {
  Scraper::HttpClient::ResponseCallback wrapped =
      [this, headers, url, onResponse = std::move(onResponse), maxResponseBytes,
       requiredContentTypePrefix, allowedHostSuffixes, retry, attempt,
       alive =
           std::weak_ptr<int>(m_lifetimeToken)](ErrorUtils::Result<QByteArray> response) mutable {
        // Liveness guard (Kartend audit cr950 class): this lambda is held by
        // the qApp-lifetime HttpClient with raw `this` and no QObject
        // connection to sever, so it can fire after the provider is destroyed.
        // Deliver the response untouched — no retry bookkeeping on freed
        // members; the caller's onResponse guards its own member access (or,
        // like getJson's parse/callback chain, captures no members at all).
        if (alive.expired()) {
          onResponse(std::move(response));
          return;
        }
        // Kartend-1rtrt / Kartend-jjyst.10: bounded retry for transient
        // transport/server faults. The GET is idempotent, so re-issuing the
        // same URL is safe. Permanent failures (auth/quota/404/parse,
        // hint-less 429, a deliberate RequestQueueCleared cancel) and
        // successes fall straight through to onResponse.
        if (response.isError() && attempt < retry.maxRetries &&
            Scraper::RetryPolicy::isTransient(response.error())) {
          const int delayMs = Scraper::RetryPolicy::retryDelayMs(
              attempt, response.error().retryAfterSeconds, retry.baseDelayMs, retry.maxDelayMs);
          // The URL is deliberately NOT logged: ScreenScraper URLs carry
          // credentials in the query string.
          qCInfo(lcProviderBase).nospace()
              << "Transient fetch failure for provider " << id()
              << " (httpStatus=" << response.error().httpStatus
              << ", code=" << static_cast<int>(response.error().code) << "); retry "
              << (attempt + 1) << "/" << retry.maxRetries << " in " << delayMs << "ms";
          // Per-retry timer (Kartend audit vrqzk): a single shared timer made
          // two concurrent lookups' retries clobber each other (stop() +
          // disconnect() dropped the prior schedule), losing the dropped
          // retry's callback and leaking its batch item. Each retry owns a
          // timer, tracked in m_inFlightRetryTimers so the destructor stops +
          // deletes it (the lambda captures `this` raw, so it must not fire
          // after free). Uses the timer as its own connection context — the
          // provider is not a QObject, so it can't serve as one.
          auto *retryTimer = new QTimer();
          retryTimer->setSingleShot(true);
          m_inFlightRetryTimers.insert(retryTimer);
          QObject::connect(retryTimer, &QTimer::timeout, retryTimer,
                           [this, retryTimer, headers, url, onResponse = std::move(onResponse),
                            maxResponseBytes, requiredContentTypePrefix, allowedHostSuffixes, retry,
                            attempt]() mutable {
                             m_inFlightRetryTimers.remove(retryTimer);
                             retryTimer->deleteLater();
                             dispatchWithRetry(headers, url, std::move(onResponse),
                                               maxResponseBytes, requiredContentTypePrefix,
                                               allowedHostSuffixes, retry, attempt + 1);
                           });
          retryTimer->start(delayMs);
          return;
        }
        onResponse(std::move(response));
      };
  if (s_testFetch) {
    s_testFetch(url, headers, std::move(wrapped), allowedHostSuffixes);
    return;
  }
  Scraper::HttpClient::instance()->get(url, headers, std::move(wrapped), maxResponseBytes,
                                       requiredContentTypePrefix, allowedHostSuffixes);
}
