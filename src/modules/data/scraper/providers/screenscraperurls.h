#ifndef SCREENSCRAPERURLS_H
#define SCREENSCRAPERURLS_H

#include "errorutils.h"
#include "romhasher.h"
#include "screenscrapercatalogmanager.h"

#include <QString>
#include <QUrl>
#include <QUrlQuery>

/// Pure URL/query construction + HTTP-status mapping for ScreenScraper's
/// api2 endpoints. Extracted from ScreenScraperProvider so the SS wire
/// shape (which params ride on which endpoint, how SS's overloaded status
/// codes read back) can be unit-tested without a provider instance or the
/// network, and the provider TU stays focused on the fetch/callback
/// pipeline.
namespace ScreenScraperUrls {

/// SS dev + optional user credentials, in the shape the catalog manager
/// already owns (see screenscrapercatalogmanager.h).
using Credentials = ScreenScraperCatalogManager::Credentials;

// API endpoints live under api.screenscraper.fr — the public site at
// www.screenscraper.fr is the human-facing browse UI and does not
// answer the api2/* paths we hit here.
inline constexpr const char *SS_HOST = "api.screenscraper.fr";

// SS serves media files from a separate CDN host (`neoclone.screenscraper.fr`).
// Without an explicit policy this host defaults to maxConcurrent=1 in
// HttpClient, so every cover/screenshot/fanart download serialized
// behind the previous one — the symptom in /tmp/scrape.log was 27
// queued media requests all sitting at inflight=1 even after the
// dialog dispatched them in parallel.
inline constexpr const char *SS_MEDIA_HOST = "neoclone.screenscraper.fr";
// SSRF defense-in-depth (Kartend-pugp.2): medias[].url and any redirect it
// follows are response-derived (untrusted), so fetchMediaBytes pins them to
// ScreenScraper's own domain. Domain-level rather than the exact SS_MEDIA_HOST
// so SS adding/rotating CDN subdomains keeps working — only a host (or
// redirect) off screenscraper.fr entirely is refused, which is the SSRF case.
inline constexpr const char *SS_MEDIA_HOST_SUFFIX = "screenscraper.fr";

/// The four base api2 query params every SS endpoint needs. Type-agnostic on the
/// dev creds so both the Credentials builders (buildJeuInfosUrl) and the free
/// helpers (fetchUserInfo/fetchInfraInfo) share one definition (Kartend audit D-03).
void addCommonQueryParams(QUrlQuery &q, const QString &devId, const QString &devPassword);

/// Build the jeuInfos.php URL from credentials + resolved system id
/// + hash result. Extracted from runLookupAfterHash so the SS query
/// shape can be regression-tested without hitting the network.
[[nodiscard]] QUrl buildJeuInfosUrl(const Credentials &creds, const QString &romnom, int systemeid,
                                    const RomHasher::Result &hashes, bool hasUser);

/// Build a mediaSysteme.php URL for one platform media type (Kartend-ckepd.4).
/// Media (bytes) endpoint — no output=json.
///
/// `mediaToken` is the SS media parameter VERBATIM and must already include the
/// region qualifier — "wheel(wor)", not "wheel". A bare type answers 200
/// "NOMEDIA" even for systems that have the art. The caller owns the qualifier
/// (Kartend-qzk1s) because the systemesListe catalog carries the exact token
/// per available region variant; callers without a catalog entry pass
/// "<type>(wor)" explicitly.
[[nodiscard]] QUrl buildSystemeMediaUrl(const Credentials &creds, int systemeid,
                                        const QString &mediaToken, bool hasUser);

/// SS API v2 returns specific HTTP status codes for distinct failure
/// modes (see api.screenscraper.fr docs). Re-map the upstream's
/// generic "HTTP request failed" + French response body into a single
/// English sentence the dialog can show without dumping the raw blob
/// at the user. Returns the original error untouched when the status
/// code isn't one SS overloads (regular 5xx, network-level timeouts,
/// etc.) so unexpected failures still surface their underlying detail.
[[nodiscard]] ErrorUtils::ErrorContext
mapScreenScraperHttpError(const ErrorUtils::ErrorContext &original);

} // namespace ScreenScraperUrls

#endif // SCREENSCRAPERURLS_H
