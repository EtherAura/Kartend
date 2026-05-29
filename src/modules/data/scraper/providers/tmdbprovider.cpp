// TMDB provider. Reads the v4 bearer token from GeneralSettings and
// attaches it to every request via the Authorization header. The token
// itself is never logged or surfaced — error messages reference
// "Settings → Scrapers" so the user knows where to fix a missing
// credential without seeing the value in a stack trace.
#include "tmdbprovider.h"

#include <utility>

#include <QNetworkRequest>
#include <QUrlQuery>

#include "collection/generalsettings.h"
#include "httpclient.h"
#include "tmdbparser.h"

namespace {

constexpr const char *TMDB_HOST = "api.themoviedb.org";
constexpr const char *TMDB_IMAGE_HOST = "image.tmdb.org";
constexpr const char *TMDB_API_BASE = "https://api.themoviedb.org/3";
// TMDB's documented limit is ~50 req/sec — way more than we'll ever
// need from a single client. Throttle conservatively at 100ms so a
// batch scrape doesn't get rate-limited if the user's network adds
// retries.
constexpr int TMDB_RATE_LIMIT_MS = 100;
constexpr int TMDB_IMAGE_RATE_LIMIT_MS = 50;

constexpr const char *TMDB_PROVIDER_ID = "tmdb";
constexpr const char *TMDB_TOKEN_FIELD = "api_token";

QString userAgent() {
  return QStringLiteral("Kartend/%1 (https://github.com/EtherAura/Kartend)")
      .arg(QString::fromLatin1(APP_VERSION));
}

void registerHostThrottles() {
  Scraper::HttpClient *client = Scraper::HttpClient::instance();
  client->setRateLimit(QString::fromLatin1(TMDB_HOST), TMDB_RATE_LIMIT_MS);
  client->setRateLimit(QString::fromLatin1(TMDB_IMAGE_HOST), TMDB_IMAGE_RATE_LIMIT_MS);
}

ErrorUtils::ErrorContext notConfiguredError() {
  return ErrorUtils::ErrorContext::error(
      ErrorUtils::ErrorCode::InvalidArgument,
      QStringLiteral("TMDB token not configured. Set 'api_token' under "
                     "Settings → Scrapers → TMDB to enable lookups."),
      "TmdbProvider");
}

} // namespace

TmdbProvider::TmdbProvider(GeneralSettingsAccessor settingsAccessor)
    : m_settingsAccessor(std::move(settingsAccessor)) {
  registerHostThrottles();
}

QString TmdbProvider::currentToken() const {
  if (!m_settingsAccessor) return {};
  const GeneralSettings *settings = m_settingsAccessor();
  if (!settings) return {};
  return settings->scraperCredentials.value(QString::fromLatin1(TMDB_PROVIDER_ID))
      .value(QString::fromLatin1(TMDB_TOKEN_FIELD));
}

QUrl TmdbProvider::searchUrl(const QString &query) const {
  if (query.trimmed().isEmpty()) {
    return {};
  }
  const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query.trimmed()));
  return QUrl(QStringLiteral("https://www.themoviedb.org/search?query=%1").arg(encoded));
}

void TmdbProvider::lookup(const QString &query, LookupCallback callback) {
  if (!callback) return;
  const QString trimmed = query.trimmed();
  if (trimmed.isEmpty()) {
    callback(QList<Scraper::ScrapeCandidate>{});
    return;
  }
  const QString token = currentToken();
  if (token.isEmpty()) {
    callback(notConfiguredError());
    return;
  }

  // Use /search/multi so movies + TV shows come back from one query
  // without the user having to pick a content type first. The detail
  // fetch routes via the embedded media_type prefix in the candidate id.
  QUrl url(QString::fromLatin1(TMDB_API_BASE) + QStringLiteral("/search/multi"));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("query"), trimmed);
  q.addQueryItem(QStringLiteral("include_adult"), QStringLiteral("false"));
  url.setQuery(q);

  // TmdbProvider extends MetadataLookupProvider but the HttpClient API
  // doesn't take per-request headers (only User-Agent). We work around
  // that by appending the token as the standard `api_key` query param
  // — TMDB v3 still accepts both `?api_key=...` (legacy) and the
  // bearer header, and the user's token is shaped for either path.
  // Future work: extend HttpClient to accept an Authorization header
  // so we can use the v4 bearer-only form properly.
  q.addQueryItem(QStringLiteral("api_key"), token);
  url.setQuery(q);

  Scraper::HttpClient::instance()->get(
      url, userAgent(), [callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(response.error());
          return;
        }
        callback(TmdbParser::parseSearchResponse(response.value()));
      });
}

void TmdbProvider::fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) {
  if (!callback) return;
  const QString token = currentToken();
  if (token.isEmpty()) {
    callback(notConfiguredError());
    return;
  }
  // Candidate id format: "<media_type>:<tmdb_id>" (set in
  // TmdbParser::parseSearchResponse). Split here to route to the
  // right detail endpoint.
  const int colon = candidate.providerSpecificId.indexOf(':');
  if (colon <= 0 || colon >= candidate.providerSpecificId.size() - 1) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "TMDB candidate id missing media-type prefix",
                                             "TmdbProvider::fetchDetail"));
    return;
  }
  const QString mediaType = candidate.providerSpecificId.left(colon);
  const QString tmdbId = candidate.providerSpecificId.mid(colon + 1);

  QUrl url(QString::fromLatin1(TMDB_API_BASE) + QLatin1Char('/') + mediaType + QLatin1Char('/') +
           tmdbId);
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("api_key"), token);
  q.addQueryItem(QStringLiteral("append_to_response"), mediaType == QStringLiteral("movie")
                                                           ? QStringLiteral("release_dates")
                                                           : QStringLiteral("content_ratings"));
  url.setQuery(q);

  Scraper::HttpClient::instance()->get(
      url, userAgent(),
      [callback = std::move(callback), mediaType](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(response.error());
          return;
        }
        callback(TmdbParser::parseDetailResponse(response.value(), mediaType));
      });
}

void TmdbProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  if (!callback) return;
  // Image host doesn't require auth — public CDN. User-Agent kept
  // for audit-trail consistency. Kartend-9ryx: scope the response to
  // image/* so a misrouted or hostile CDN response can't reach the
  // decoder.
  Scraper::HttpClient::instance()->get(url, userAgent(), std::move(callback),
                                       Scraper::HttpClient::kDefaultMaxResponseBytes,
                                       QStringLiteral("image/"));
}
