// Wikidata logo provider (Kartend-czna3). Two JSON hops + one image fetch;
// all wire shapes live in WikidataLogoParser so this TU is orchestration
// only. Wikimedia etiquette: descriptive User-Agent (ProviderBase's
// contact-bearing UA satisfies their policy) and gentle throttles — there
// is no hard quota, but this fires at most a handful of requests per
// scrape run anyway.
#include "wikidatalogoprovider.h"

#include <utility>

#include "collection/collectionconfig.h"
#include "entitymetadata.h"
#include "wikidatalogoparser.h"

namespace {

constexpr int WD_RATE_LIMIT_MS = 250;

/// Fetch + every redirect pinned to Wikimedia infrastructure (SSRF defence,
/// same posture as every other provider's media host pin): Special:FilePath
/// answers with a redirect to upload.wikimedia.org.
const QStringList &wikimediaHostSuffixes() {
  static const QStringList suffixes = {QStringLiteral("wikidata.org"),
                                       QStringLiteral("wikimedia.org")};
  return suffixes;
}

} // namespace

WikidataLogoProvider::WikidataLogoProvider(CollectionAccessor collectionAccessor,
                                           std::function<bool()> isShellAccessor)
    : m_collectionAccessor(std::move(collectionAccessor)),
      m_isShellAccessor(std::move(isShellAccessor)) {
  registerThrottles({{"www.wikidata.org", WD_RATE_LIMIT_MS},
                     {"commons.wikimedia.org", WD_RATE_LIMIT_MS},
                     {"upload.wikimedia.org", WD_RATE_LIMIT_MS},
                     // Kartend-445su: the data path adds the Wikipedia REST
                     // summary endpoint.
                     {"en.wikipedia.org", WD_RATE_LIMIT_MS}});
}

void WikidataLogoProvider::lookup(const QString & /*query*/, LookupCallback callback) {
  // Entity-only provider (see class doc) — the game-lookup surface is
  // deliberately inert rather than half-implemented.
  if (callback) callback(QList<Scraper::ScrapeCandidate>{});
}

void WikidataLogoProvider::fetchDetail(const Scraper::ScrapeCandidate & /*candidate*/,
                                       DetailCallback callback) {
  if (callback) {
    callback(ErrorUtils::ErrorContext::error(
        ErrorUtils::ErrorCode::InvalidArgument,
        QStringLiteral("Wikidata is an entity-art provider; it has no per-item detail"),
        QStringLiteral("WikidataLogoProvider::fetchDetail")));
  }
}

void WikidataLogoProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  if (!callback) return;
  getImageBytes(userAgentHeader(), url, std::move(callback), wikimediaHostSuffixes());
}

void WikidataLogoProvider::fetchEntity(const Scraper::EntityScrapeTarget &target,
                                       DetailCallback callback) {
  if (!callback) return;
  if (target.type != Scraper::ScrapeEntityType::Collection) {
    callback(ErrorUtils::ErrorContext::error(
        ErrorUtils::ErrorCode::InvalidArgument,
        QStringLiteral("Wikidata scrapes Collection entities only (got entity type %1)")
            .arg(static_cast<int>(target.type)),
        QStringLiteral("WikidataLogoProvider::fetchEntity")));
    return;
  }
  const CollectionConfig *cfg = m_collectionAccessor ? m_collectionAccessor() : nullptr;
  const QString name = cfg ? cfg->name.trimmed() : QString();
  if (name.isEmpty()) {
    callback(ErrorUtils::ErrorContext::error(
        ErrorUtils::ErrorCode::InvalidArgument,
        QStringLiteral("No collection resolves for the Wikidata logo lookup"),
        QStringLiteral("WikidataLogoProvider::fetchEntity")));
    return;
  }
  // scopeKey drives the shared filename (`collection_<key>`); the target
  // identity is the collection uuid, which passes the scope-key allowlist.
  const QString scopeKey = target.identity;

  const auto notFound = [name](const char *what) {
    return ErrorUtils::ErrorContext::error(
        ErrorUtils::ErrorCode::RemoteResourceNotFound,
        QStringLiteral("Wikidata has no %1 for \"%2\"").arg(QLatin1String(what), name),
        QStringLiteral("WikidataLogoProvider::fetchEntity"));
  };

  // Hop 1 (Kartend-6i10t): resolve the entity through the search-query
  // ladder — the full name first, then compound-name fragments — picking
  // the hit whose label/description matches the collection's media-type
  // vocabulary. Blind first-hit acceptance gave a games collection named
  // "Saturn" the PLANET, and compound names ("Famicom - Nintendo
  // Entertainment System") matched nothing at all. Continuations guard on
  // the base lifetime token (cr950 class): HttpClient holds them with no
  // QObject severing, so a provider destroyed mid-flight must not be
  // touched.
  const bool preferCompany = m_isShellAccessor && m_isShellAccessor();
  resolveEntityId(
      WikidataLogoParser::searchQueryLadder(name), 0, QString(), cfg->type, preferCompany,
      [this, alive = std::weak_ptr<int>(m_lifetimeToken), name, scopeKey, notFound,
       callback](ErrorUtils::Result<QString> entity) mutable {
        if (alive.expired()) return;
        if (entity.isError()) {
          callback(entity.error());
          return;
        }
        if (entity.value().isEmpty()) {
          callback(notFound("matching entity"));
          return;
        }
        // Hop 2 (Kartend-445su): one wbgetentities call carries the logo
        // claim, the manufacturer claim, the inception year, the one-line
        // description, and the enwiki sitelink — the logo-only wbgetclaims
        // hop grew into the DATA hop.
        getJson<WikidataLogoParser::EntityData>(
            userAgentHeader(), WikidataLogoParser::buildEntityDataUrl(entity.value()),
            [](const QByteArray &body) { return WikidataLogoParser::parseEntityData(body); },
            [this, alive, name, scopeKey, notFound,
             callback](ErrorUtils::Result<WikidataLogoParser::EntityData> dataResult) mutable {
              if (alive.expired()) return;
              if (dataResult.isError()) {
                callback(dataResult.error());
                return;
              }
              const WikidataLogoParser::EntityData data = dataResult.value();
              if (data.logoFilename.isEmpty() && data.description.isEmpty() &&
                  data.manufacturerId.isEmpty() && data.enwikiTitle.isEmpty()) {
                // The entity resolved but carries nothing usable — art OR
                // text. Only now is not-found honest; a logo-less entity
                // with a description is a metadata-only success.
                callback(notFound("logo or descriptive data"));
                return;
              }
              // No std::move: finishEntityWithData takes the callback by CONST
              // reference (it copies into each continuation itself), so a move
              // here cannot happen and only reads as though ownership were
              // being handed over. clang-tidy performance-move-const-arg.
              finishEntityWithData(name, scopeKey, data, callback);
            });
      });
}

