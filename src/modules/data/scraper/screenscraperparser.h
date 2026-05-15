#ifndef SCREENSCRAPERPARSER_H
#define SCREENSCRAPERPARSER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include "errorutils.h"
#include "scrapertypes.h"

/// Pure JSON → typed-result parsers for ScreenScraper.fr's
/// `jeuInfos.php` response. SS's `jeuInfos` returns one matched game
/// (the API picks the best match server-side from `romnom`/hash
/// inputs); there is no separate "search-then-detail" two-step like
/// MusicBrainz/TMDB. So both `parseSearch...` and `parseDetail...`
/// collapse to one helper, exposed under both names so the provider
/// can satisfy MetadataLookupProvider's API.
///
/// Field selection: SS returns nested arrays of region- and language-
/// tagged values (e.g. `noms[].text` keyed on region "us"/"eu"/"jp",
/// `synopsis[].text` keyed on language "en"/"fr"/etc.). The parser
/// picks the user's preferred locale where possible; the "most
/// common English" subset (us > wor > ss > eu > jp > first available)
/// is hard-coded for now — locale-respecting selection is a future
/// polish.
namespace ScreenScraperParser {

/// Options threaded through to the media URL rewriter. `mediaMaxDim`
/// > 0 appends `maxwidth` + `maxheight` query params to image media
/// URLs so SS server-side downscales the asset; 0 leaves URLs alone
/// (full resolution). `preferJpg` stamps `outputformat=jpg` onto image
/// URLs so SS re-encodes lossless PNGs as smaller (lossy) JPGs — only
/// honored alongside the Fastest preset since it discards quality.
struct ParseOptions {
  int mediaMaxDim = 0;
  bool preferJpg = false;
  /// Optional canonical-tag → friendly-label map sourced from SS's
  /// runtime mediasJeuListe.php catalog. When supplied, `mapMedia`
  /// uses the catalog label instead of the raw SS tag for unknown
  /// types (so `bezel-16-9` shows as "Bezel 16:9" instead of the raw
  /// slug). Empty map = fall back to the parser's hard-coded subset
  /// — the loose contract that lets new SS types surface even before
  /// the catalog cache is populated.
  QHash<QString, QString> mediaTypeLabels;
};

/// Parse the response body. The matched game (if any) is wrapped in
/// `response.jeu` per SS's API shape; an absent `jeu` is treated as
/// "no match found" and yields an empty list rather than an error.
[[nodiscard]] ErrorUtils::Result<QList<Scraper::ScrapeCandidate>>
parseSearchResponse(const QByteArray &json);

/// Same JSON but yielding the rich ScrapedItem (mapped onto
/// ItemMetadata typed columns + customFields). The candidate's
/// providerSpecificId carries SS's internal game id which we
/// stash in customFields for round-trip.
[[nodiscard]] ErrorUtils::Result<Scraper::ScrapedItem>
parseDetailResponse(const QByteArray &json, const ParseOptions &options = {});

/// Snapshot of the authenticated user's account state returned by
/// SS's `ssuserInfos.php` endpoint. Surfaced under the Scrapers
/// settings panel so the user can see (a) which tier SS thinks
/// they are at runtime, and (b) the exact thread / quota numbers
/// SS is granting — useful when premium-tier perks aren't taking
/// effect for whatever reason. All numeric fields can be 0 if the
/// API doesn't report them.
struct ScreenScraperUserInfo {
  QString id;                ///< SS user id (`ssuser.id`).
  QString level;             ///< Localised tier name (`niveau`).
  int maxThreads = 0;        ///< Concurrent ROM-scrape allowance.
  int requestsToday = 0;     ///< Requests already made today.
  int maxRequestsPerDay = 0; ///< Daily quota.
  /// Per-account download bandwidth ceiling enforced server-side
  /// (Ko/s — kibibytes per second, SS notation). Drives the empirical
  /// scrape-throughput wall users hit at high concurrency. 0 = field
  /// absent in the response.
  int maxDownloadSpeedKBps = 0;
  /// Per-minute API call quota. 0 = field absent.
  int maxRequestsPerMinute = 0;
  /// Daily failed-lookup quota. SS treats lookups that don't match a
  /// known game as 'KO'. Hitting `maxRequestsKoPerDay` returns HTTP
  /// 431 for the rest of the day. 0 = field absent.
  int maxRequestsKoPerDay = 0;
  int requestsKoToday = 0;
};

/// Parse the response of `ssuserInfos.php`. Returns an error context
/// when the response isn't a valid JSON document or when SS replies
/// with one of its plain-text error blobs (authentication failed,
/// quota exhausted, etc.) — those land here as InvalidArgument with
/// the SS error text in the details.
[[nodiscard]] ErrorUtils::Result<ScreenScraperUserInfo>
parseUserInfoResponse(const QByteArray &json);

/// Realtime infrastructure status returned by SS's `ssinfraInfos.php`.
/// Surfaced before a scrape so users get a heads-up when the SS API
/// is overloaded or has shut anonymous/leecher traffic out (in which
/// case anonymous scrapes will fail with HTTP 401 across the board).
/// Numeric fields default to 0 when SS doesn't report them; the
/// dialog renders only the populated ones.
struct ScreenScraperInfraInfo {
  /// Per-server CPU usage (percent). SS exposes three numbered
  /// servers; values are 0..100. Empty when the field was absent.
  int cpu1Percent = 0;
  int cpu2Percent = 0;
  int cpu3Percent = 0;
  /// API hits per minute across the cluster.
  int threadsPerMin = 0;
  /// Active scrapers right now (other users currently mid-scrape).
  int activeScrapers = 0;
  /// Today's total API call count across all users.
  int apiAccessToday = 0;
  /// True when SS has shut the API down for non-members (the
  /// "leecher" tier, in SS parlance — anonymous + free accounts).
  /// Anonymous scrapes will fail uniformly with HTTP 401 in this
  /// state; the dialog refuses up-front instead of firing doomed
  /// requests.
  bool closedForNonMembers = false;
  bool closedForLeechers = false;
};

/// Parse the response of `ssinfraInfos.php`. Same error semantics as
/// `parseUserInfoResponse`.
[[nodiscard]] ErrorUtils::Result<ScreenScraperInfraInfo>
parseInfraInfoResponse(const QByteArray &json);

} // namespace ScreenScraperParser

#endif // SCREENSCRAPERPARSER_H
