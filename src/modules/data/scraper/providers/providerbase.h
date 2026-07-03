#ifndef PROVIDERBASE_H
#define PROVIDERBASE_H

#include <functional>
#include <initializer_list>
#include <utility>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "errorutils.h"
#include "httpclient.h"
#include "metadatalookupprovider.h"

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
  /// HttpClient::instance()->get(). When set, getJson / getImageBytes
  /// route every request through it instead of the live singleton, so
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

  /// GET @p url with @p headers; route a transport-level error straight
  /// to @p callback, otherwise run @p parse on the body bytes and deliver
  /// its Result. Collapses the identical
  ///   if (response.isError()) { callback(response.error()); return; }
  ///   callback(parse(response.value()));
  /// tail every lookup() / fetchDetail() carried. @p T is the parsed
  /// payload type (candidate list or scraped item); pass it explicitly.
  template <typename T>
  static void getJson(const Scraper::HttpClient::RawHeaders &headers, const QUrl &url,
                      std::function<ErrorUtils::Result<T>(const QByteArray &)> parse,
                      std::function<void(ErrorUtils::Result<T>)> callback) {
    Scraper::HttpClient::ResponseCallback onResponse =
        [parse = std::move(parse),
         callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
          if (response.isError()) {
            callback(response.error());
            return;
          }
          callback(parse(response.value()));
        };
    if (s_testFetch) {
      s_testFetch(url, headers, std::move(onResponse), {});
      return;
    }
    Scraper::HttpClient::instance()->get(url, headers, std::move(onResponse));
  }

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

private:
  /// Backing store for the Kartend-ym7z3 test seam. Null in production
  /// (the default), so getJson/getImageBytes hit the live HttpClient
  /// singleton. Defined in providerbase.cpp.
  static TestFetchFunction s_testFetch;
};

#endif // PROVIDERBASE_H
