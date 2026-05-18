// Concrete MusicBrainz provider. Wires the pure parser layer to the
// async HttpClient with the User-Agent header MB requires and the
// 1 req/sec rate limit MB enforces.
#include "musicbrainzprovider.h"

#include <utility>

#include <QUrlQuery>

#include "httpclient.h"
#include "musicbrainzparser.h"

namespace {

constexpr const char *MB_API_BASE = "https://musicbrainz.org/ws/2/release/";
constexpr const char *MB_HOST = "musicbrainz.org";
constexpr const char *COVER_ART_HOST = "coverartarchive.org";
constexpr int MB_RATE_LIMIT_MS = 1000; // MB asks for 1 req/sec per IP.

constexpr int SEARCH_LIMIT = 10;

QString userAgent() {
  // MB rejects requests without a meaningful User-Agent that includes
  // app name, version, and a contact URL. APP_NAME/APP_VERSION are
  // injected at compile time via target_compile_definitions in the
  // top-level CMakeLists.txt.
  return QStringLiteral("Kartend/%1 (https://github.com/EtherAura/Kartend)")
      .arg(QString::fromLatin1(APP_VERSION));
}

void registerHostThrottles() {
  // Idempotent — HttpClient::setRateLimit just overwrites the entry
  // each time. MusicBrainz is the strict 1/sec; Cover Art Archive
  // (different host) has no documented rate limit but we still pace
  // it modestly so we don't accidentally hammer it during a batch
  // scrape.
  Scraper::HttpClient *client = Scraper::HttpClient::instance();
  client->setRateLimit(QString::fromLatin1(MB_HOST), MB_RATE_LIMIT_MS);
  client->setRateLimit(QString::fromLatin1(COVER_ART_HOST), 250);
}

} // namespace

MusicBrainzProvider::MusicBrainzProvider() {
  registerHostThrottles();
}

QUrl MusicBrainzProvider::searchUrl(const QString &query) const {
  if (query.trimmed().isEmpty()) {
    return {};
  }
  // The web URL is the public search page, not the API endpoint —
  // matches what the URL-only WebSearchProvider used before this
  // provider replaced it in the registry.
  const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query.trimmed()));
  return QUrl(QStringLiteral("https://musicbrainz.org/search?query=%1&type=release").arg(encoded));
}

void MusicBrainzProvider::lookup(const QString &query, LookupCallback callback) {
  if (!callback) {
    return;
  }
  const QString trimmed = query.trimmed();
  if (trimmed.isEmpty()) {
    callback(QList<Scraper::ScrapeCandidate>{});
    return;
  }

  QUrl url(QString::fromLatin1(MB_API_BASE));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("query"), trimmed);
  q.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
  q.addQueryItem(QStringLiteral("limit"), QString::number(SEARCH_LIMIT));
  url.setQuery(q);

  Scraper::HttpClient::instance()->get(
      url, userAgent(), [callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(response.error());
          return;
        }
        callback(MusicBrainzParser::parseSearchResponse(response.value()));
      });
}

void MusicBrainzProvider::fetchDetail(const Scraper::ScrapeCandidate &candidate,
                                      DetailCallback callback) {
  if (!callback) {
    return;
  }
  if (candidate.providerSpecificId.isEmpty()) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "MusicBrainz candidate missing MBID",
                                             "MusicBrainzProvider::fetchDetail"));
    return;
  }

  QUrl url(QString::fromLatin1(MB_API_BASE) + candidate.providerSpecificId);
  QUrlQuery q;
  // ?inc covers everything the parser reads. Order doesn't matter to
  // MB; the join chars are `+` per their convention.
  q.addQueryItem(QStringLiteral("inc"),
                 QStringLiteral("artists+labels+release-groups+tags+genres+annotation"));
  q.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
  url.setQuery(q);

  Scraper::HttpClient::instance()->get(
      url, userAgent(), [callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(response.error());
          return;
        }
        callback(MusicBrainzParser::parseDetailResponse(response.value()));
      });
}

void MusicBrainzProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  if (!callback) {
    return;
  }
  // Cover Art Archive doesn't require a User-Agent but accepting one
  // keeps the audit trail consistent with the MB API requests.
  Scraper::HttpClient::instance()->get(url, userAgent(), std::move(callback));
}
