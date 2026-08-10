#ifndef FLATHUBPARSER_H
#define FLATHUBPARSER_H

#include <QByteArray>
#include <QString>

#include "errorutils.h"
#include "scrapertypes.h"

/// Parser for the Flathub AppStream web API (Kartend-2bzbu). Pure
/// QByteArray → Result transform, stateless, no network — the provider
/// feeds it reply bodies.
///
/// One endpoint:
///   api/v2/appstream/<app-id> → {name, summary, description (HTML),
///   developer_name, categories[], project_license,
///   releases[{version,timestamp}], …}
/// Unknown apps answer HTTP 404 (surfaced as a transport error before this
/// parser runs); a 200 whose body is not a JSON object maps to
/// RemoteResourceNotFound so the batch runner buckets it as not-found.
namespace FlathubParser {

/// Field mapping mirrors what LauncherImportService::applySteamMetadata
/// produces for Steam stubs, so a Flatpak collection's details pane fills
/// the same way: description (HTML stripped, summary fallback), developer,
/// genre (categories minus the redundant "Game"), releaseDate (latest
/// release timestamp as ISO date). project_license lands in customFields —
/// for FOSS apps the licence is a headline fact no other provider carries.
[[nodiscard]] ErrorUtils::Result<Scraper::ScrapedItem> parseAppstream(const QByteArray &body,
                                                                      const QString &appId);

} // namespace FlathubParser

#endif // FLATHUBPARSER_H
