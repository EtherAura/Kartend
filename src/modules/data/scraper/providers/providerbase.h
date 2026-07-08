#ifndef PROVIDERBASE_H
#define PROVIDERBASE_H

#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "errorutils.h"
#include "httpclient.h"
#include "metadatalookupprovider.h"
#include "scraperretrypolicy.h"

class QTimer;

/// Tuning knobs for ProviderBase's shared bounded transient-retry loop
/// (Kartend-jjyst.10, hoisted from ScreenScraperProvider's Kartend-1rtrt
/// jeuInfos loop). Defaults mirror Scraper::RetryPolicy's constants; the
/// transient classification itself is NOT a knob — it stays the shared
/// RetryPolicy::isTransient (timeouts, 5xx, SS's 423, a Retry-After'd 429;
/// never a deliberate local cancel / queue clear) so every provider inherits
/// the same semantics settled by Kartend-jjyst.2/.3. Lives at namespace
/// scope (not nested in ProviderBase) because GCC rejects `= {}` default
/// arguments of a nested class with NSDMIs while the enclosing class is
/// still incomplete; ProviderBase::RetryOptions remains valid via an alias.
struct ProviderRetryOptions {
  int maxRetries = Scraper::RetryPolicy::kDefaultMaxRetries;
  int baseDelayMs = Scraper::RetryPolicy::kDefaultBaseDelayMs;
  int maxDelayMs = Scraper::RetryPolicy::kDefaultMaxDelayMs;
};

/// Shared scaffolding for the API-backed MetadataLookupProvider
/// implementations (ScreenScraper, TMDB, MusicBrainz, Open Library).
///
/// Collapses the boilerplate every provider repeated: the Kartend
/// User-Agent string/header, per-host throttle registration, and the
/// GET -> isError -> parse -> callback chain (plus the image/* MIME
/// guard on media fetches). Stays abstract — subclasses still supply the
/// MetadataProvider / MetadataLookupProvider interface (id, lookup, …).
class ProviderBase : public MetadataLookupProvider {
public:
  /// Stops + deletes every pending transient-retry timer (bounded-teardown
  /// convention): the retry lambdas capture `this` raw, so none may fire
  /// after the provider is gone. A retry that already fired removed itself
  /// in its lambda, so it isn't in the set.
  ~ProviderBase() override;

  /// User-Agent every Kartend request sends: app version (compile-time
  /// APP_VERSION) plus the project URL. MusicBrainz rejects requests
  /// without a contact-bearing UA; the others accept it for a consistent
  /// audit trail. Public + static so the provider-adjacent free helpers
  /// (ScreenScraper's ssuserInfos / ssinfraInfos probes, which are not
  /// members) can reuse it without re-stating the literal.
  [[nodiscard]] static QString userAgent();
  /// {User-Agent: userAgent()} header list, ready for HttpClient::get.
  [[nodiscard]] static Scraper::HttpClient::RawHeaders userAgentHeader();

  /// Test seam (Kartend-ym7z3): a fetch function that stands in for
  /// HttpClient::instance()->get(). When set, getJson / getWithRetry /
  /// getImageBytes route every request through it instead of the live
  /// singleton (the retry loop sits ABOVE the seam, so each retry attempt
  /// is a separate captured request — tests can count dispatches), so
  /// provider-orchestration tests can capture the outbound URL/headers
  /// and answer with canned bytes — no QNetworkAccessManager, no
  /// network. The HttpClient-side response guards (size cap, image/*
  /// Content-Type pin) are intentionally bypassed under the hook; they
  /// have their own coverage in test_httpclient.cpp.
  ///
  /// Pass {} to restore live behaviour. Not thread-safe — set it from
  /// the test's init/cleanup on the main thread, never while a request
  /// is in flight. Production code must never call this.
  using TestFetchFunction = std::function<void(
      const QUrl &url, const Scraper::HttpClient::RawHeaders &headers,
      Scraper::HttpClient::ResponseCallback callback, const QStringList &allowedHostSuffixes)>;
  static void setFetchFunctionForTesting(TestFetchFunction fetch);

protected:
  /// Register per-host request throttles on the shared HttpClient.
  /// Idempotent — setRateLimit overwrites the entry — so re-running it on
  /// a settings change is safe. Each pair is {host, minIntervalMs}.
  static void registerThrottles(std::initializer_list<std::pair<const char *, int>> hostLimits);

  /// See ProviderRetryOptions at namespace scope for the docs (and why it
  /// isn't nested here).
  using RetryOptions = ProviderRetryOptions;

  /// GET @p url with @p headers; route a transport-level error straight
  /// to @p callback, otherwise run @p parse on the body bytes and deliver
  /// its Result. Collapses the identical
  ///   if (response.isError()) { callback(response.error()); return; }
  ///   callback(parse(response.value()));
  /// tail every lookup() / fetchDetail() carried. @p T is the parsed
  /// payload type (candidate list or scraped item); pass it explicitly.
  ///
  /// Routes through getWithRetry, so a transient failure (timeout / 5xx /
  /// Retry-After'd 429) is re-attempted a bounded number of times before the
  /// error reaches @p callback — one blip 503 no longer permanently errors a
  /// TMDB/MusicBrainz/OpenLibrary lookup or detail fetch (Kartend-jjyst.10).
  template <typename T>
  void getJson(const Scraper::HttpClient::RawHeaders &headers, const QUrl &url,
               std::function<ErrorUtils::Result<T>(const QByteArray &)> parse,
               std::function<void(ErrorUtils::Result<T>)> callback) {
    getWithRetry(headers, url,
                 [parse = std::move(parse),
                  callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
                   if (response.isError()) {
                     callback(response.error());
                     return;
                   }
                   callback(parse(response.value()));
                 });
  }