void WikidataLogoProvider::resolveEntityId(const QStringList &queries, int index,
                                           const QString &fallbackId, const QString &collectionType,
                                           bool preferCompany,
                                           std::function<void(ErrorUtils::Result<QString>)> done) {
  if (index >= queries.size()) {
    // Ladder exhausted with no vocabulary match — the first raw hit (if any
    // query returned one) preserves the pre-ladder behaviour for names the
    // keyword sets don't know; empty means genuinely nothing found.
    done(fallbackId);
    return;
  }
  getJson<QList<WikidataLogoParser::SearchHit>>(
      userAgentHeader(), WikidataLogoParser::buildSearchUrl(queries.at(index)),
      [](const QByteArray &body) { return WikidataLogoParser::parseEntitySearchHits(body); },
      [this, alive = std::weak_ptr<int>(m_lifetimeToken), queries, index, fallbackId,
       collectionType, preferCompany,
       done](ErrorUtils::Result<QList<WikidataLogoParser::SearchHit>> r) mutable {
        if (alive.expired()) return;
        if (r.isError()) {
          done(r.error());
          return;
        }
        const QList<WikidataLogoParser::SearchHit> hits = r.value();
        const QString picked = WikidataLogoParser::pickEntityForCollection(
            hits, collectionType, queries.at(index), preferCompany);
        if (!picked.isEmpty()) {
          done(picked);
          return;
        }
        QString nextFallback = fallbackId;
        if (nextFallback.isEmpty() && !hits.isEmpty()) {
          nextFallback = hits.first().id;
        }
        resolveEntityId(queries, index + 1, nextFallback, collectionType, preferCompany,
                        std::move(done));
      });
}

