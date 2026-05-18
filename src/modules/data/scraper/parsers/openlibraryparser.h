#ifndef OPENLIBRARYPARSER_H
#define OPENLIBRARYPARSER_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "errorutils.h"
#include "scrapertypes.h"

/// Pure JSON → typed-result parsers for Open Library responses, split
/// from OpenLibraryProvider so the parsing layer is testable with
/// canned payloads. Reference shapes:
///
///   Search:  https://openlibrary.org/search.json?q=...&limit=N
///   Detail:  https://openlibrary.org/works/<work-id>.json
///   Cover:   https://covers.openlibrary.org/b/id/{cover_i}-{S|M|L}.jpg
///
/// Open Library has no API key requirement for read access.
namespace OpenLibraryParser {

/// Parse a `/search.json` response into candidates ordered by Open
/// Library's own ranking (we preserve their order rather than re-sorting
/// — they don't return a per-row score). Empty `docs` array is a
/// successful "no match" rather than an error.
[[nodiscard]] ErrorUtils::Result<QList<Scraper::ScrapeCandidate>>
parseSearchResponse(const QByteArray &json);

/// Parse a `/works/<work-id>.json` detail response. The work key is
/// passed in separately because the response itself uses `key`
/// (slash-prefixed) and we want the bare id round-tripped into
/// customFields.
[[nodiscard]] ErrorUtils::Result<Scraper::ScrapedItem> parseDetailResponse(const QByteArray &json,
                                                                           const QString &workKey);

/// Append the canonical Open Library cover URLs (medium + large) for
/// the given `cover_i` integer to `item`. No-op when coverId <= 0.
void appendCoverUrls(Scraper::ScrapedItem &item, qint64 coverId);

/// Extracts an ISBN-10 or ISBN-13 from a filename / arbitrary string
/// when present. Returns the digits-only form (no dashes, no "ISBN"
/// prefix); empty when no match. Tries ISBN-13 first (more specific)
/// then ISBN-10. Used by OpenLibraryProvider to route filenames like
/// "9780451524935 - 1984.epub" to the more reliable ISBN-keyed
/// lookup before falling back to title-search.
[[nodiscard]] QString extractIsbnFromText(const QString &text);

} // namespace OpenLibraryParser

#endif // OPENLIBRARYPARSER_H
