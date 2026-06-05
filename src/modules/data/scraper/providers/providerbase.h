#ifndef PROVIDERBASE_H
#define PROVIDERBASE_H

#include <functional>
#include <initializer_list>
#include <utility>

#include <QByteArray>
#include <QString>
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
    Scraper::HttpClient::instance()->get(url, headers,
                                         [parse = std::move(parse), callback = std::move(callback)](
                                             ErrorUtils::Result<QByteArray> response) {
                                           if (response.isError()) {
                                             callback(response.error());
                                             return;
                                           }
                                           callback(parse(response.value()));
                                         });
  }

  /// GET an image asset, pinning the response to image/* (the 9ryx
  /// guard): cover / fanart hosts occasionally answer 200-OK with an HTML
  /// error page, which must surface as a structured error rather than
  /// reach the image decoder.
  static void getImageBytes(const Scraper::HttpClient::RawHeaders &headers, const QUrl &url,
                            MediaCallback callback) {
    Scraper::HttpClient::instance()->get(url, headers, std::move(callback),
                                         Scraper::HttpClient::kDefaultMaxResponseBytes,
                                         QStringLiteral("image/"));
  }
};

#endif // PROVIDERBASE_H
