#ifndef ENTITYJOBBUILDER_H
#define ENTITYJOBBUILDER_H

// Kartend-ud6q2: the entity (platform / collection logo + background) job
// queue, built away from any dialog.
//
// This was ScrapeResultDialogUnified::buildEntityJobs, a private method of a
// UI class. It became shared the moment collection CREATION grew the ability
// to fetch entity art with no dialog on screen: the background path and the
// dialog path must enqueue the *same* jobs, or the two routes to the same
// artwork start disagreeing about what a collection's art is. Rather than
// duplicate the rules (drop playlists, drop provider-less collections, always
// append a Collection job so the coordinator's capability routing can reach
// the Wikidata fallback), they live here and both callers delegate.
//
// Pure apart from the provider builder it is handed — no widgets, no service
// state — so the rules are unit-testable without standing up a scrape.

#include <functional>
#include <memory>

#include <QList>
#include <QString>

#include "scraperservice.h"

class MetadataLookupProvider;
struct CollectionConfig;

namespace Scraper {

/// Resolves the scraper for a collection index. Matches the closure
/// ScraperControllerInternal::makeProviderBuilder hands the dialog and the
/// service, so callers pass the one they already hold.
using EntityProviderBuilder = std::function<std::shared_ptr<MetadataLookupProvider>(int)>;

/// Build the entity jobs for one collection, or an empty list when the
/// collection cannot produce any: an out-of-range index, a playlist (a
/// synthesized config spanning whatever its rules match, so it has no single
/// platform to resolve), or a collection whose @p providerBuilder yields no
/// scraper.
///
/// @p uuid and @p artworkDir are resolved by the caller — the service's
/// persistence layer keys jobs by them so a resume survives a config reorder.
[[nodiscard]] QList<ScraperService::CollectionJob>
buildEntityJobs(const QList<CollectionConfig> &collections, int collectionIndex,
                const QString &uuid, const QString &artworkDir,
                const EntityProviderBuilder &providerBuilder);

} // namespace Scraper

#endif // ENTITYJOBBUILDER_H
