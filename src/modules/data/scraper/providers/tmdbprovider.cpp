// TMDB provider. Reads the v4 bearer token from GeneralSettings and
// attaches it to every request via the Authorization header. The token
// itself is never logged or surfaced — error messages reference
// "Settings → Scrapers" so the user knows where to fix a missing
// credential without seeing the value in a stack trace.
#include "tmdbprovider.h"

#include <utility>

#include <QRegularExpression>
#include <QUrlQuery>

#include "collection/collectionconfig.h"
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

// v4 read-access token rides in the Authorization header, never the
// query string: TMDB's v3 endpoints accept the bearer token this way,
// and a header keeps the secret out of the logs / Referer / proxy
// access logs that a `?api_key=` query param would expose (Kartend-0gp7).
Scraper::HttpClient::RawHeaders authHeaders(const QString &token) {
  Scraper::HttpClient::RawHeaders headers = ProviderBase::userAgentHeader();
  headers.append(
      {QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8()});
  return headers;
}

ErrorUtils::ErrorContext notConfiguredError() {
  return ErrorUtils::ErrorContext::error(
      ErrorUtils::ErrorCode::InvalidArgument,
      QStringLiteral("TMDB token not configured. Set 'api_token' under "
                     "Settings → Scrapers → TMDB to enable lookups."),
      "TmdbProvider");
}

} // namespace

TmdbProvider::TmdbProvider(GeneralSettingsAccessor settingsAccessor,
                           CollectionAccessor collectionAccessor)
    : m_settingsAccessor(std::move(settingsAccessor)),
      m_collectionAccessor(std::move(collectionAccessor)) {
  registerThrottles({{TMDB_HOST, TMDB_RATE_LIMIT_MS}, {TMDB_IMAGE_HOST, TMDB_IMAGE_RATE_LIMIT_MS}});
}

QString TmdbProvider::currentToken() const {
  if (!m_settingsAccessor) return {};
  const GeneralSettings *settings = m_settingsAccessor();
  if (!settings) return {};
  return settings->scraper.credentials.value(QString::fromLatin1(TMDB_PROVIDER_ID))
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

  getJson<QList<Scraper::ScrapeCandidate>>(
      authHeaders(token), url,
      [](const QByteArray &body) { return TmdbParser::parseSearchResponse(body); },
      std::move(callback));
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

  // Defense-in-depth (Kartend-tjyh): ScrapeCandidate treats providerSpecificId
  // as opaque, so re-validate the two path components before interpolating
  // them into the request URL. The search parser already constrains these, but
  // this guard keeps a future candidate source from injecting `../`/`?`/`#`.
  static const QRegularExpression digits(
      QRegularExpression::anchoredPattern(QStringLiteral("[0-9]+")));
  if ((mediaType != QStringLiteral("movie") && mediaType != QStringLiteral("tv")) ||
      !digits.match(tmdbId).hasMatch()) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "TMDB candidate id has an invalid media type or id",
                                             "TmdbProvider::fetchDetail"));
    return;
  }

  QUrl url(QString::fromLatin1(TMDB_API_BASE) + QLatin1Char('/') + mediaType + QLatin1Char('/') +
           tmdbId);
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("append_to_response"), mediaType == QStringLiteral("movie")
                                                           ? QStringLiteral("release_dates")
                                                           : QStringLiteral("content_ratings"));
  url.setQuery(q);

  getJson<Scraper::ScrapedItem>(
      authHeaders(token), url,
      [mediaType](const QByteArray &body) {
        return TmdbParser::parseDetailResponse(body, mediaType);
      },
      std::move(callback));
}

void TmdbProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  if (!callback) return;
  // Image host doesn't require auth — public CDN. User-Agent kept for
  // audit-trail consistency. getImageBytes scopes the response to image/*
  // so a misrouted or hostile CDN response can't reach the decoder, and pins
  // the fetch + any redirect to image.tmdb.org so a hostile/MITM'd CDN can't
  // 3xx-redirect it to an internal host (SSRF, Kartend audit faz4r).
  getImageBytes(userAgentHeader(), url, std::move(callback),
                {QString::fromLatin1(TMDB_IMAGE_HOST)});
}

void TmdbProvider::fetchEntity(const Scraper::EntityScrapeTarget &target, DetailCallback callback) {
  if (!callback) return;
  if (target.type != Scraper::ScrapeEntityType::Collection) {
    callback(ErrorUtils::ErrorContext::error(
        ErrorUtils::ErrorCode::InvalidArgument,
        QStringLiteral("TMDB entity scraping only supports Collection (got type %1)")
            .arg(static_cast<int>(target.type)),
        "TmdbProvider::fetchEntity"));
    return;
  }
  const QString token = currentToken();
  if (token.isEmpty()) {
    callback(notConfiguredError());
    return;
  }
  // The collection NAME is what TMDB searches on (the target's identity is the
  // collection uuid, which TMDB doesn't know). Resolve it from the live config.
  const CollectionConfig *cfg = m_collectionAccessor ? m_collectionAccessor() : nullptr;
  const QString name = cfg ? cfg->name.trimmed() : QString();
  if (name.isEmpty()) {
    callback(ErrorUtils::ErrorContext::error(
        ErrorUtils::ErrorCode::InvalidArgument,
        QStringLiteral("Cannot scrape collection art without a collection name"),
        "TmdbProvider::fetchEntity"));
    return;
  }

  QUrl url(QString::fromLatin1(TMDB_API_BASE) + QStringLiteral("/search/collection"));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("query"), name);
  url.setQuery(q);

  const QString scopeKey = target.identity; // the owning collection uuid
  getJson<Scraper::ScrapedItem>(
      authHeaders(token), url,
      [](const QByteArray &body) { return TmdbParser::parseCollectionSearchResponse(body); },
      [callback = std::move(callback), scopeKey, name](ErrorUtils::Result<Scraper::ScrapedItem> r) {
        if (r.isError()) {
          callback(r.error());
          return;
        }
        Scraper::ScrapedItem item = r.value();
        if (item.media.isEmpty()) {
          // Niche collections legitimately have no TMDB match — a routine
          // not-found (re-queueable, Kartend-e8aag bucketing), not an error.
          callback(ErrorUtils::ErrorContext::error(
              ErrorUtils::ErrorCode::RemoteResourceNotFound,
              QStringLiteral("No TMDB collection matched \"%1\"").arg(name),
              "TmdbProvider::fetchEntity"));
          return;
        }
        item.sourceProviderId = QString::fromLatin1(TMDB_PROVIDER_ID);
        // The parser leaves scopeKey empty — stamp the owning collection uuid so
        // the persistence sink writes `_shared/<type>/collection_<uuid>.<ext>`.
        for (Scraper::MediaAsset &asset : item.media) {
          asset.scopeKey = scopeKey;
        }
        callback(item);
      });
}
