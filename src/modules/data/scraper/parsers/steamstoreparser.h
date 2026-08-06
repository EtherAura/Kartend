#ifndef STEAMSTOREPARSER_H
#define STEAMSTOREPARSER_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "errorutils.h"
#include "scrapertypes.h"

/// Parsers for the Steam storefront web API (Kartend-ksjx0). Pure
/// QByteArray → Result transforms, stateless, no network — the provider
/// feeds them reply bodies.
///
/// Two endpoints:
///   storesearch/?term=…&cc=us&l=en → {"items":[{id, name, tiny_image}…]}
///   appdetails?appids=N&l=en      → {"N":{"success":bool,"data":{…}}}
/// appdetails wraps the payload under the requested appid and reports
/// missing/delisted apps as success:false with HTTP 200 — that maps to
/// RemoteResourceNotFound so the batch runner buckets it as not-found
/// rather than an error.
namespace SteamStoreParser {

[[nodiscard]] ErrorUtils::Result<QList<Scraper::ScrapeCandidate>>
parseStoreSearch(const QByteArray &body);

[[nodiscard]] ErrorUtils::Result<Scraper::ScrapedItem> parseAppDetails(const QByteArray &body,
                                                                       const QString &appId);

} // namespace SteamStoreParser

#endif // STEAMSTOREPARSER_H
