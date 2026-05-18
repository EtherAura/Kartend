#ifndef SCREENSCRAPERSYSTEMCACHE_H
#define SCREENSCRAPERSYSTEMCACHE_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "errorutils.h"
#include "screenscrapersystems.h"

/// Runtime cache for ScreenScraper.fr's systemesListe.php catalog.
///
/// The catalog is the source-of-truth for systemeid → display name
/// + aliases + extensions (no hardcoded list anywhere in Kartend per
/// the user's "no platform names in source" requirement). On first
/// use the cache fetcher hits SS's API, parses the response, and
/// writes it to disk under `QStandardPaths::CacheLocation/kartend/`;
/// subsequent runs reload from disk until the file is older than the
/// TTL and a fresh fetch happens.
///
/// All parsing is in pure functions on QByteArray so the network
/// path is testable with canned JSON; the on-disk persistence is
/// testable with QTemporaryDir.
namespace ScreenScraperSystemCache {

/// Cache TTL — 30 days. The SS catalog grows when new systems are
/// added (a few times a year) but the existing entries never change
/// shape, so a long TTL minimises noise without missing meaningful
/// updates. Users can force a refresh by deleting the cache file.
constexpr int CACHE_TTL_DAYS = 30;

/// Canonical on-disk path for the cached catalog. Lives under
/// `QStandardPaths::CacheLocation/kartend/screenscraper-systems.json`
/// — caller passes this to load/save unless overriding for tests.
[[nodiscard]] QString defaultCachePath();

/// Parse SS's systemesListe.php response (or our own cached JSON,
/// which is the same shape) into the typed System list. Defensive:
/// pulls every string-valued entry under `systemes[].noms.*` as a
/// candidate alias (so frontend-shorthand variants like nom_recalbox
/// and nom_retropie all participate in autodetect); splits the
/// `extensions` comma-separated string into the typed list. Empty
/// `systemes` array is a successful empty list, not an error.
[[nodiscard]] ErrorUtils::Result<QList<ScreenScraperSystems::System>>
parseSystemsResponse(const QByteArray &json);

/// Read the cached catalog from disk. Returns empty (non-error) when
/// the file doesn't exist; returns an error only on actual parse
/// failures so callers can distinguish "not cached yet" from "cache
/// got corrupted".
[[nodiscard]] ErrorUtils::Result<QList<ScreenScraperSystems::System>>
loadCachedSystems(const QString &filePath);

/// Write the catalog to disk. Creates intermediate directories.
/// Returns false (with a logged error) when the write fails — the
/// in-memory catalog is still usable for the current session.
[[nodiscard]] bool saveSystems(const QString &filePath,
                               const QList<ScreenScraperSystems::System> &systems);

/// True when the cache file at `filePath` is older than CACHE_TTL_DAYS
/// or missing. Caller uses this to decide whether to skip the network
/// fetch on cold start.
[[nodiscard]] bool isCacheStale(const QString &filePath);

} // namespace ScreenScraperSystemCache

#endif // SCREENSCRAPERSYSTEMCACHE_H