// Compose the ScrapedItem from the resolved entity data, chasing the two
// optional text hops first (one BATCHED labels request for every entity
// reference — manufacturer, country, developer, publisher, genre — then the
// Wikipedia summary). Each hop degrades gracefully: a failed labels hop
// leaves those fields unset, a failed summary falls back to Wikidata's
// one-line description.
void WikidataLogoProvider::finishEntityWithData(const QString &name, const QString &scopeKey,
                                                const WikidataLogoParser::EntityData &data,
                                                const DetailCallback &callback) {
  auto compose = [name, scopeKey, data](const QHash<QString, QString> &labels,
                                        const QString &summary) {
    Scraper::ScrapedItem item;
    item.sourceProviderId = QStringLiteral("wikidata");
    item.title = name;
    // Wikipedia's prose paragraph outranks Wikidata's one-liner; the
    // one-liner outranks nothing.
    item.description = !summary.isEmpty() ? summary : data.description;
    const auto putLabel = [&item, &labels](const char *field, const QString &id) {
      const QString label = labels.value(id);
      if (!id.isEmpty() && !label.isEmpty()) {
        item.customFields.insert(QLatin1String(field), label);
      }
    };
    putLabel(EntityMetadataStore::kFieldManufacturer, data.manufacturerId);
    putLabel(EntityMetadataStore::kFieldCountry, data.countryId);
    putLabel(EntityMetadataStore::kFieldDeveloper, data.developerId);
    putLabel(EntityMetadataStore::kFieldPublisher, data.publisherId);
    putLabel(EntityMetadataStore::kFieldGenre, data.genreId);
    // Kartend-5b5r1: the spec sheet. CPU/GPU/predecessor/successor labels
    // arrive from the same batched hop; the generation is the P361 value
    // whose label actually names a console generation ("part of" carries
    // other memberships too).
    putLabel(EntityMetadataStore::kFieldCpu, data.cpuId);
    putLabel(EntityMetadataStore::kFieldGpu, data.gpuId);
    putLabel(EntityMetadataStore::kFieldPredecessor, data.predecessorId);
    putLabel(EntityMetadataStore::kFieldSuccessor, data.successorId);
    for (const QString &partId : data.partOfIds) {
      const QString label = labels.value(partId);
      if (label.contains(QLatin1String("generation"), Qt::CaseInsensitive)) {
        item.customFields.insert(QLatin1String(EntityMetadataStore::kFieldGeneration), label);
        break;
      }
    }
    if (!data.unitsSold.isEmpty()) {
      item.customFields.insert(QLatin1String(EntityMetadataStore::kFieldUnitsSold), data.unitsSold);
    }
    if (!data.websiteUrl.isEmpty()) {
      item.customFields.insert(QLatin1String(EntityMetadataStore::kFieldWebsite), data.websiteUrl);
    }
    // Release span: P577's earliest release year beats P571 inception (a
    // console's inception is its announcement, not its launch), and P2669
    // closes the production span the way ScreenScraper's catalog does.
    QString released = !data.publicationYear.isEmpty() ? data.publicationYear : data.inceptionYear;
    if (!released.isEmpty() && !data.discontinuedYear.isEmpty()) {
      released += QStringLiteral("–") + data.discontinuedYear;
    }
    if (!released.isEmpty()) {
      item.customFields.insert(QLatin1String(EntityMetadataStore::kFieldReleaseDate), released);
    }
    if (!data.logoFilename.isEmpty()) {
      const bool isSvg = data.logoFilename.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive);
      Scraper::MediaAsset asset;
      // "logo-svg" lands in the sibling dir the navigation sidebar's
      // silhouette-source probing already prefers; raster logos join
      // the monochrome "logo" dir the same probing falls back to.
      asset.type = isSvg ? QStringLiteral("logo-svg") : QStringLiteral("logo");
      asset.label = QStringLiteral("Logo (Wikimedia Commons)");
      asset.url = WikidataLogoParser::buildLogoFileUrl(data.logoFilename);
      asset.scope = Scraper::MediaScope::Collection;
      asset.scopeKey = scopeKey;
      asset.entityRole = Scraper::EntityArtRole::Logo;
      asset.entityRolePriority = 0;
      item.media.append(asset);
    }
    if (!data.photoFilename.isEmpty()) {
      // Kartend-5b5r1: the console photograph (P18) rides along with the
      // logo — role None, so it lands in the shared art for the gallery
      // without disturbing the config-wired picks.
      Scraper::MediaAsset photo;
      photo.type = QStringLiteral("photo");
      photo.label = QStringLiteral("Console photo (Wikimedia Commons)");
      photo.url = WikidataLogoParser::buildLogoFileUrl(data.photoFilename);
      photo.scope = Scraper::MediaScope::Collection;
      photo.scopeKey = scopeKey;
      photo.entityRole = Scraper::EntityArtRole::None;
      photo.entityRolePriority = 0;
      item.media.append(photo);
    }
    return item;
  };

  // Hop 3 (optional): every referenced entity → English label, one batched
  // wbgetentities request (Kartend-6i10t — five ids would otherwise be five
  // throttled round-trips against Wikimedia).
  auto withLabels = [this, alive = std::weak_ptr<int>(m_lifetimeToken), data,
                     callback](auto &&then) mutable {
    const QStringList ids = data.referencedEntityIds();
    if (ids.isEmpty()) {
      then(QHash<QString, QString>{});
      return;
    }
    getJson<QHash<QString, QString>>(
        userAgentHeader(), WikidataLogoParser::buildLabelsUrl(ids),
        [](const QByteArray &body) { return WikidataLogoParser::parseEntityLabels(body); },
        [alive, then = std::forward<decltype(then)>(then)](
            ErrorUtils::Result<QHash<QString, QString>> r) mutable {
          if (alive.expired()) return;
          // Degrade, don't fail: labels are garnish on an already-usable
          // result.
          then(r.isOk() ? r.value() : QHash<QString, QString>{});
        });
  };

  withLabels([this, alive = std::weak_ptr<int>(m_lifetimeToken), data, compose,
              callback](const QHash<QString, QString> &labels) mutable {
    // Hop 4 (optional): enwiki sitelink → Wikipedia REST summary.
    if (data.enwikiTitle.isEmpty()) {
      callback(compose(labels, QString()));
      return;
    }
    getJson<QString>(
        userAgentHeader(), WikidataLogoParser::buildWikipediaSummaryUrl(data.enwikiTitle),
        [](const QByteArray &body) { return WikidataLogoParser::parseWikipediaSummary(body); },
        [alive, labels, compose, callback](ErrorUtils::Result<QString> r) mutable {
          if (alive.expired()) return;
          callback(compose(labels, r.isOk() ? r.value() : QString()));
        });
  });
}