  /// GET @p url with bounded retry of transient failures: on a timeout /
  /// 5xx / SS 423 / Retry-After'd 429 (RetryPolicy::isTransient) the same
  /// request is re-dispatched after RetryPolicy::retryDelayMs, up to
  /// @p retry.maxRetries extra attempts; the final Result — success or the
  /// last error — is delivered to @p onResponse exactly once. Only safe for
  /// idempotent GETs, which is all the providers issue. A deliberate local
  /// cancel (RequestQueueCleared from HttpClient::clearPending) is never
  /// retried — the policy excludes it so a batch cancel can't re-issue
  /// requests for a killed run (Kartend-jjyst.2). Retries are paced by
  /// per-retry timers tracked in m_inFlightRetryTimers so ~ProviderBase can
  /// tear them down. The extra parameters mirror HttpClient::get; the
  /// defaults reproduce the plain getJson dispatch.
  void getWithRetry(const Scraper::HttpClient::RawHeaders &headers, const QUrl &url,
                    Scraper::HttpClient::ResponseCallback onResponse,
                    qint64 maxResponseBytes = Scraper::HttpClient::kDefaultMaxResponseBytes,
                    const QString &requiredContentTypePrefix = QString(),
                    const QStringList &allowedHostSuffixes = {}, RetryOptions retry = {});

  /// GET an image asset, pinning the response to image/* (the 9ryx
  /// guard): cover / fanart hosts occasionally answer 200-OK with an HTML
  /// error page, which must surface as a structured error rather than
  /// reach the image decoder.
  ///
  /// @p allowedHostSuffixes pins the fetch — and every redirect it follows —
  /// to the caller's trusted media host(s) (SSRF defence, Kartend audit
  /// faz4r). It is required, not defaulted: a media URL is fetched from an
  /// upstream treated as untrusted, which can answer with a 3xx to an
  /// internal host; without a non-empty allowlist HttpClient runs under
  /// NoLessSafeRedirectPolicy and auto-follows it. Pass the provider's own
  /// image host plus any legitimate redirect target (e.g. coverartarchive.org
  /// redirects covers to archive.org).
  static void getImageBytes(const Scraper::HttpClient::RawHeaders &headers, const QUrl &url,
                            MediaCallback callback, const QStringList &allowedHostSuffixes) {
    if (s_testFetch) {
      s_testFetch(url, headers, std::move(callback), allowedHostSuffixes);
      return;
    }
    // Image-sized response cap: media fetches fan out concurrently and each
    // reply is buffered whole, so the tighter per-request bound (not the wide
    // default meant for video/manual payloads) limits how much RAM a hostile
    // or misconfigured image CDN can pin before the abort trips.
    Scraper::HttpClient::instance()->get(url, headers, std::move(callback),
                                         Scraper::HttpClient::kImageMaxResponseBytes,
                                         QStringLiteral("image/"), allowedHostSuffixes);
  }

  /// Liveness token (Kartend audit cr950 class). A provider's async HTTP
  /// continuations — getWithRetry's response lambda, ScreenScraper's catalog
  /// and jeuInfos handlers — capture raw `this` and are held by the
  /// qApp-lifetime HttpClient with NO QObject connection to sever on teardown,
  /// so they can outlive the provider (a batch cancel destroys it while a
  /// reply is in flight). Such continuations capture weak_ptr(m_lifetimeToken)
  /// and, when it has expired, must not touch any member. Protected so
  /// subclasses guard their own continuations with the same token; it expires
  /// automatically when the provider is destroyed.
  std::shared_ptr<int> m_lifetimeToken = std::make_shared<int>(0);

private:
  /// One attempt of the getWithRetry loop: dispatch the GET (through the
  /// test seam when armed), classify a failure via RetryPolicy::isTransient,
  /// and either schedule attempt+1 on a tracked single-shot timer or deliver
  /// the final Result to @p onResponse. @p attempt is 0 on the initial call.
  void dispatchWithRetry(const Scraper::HttpClient::RawHeaders &headers, const QUrl &url,
                         Scraper::HttpClient::ResponseCallback onResponse, qint64 maxResponseBytes,
                         const QString &requiredContentTypePrefix,
                         const QStringList &allowedHostSuffixes, RetryOptions retry, int attempt);

  /// In-flight single-shot timers driving the transient-failure retries, one
  /// per scheduled retry (a shared timer would let two concurrent lookups'
  /// retries clobber each other and drop a callback — Kartend audit vrqzk).
  /// Each timer's lambda captures `this` raw; ~ProviderBase stops + deletes
  /// every entry so none fires after free.
  QSet<QTimer *> m_inFlightRetryTimers;

  /// Backing store for the Kartend-ym7z3 test seam. Null in production
  /// (the default), so getJson/getImageBytes hit the live HttpClient
  /// singleton. Defined in providerbase.cpp.
  static TestFetchFunction s_testFetch;
};

#endif // PROVIDERBASE_H
