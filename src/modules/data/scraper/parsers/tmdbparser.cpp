// JSON parsers for TMDB responses. Pure data shaping; the provider
// layer wires these to the HttpClient with the bearer token in the
// Authorization header.
#include "tmdbparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace TmdbParser {

namespace {

constexpr const char *IMAGE_BASE = "https://image.tmdb.org/t/p/w500";

QString joinNamed(const QJsonArray &arr, const char *key,
                  const QString &sep = QStringLiteral(", ")) {
  QStringList parts;
  for (const auto &v : arr) {
    const QString name = v.toObject().value(QString::fromLatin1(key)).toString();
    if (!name.isEmpty() && !parts.contains(name)) {
      parts.append(name);
    }
  }
  return parts.join(sep);
}

QString readContentRating(const QJsonObject &detail, const QString &mediaType) {
  // For movies: release_dates.results[].release_dates[].certification
  // For TV: content_ratings.results[].rating
  // We pick the US entry first (most-likely to be a recognised
  // rating string) then fall back to the first available.
  if (mediaType == QStringLiteral("movie")) {
    const auto results = detail.value("release_dates").toObject().value("results").toArray();
    QString fallback;
    for (const auto &v : results) {
      const QJsonObject country = v.toObject();
      const QString iso = country.value("iso_3166_1").toString();
      const auto dates = country.value("release_dates").toArray();
      for (const auto &d : dates) {
        const QString cert = d.toObject().value("certification").toString().trimmed();
        if (cert.isEmpty()) continue;
        if (iso == QStringLiteral("US")) {
          return cert;
        }
        if (fallback.isEmpty()) {
          fallback = cert;
        }
      }
    }
    return fallback;
  }
  if (mediaType == QStringLiteral("tv")) {
    const auto results = detail.value("content_ratings").toObject().value("results").toArray();
    QString fallback;
    for (const auto &v : results) {
      const QJsonObject c = v.toObject();
      const QString rating = c.value("rating").toString().trimmed();
      if (rating.isEmpty()) continue;
      if (c.value("iso_3166_1").toString() == QStringLiteral("US")) {
        return rating;
      }
      if (fallback.isEmpty()) {
        fallback = rating;
      }
    }
    return fallback;
  }
  return {};
}

} // namespace

ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> parseSearchResponse(const QByteArray &json) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "TMDB search response is not valid JSON",
                               "TmdbParser::parseSearchResponse")
        .withDetails(err.errorString());
  }
  const QJsonArray results = doc.object().value("results").toArray();

  QList<Scraper::ScrapeCandidate> out;
  out.reserve(results.size());
  for (const auto &v : results) {
    const QJsonObject r = v.toObject();
    const QString mediaType = r.value("media_type").toString();
    // /search/multi also returns "person" rows — those have no media
    // worth scraping into Kartend, so skip.
    if (mediaType != QStringLiteral("movie") && mediaType != QStringLiteral("tv")) {
      continue;
    }
    Scraper::ScrapeCandidate c;
    const qint64 tmdbId = static_cast<qint64>(r.value("id").toDouble(0));
    if (tmdbId <= 0) {
      continue;
    }
    // Prefix the id with the media type so the detail fetcher knows
    // which endpoint to hit without a second sniff. ScrapeCandidate
    // treats providerSpecificId as opaque so this is fine.
    c.providerSpecificId = mediaType + QLatin1Char(':') + QString::number(tmdbId);
    c.displayName = mediaType == QStringLiteral("movie") ? r.value("title").toString()
                                                         : r.value("name").toString();
    QStringList sub;
    sub.append(mediaType == QStringLiteral("movie") ? QStringLiteral("Movie")
                                                    : QStringLiteral("TV"));
    const QString date = mediaType == QStringLiteral("movie")
                             ? r.value("release_date").toString()
                             : r.value("first_air_date").toString();
    if (!date.isEmpty()) {
      sub.append(date.left(4));
    }
    c.subtitle = sub.join(QStringLiteral(" · "));
    // TMDB returns vote_average (0–10) instead of a discrete match
    // score; map to 0–100 so the dialog's score column reads
    // consistently across providers.
    c.matchScore = static_cast<int>(r.value("vote_average").toDouble(-1.0) * 10);
    const QString posterPath = r.value("poster_path").toString();
    if (!posterPath.isEmpty()) {
      c.thumbnailUrl = QUrl(QStringLiteral("https://image.tmdb.org/t/p/w185") + posterPath);
    }
    out.append(c);
  }
  return out;
}

