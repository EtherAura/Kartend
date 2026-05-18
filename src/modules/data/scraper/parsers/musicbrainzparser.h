#ifndef MUSICBRAINZPARSER_H
#define MUSICBRAINZPARSER_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "errorutils.h"
#include "scrapertypes.h"

/// Pure JSON → typed-result parsers for MusicBrainz responses. Split
/// from MusicBrainzProvider so the parsing layer is testable with
/// canned payloads and doesn't need an HttpClient / QNetworkReply
/// fake. Production calls these on the bytes returned by HttpClient.
namespace MusicBrainzParser {

/// Parse a `/ws/2/release/?query=...&fmt=json` response into a list of
/// candidates ordered by descending match score. Returns an empty
/// list (with no error) when the JSON parsed but had zero releases —
/// that's a successful "no match" rather than a parse failure.
[[nodiscard]] ErrorUtils::Result<QList<Scraper::ScrapeCandidate>>
parseSearchResponse(const QByteArray &json);

/// Parse a `/ws/2/release/<mbid>?inc=artists+labels+release-groups+tags
/// +recordings&fmt=json` detail response into a ScrapedItem with all
/// the typed fields ItemMetadata can absorb (title / publisher / date
/// / genre tags / track count). Cover Art Archive media URLs are
/// added separately via `appendCoverArtUrls`.
[[nodiscard]] ErrorUtils::Result<Scraper::ScrapedItem> parseDetailResponse(const QByteArray &json);

/// Append the canonical Cover Art Archive media URLs for the given
/// MBID to `item`. Doesn't make HTTP requests — just builds the
/// well-known coverartarchive.org URLs (front cover + back cover);
/// the provider downloads them on demand. Adds nothing when `mbid`
/// is empty.
void appendCoverArtUrls(Scraper::ScrapedItem &item, const QString &mbid);

} // namespace MusicBrainzParser

#endif // MUSICBRAINZPARSER_H
