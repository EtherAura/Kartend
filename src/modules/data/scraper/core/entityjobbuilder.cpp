#include "entityjobbuilder.h"

#include "collection/collectionconfig.h"
#include "metadatalookupprovider.h"
#include "scrapertypes.h"

namespace Scraper {

QList<ScraperService::CollectionJob> buildEntityJobs(const QList<CollectionConfig> &collections,
                                                     int collectionIndex, const QString &uuid,
                                                     const QString &artworkDir,
                                                     const EntityProviderBuilder &providerBuilder) {
  QList<ScraperService::CollectionJob> jobs;
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return jobs;
  }
  const CollectionConfig &cfg = collections[collectionIndex];
  // A playlist is a synthesized config spanning whatever its rules match — it
  // has no single platform to resolve, so an entity job could only ever land
  // in the not-found bucket.
  if (cfg.isPlaylist) {
    return jobs;
  }
  auto provider = providerBuilder ? providerBuilder(collectionIndex) : nullptr;
  if (!provider) {
    return jobs;
  }
  QList<Scraper::ScrapeEntityType> types = provider->supportedEntities();
  // Kartend-445su: collection/group data rides along with every entity
  // launch. A provider that cannot scrape Collection entities itself
  // (ScreenScraper is Platform-only) still gets a Collection job — the
  // coordinator's capability routing dispatches it to the
  // Wikidata/Wikipedia data provider.
  if (!types.contains(Scraper::ScrapeEntityType::Collection)) {
    types.append(Scraper::ScrapeEntityType::Collection);
  }
  for (Scraper::ScrapeEntityType type : types) {
    if (type == Scraper::ScrapeEntityType::Game) continue; // Game is the per-item path
    ScraperService::CollectionJob job;
    job.collectionIndex = collectionIndex;
    job.collectionUuid = uuid;
    job.collectionName = cfg.name;
    job.artworkDir = artworkDir;
    job.entity.type = type;
    // Platform resolves its systemeid from an empty identity (override →
    // autodetect); Collection/Category carry the collection uuid (Kartend-ckepd.1).
    job.entity.identity = (type == Scraper::ScrapeEntityType::Platform) ? QString() : uuid;
    job.entity.collectionIndex = collectionIndex;
    jobs.append(job);
  }
  return jobs;
}

} // namespace Scraper
