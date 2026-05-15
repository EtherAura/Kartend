#ifndef TMDBPARSER_H
#define TMDBPARSER_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "errorutils.h"
#include "scrapertypes.h"

/// Pure JSON → typed-result parsers for The Movie Database (TMDB).
/// Reference shapes:
///
///   Multi search:  https://api.themoviedb.org/3/search/multi?query=...
///   Movie detail: https://api.themoviedb.org/3/movie/<id>?append_to_response=release_dates,credits
///   TV detail:     https://api.themoviedb.org/3/tv/<id>?append_to_response=content_ratings,credits
///   Image base:    https://image.tmdb.org/t/p/{size}/{path}
///                  (size ∈ w92, w185, w300, w500, original)
///
/// Authentication is a v4 read-access bearer token attached to the
/// Authorization header by the provider. The parser layer never sees
/// the token — it just shapes the response bodies.
namespace TmdbParser {

/// Parse a `/search/multi` response into candidates ordered by
/// TMDB's own popularity ranking (we preserve their order). Empty
/// `results` array is a successful "no match". Each candidate's
/// `providerSpecificId` carries `<media_type>:<id>` so the detail
/// fetcher can route to the right `/movie/` or `/tv/` endpoint
/// without a second sniff.
[[nodiscard]] ErrorUtils::Result<QList<Scraper::ScrapeCandidate>>
parseSearchResponse(const QByteArray &json);

/// Parse a movie or TV detail response. `mediaType` must be either
/// "movie" or "tv" — used to apply the small structural differences
/// between the two endpoints (TV uses first_air_date instead of
/// release_date, episode_run_time instead of runtime).
[[nodiscard]] ErrorUtils::Result<Scraper::ScrapedItem>
parseDetailResponse(const QByteArray &json, const QString &mediaType);

/// Append cover URLs (poster as primary `front`; backdrop as
/// `fanart`) for the given paths. Paths come straight from TMDB's
/// `poster_path` / `backdrop_path` fields and include the leading
/// slash; the helper builds the canonical
/// `https://image.tmdb.org/t/p/w500{path}` URLs.
void appendImageUrls(Scraper::ScrapedItem &item, const QString &posterPath,
                     const QString &backdropPath);

} // namespace TmdbParser

#endif // TMDBPARSER_H