ErrorUtils::Result<Scraper::ScrapedItem> parseDetailResponse(const QByteArray &json,
                                                             const QString &mediaType) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "TMDB detail response is not valid JSON",
                               "TmdbParser::parseDetailResponse")
        .withDetails(err.errorString());
  }
  const QJsonObject detail = doc.object();
  Scraper::ScrapedItem item;
  item.sourceProviderId = QStringLiteral("tmdb");

  item.title = mediaType == QStringLiteral("movie") ? detail.value("title").toString()
                                                    : detail.value("name").toString();
  item.description = detail.value("overview").toString();
  item.genre = joinNamed(detail.value("genres").toArray(), "name");
  item.releaseDate = mediaType == QStringLiteral("movie")
                         ? detail.value("release_date").toString()
                         : detail.value("first_air_date").toString();
  item.publisher = joinNamed(detail.value("production_companies").toArray(), "name");
  item.contentRating = readContentRating(detail, mediaType);

  if (mediaType == QStringLiteral("movie")) {
    const int runtimeMin = detail.value("runtime").toInt(0);
    if (runtimeMin > 0) {
      item.runtimeSeconds = runtimeMin * 60;
    }
  } else {
    // TV: episode_run_time is an array of typical episode runtimes;
    // first entry is the canonical "episode length".
    const auto runs = detail.value("episode_run_time").toArray();
    if (!runs.isEmpty()) {
      const int runtimeMin = runs.first().toInt(0);
      if (runtimeMin > 0) {
        item.runtimeSeconds = runtimeMin * 60;
      }
    }
  }

  item.customFields.insert(QStringLiteral("tmdb_id"),
                           QString::number(static_cast<qint64>(detail.value("id").toDouble(0))));
  item.customFields.insert(QStringLiteral("media_type"), mediaType);
  const QString tagline = detail.value("tagline").toString();
  if (!tagline.isEmpty()) {
    item.customFields.insert(QStringLiteral("tagline"), tagline);
  }
  const double vote = detail.value("vote_average").toDouble(-1.0);
  if (vote >= 0) {
    item.customFields.insert(QStringLiteral("tmdb_rating"), QString::number(vote, 'f', 1));
  }

  appendImageUrls(item, detail.value("poster_path").toString(),
                  detail.value("backdrop_path").toString());
  return item;
}

ErrorUtils::Result<Scraper::ScrapedItem> parseCollectionSearchResponse(const QByteArray &json) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "TMDB collection-search response is not valid JSON",
                               "TmdbParser::parseCollectionSearchResponse");
  }
  const QJsonArray results = doc.object().value("results").toArray();
  Scraper::ScrapedItem item;
  if (results.isEmpty()) {
    return item; // successful "no match" — no media; the caller maps this to not-found
  }
  // TMDB returns collections in popularity order; take the top match.
  const QJsonObject top = results.first().toObject();
  item.title = top.value("name").toString();

  // Poster → the collection's logo/header art; backdrop → its background. The
  // scopeKey (owning collection uuid) is filled in by the provider.
  const QString posterPath = top.value("poster_path").toString();
  if (!posterPath.isEmpty()) {
    Scraper::MediaAsset logo;
    logo.type = QStringLiteral("logo");
    logo.label = QStringLiteral("Collection logo");
    logo.url = QUrl(QString::fromLatin1(IMAGE_BASE) + posterPath);
    logo.scope = Scraper::MediaScope::Collection;
    logo.entityRole = Scraper::EntityArtRole::Logo;
    item.media.append(logo);
  }
  const QString backdropPath = top.value("backdrop_path").toString();
  if (!backdropPath.isEmpty()) {
    Scraper::MediaAsset background;
    background.type = QStringLiteral("background");
    background.label = QStringLiteral("Collection background");
    background.url = QUrl(QString::fromLatin1(IMAGE_BASE) + backdropPath);
    background.scope = Scraper::MediaScope::Collection;
    background.entityRole = Scraper::EntityArtRole::Background;
    item.media.append(background);
  }
  return item;
}

void appendImageUrls(Scraper::ScrapedItem &item, const QString &posterPath,
                     const QString &backdropPath) {
  if (!posterPath.isEmpty()) {
    Scraper::MediaAsset poster;
    poster.type = QStringLiteral("front");
    poster.label = QStringLiteral("Poster");
    poster.url = QUrl(QString::fromLatin1(IMAGE_BASE) + posterPath);
    item.media.append(poster);
  }
  if (!backdropPath.isEmpty()) {
    Scraper::MediaAsset backdrop;
    backdrop.type = QStringLiteral("fanart");
    backdrop.label = QStringLiteral("Backdrop");
    backdrop.url = QUrl(QString::fromLatin1(IMAGE_BASE) + backdropPath);
    item.media.append(backdrop);
  }
}

} // namespace TmdbParser
