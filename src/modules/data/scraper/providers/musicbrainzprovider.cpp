// Concrete MusicBrainz provider. Wires the pure parser layer to the
// async HttpClient with the User-Agent header MB requires and the
// 1 req/sec rate limit MB enforces.
#include "musicbrainzprovider.h"

#include <utility>

#include <QUrlQuery>

#include "musicbrainzparser.h"

namespace {

constexpr const char *MB_API_BASE = "https://musicbrainz.org/ws/2/release/";
constexpr const char *MB_HOST = "musicbrainz.org";
constexpr const char *COVER_ART_HOST = "coverartarchive.org";
constexpr int MB_RATE_LIMIT_MS = 1000; // MB asks for 1 req/sec per IP.
// Cover Art Archive (separate host) has no documented rate limit; pace
// it modestly so a batch scrape doesn't accidentally hammer it.
constexpr int COVER_ART_RATE_LIMIT_MS = 250;

constexpr int SEARCH_LIMIT = 10;

} // namespace

MusicBrainzProvider::MusicBrainzProvider() {
  registerThrottles({{MB_HOST, MB_RATE_LIMIT_MS}, {COVER_ART_HOST, COVER_ART_RATE_LIMIT_MS}});
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

  getJson<QList<Scraper::ScrapeCandidate>>(
      userAgentHeader(), url,
      [](const QByteArray &body) { return MusicBrainzParser::parseSearchResponse(body); },
      std::move(callback));
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

  getJson<Scraper::ScrapedItem>(
      userAgentHeader(), url,
      [](const QByteArray &body) { return MusicBrainzParser::parseDetailResponse(body); },
      std::move(callback));
}

void MusicBrainzProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  if (!callback) {
    return;
  }
  // Cover Art Archive doesn't require a User-Agent but accepting one
  // keeps the audit trail consistent with the MB API requests. The
  // image/* guard in getImageBytes matters here: CAA occasionally
  // returns HTML error pages on 502/503. Pin the fetch to CAA and to
  // archive.org — CAA legitimately 307-redirects cover requests to its
  // archive.org storage backend — so a hostile/MITM'd upstream can't
  // 3xx-redirect it to an internal host (SSRF, Kartend audit faz4r).
  getImageBytes(userAgentHeader(), url, std::move(callback),
                {QString::fromLatin1(COVER_ART_HOST), QStringLiteral("archive.org")});
}
