#ifndef COLLECTIONPLATFORMBACKGROUNDS_H
#define COLLECTIONPLATFORMBACKGROUNDS_H

#include <QList>
#include <QString>

#include "platformbackgroundcandidates.h"
#include "screenscrapersystems.h"

struct CollectionConfig;

/// Collection-facing source of retained platform-background candidates
/// (Kartend-ji94z). Resolves the shared-art root and ScreenScraper systemeid
/// exactly as the collection's platform scrape does, then hands them to
/// PlatformBackgroundCandidates::enumerate, so a later picker never has to
/// guess where the scraper stored its files or which platform id it used.
///
/// Lives in modules/media because it needs both the data layer (systems
/// resolution, path expansion) and the chrome enumerator; neither of those
/// layers may see the other.
///
/// Read-only and stateless: no settings writes, no network, no widgets. Pure
/// filesystem listing over values the caller passes in, so it is safe to call
/// from any thread as long as the caller's arguments are not being mutated
/// concurrently.
namespace CollectionPlatformBackgrounds {

/// `<expanded artworkDirectory>/_shared` for @p cfg — the root the platform
/// scrape's MediaScope::Platform writes land under — or empty when the
/// collection has no artwork directory of its own. Deliberately does NOT
/// borrow another collection's root (the entity scrape's substitution for a
/// root-less collection is not repeated here).
[[nodiscard]] QString sharedRoot(const CollectionConfig &cfg);

/// The systemeid @p cfg's platform scrape resolves against @p systems
/// (override, then autodetect — ScreenScraperSystems::resolveForCollection),
/// or 0 when the collection is a playlist or no platform resolves.
[[nodiscard]] int systemId(const CollectionConfig &cfg,
                           const QList<ScreenScraperSystems::System> &systems);

/// The retained illustration/photo/background candidates for @p cfg's own
/// platform, in PlatformBackgroundCandidates::enumerate order. @p systems is
/// the systems catalog the caller already holds (this never fetches one).
/// Empty for a playlist, an unresolved platform, or a missing shared root.
[[nodiscard]] QList<PlatformBackgroundCandidates::Candidate>
forCollection(const CollectionConfig &cfg, const QList<ScreenScraperSystems::System> &systems);

/// forCollection against the ScreenScraper systems catalog already on disk at
/// @p cachePath (ScreenScraperSystemCache::loadCachedSystems), so the list is
/// available offline. Read-only: never refreshes the cache, ignores its TTL
/// (a stale but valid catalog still lists retained art), no network, no
/// credentials. A missing, unreadable, malformed or empty cache is treated as
/// an empty catalog: an explicit System ID override still resolves, and
/// without one nothing is listed (nothing is guessed).
[[nodiscard]] QList<PlatformBackgroundCandidates::Candidate>
forCollectionFromCache(const CollectionConfig &cfg, const QString &cachePath);

/// forCollectionFromCache at ScreenScraperSystemCache::defaultCachePath().
[[nodiscard]] QList<PlatformBackgroundCandidates::Candidate>
forCollectionFromCache(const CollectionConfig &cfg);

} // namespace CollectionPlatformBackgrounds

#endif // COLLECTIONPLATFORMBACKGROUNDS_H
