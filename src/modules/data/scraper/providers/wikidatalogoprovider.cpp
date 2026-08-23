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

WikidataLogoProvider::WikidataLogoProvider(CollectionAccessor collectionAccessor)
    : m_collectionAccessor(std::move(collectionAccessor)) {
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

  // Hop 1: name → entity id. Continuations guard on the base lifetime token
  // (cr950 class): HttpClient holds them with no QObject severing, so a
  // provider destroyed mid-flight must not be touched.
  getJson<QString>(
      userAgentHeader(), WikidataLogoParser::buildSearchUrl(name),
      [](const QByteArray &body) { return WikidataLogoParser::parseEntitySearch(body); },
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
              finishEntityWithData(name, scopeKey, data, std::move(callback));
            });
      });
}

// Compose the ScrapedItem from the resolved entity data, chasing the two
// optional text hops first (manufacturer label, Wikipedia summary). Each hop
// degrades gracefully: a failed label leaves the manufacturer unset, a
// failed summary falls back to Wikidata's one-line description.
void WikidataLogoProvider::finishEntityWithData(const QString &name, const QString &scopeKey,
                                                const WikidataLogoParser::EntityData &data,
                                                DetailCallback callback) {
  auto compose = [name, scopeKey, data](const QString &manufacturer, const QString &summary) {
    Scraper::ScrapedItem item;
    item.sourceProviderId = QStringLiteral("wikidata");
    item.title = name;
    // Wikipedia's prose paragraph outranks Wikidata's one-liner; the
    // one-liner outranks nothing.
    item.description = !summary.isEmpty() ? summary : data.description;
    if (!manufacturer.isEmpty()) {
      item.customFields.insert(QLatin1String(EntityMetadataStore::kFieldManufacturer),
                               manufacturer);
    }
    if (!data.inceptionYear.isEmpty()) {
      item.customFields.insert(QLatin1String(EntityMetadataStore::kFieldReleaseDate),
                               data.inceptionYear);
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
    return item;
  };

  // Hop 3 (optional): manufacturer entity → English label.
  auto withManufacturer = [this, alive = std::weak_ptr<int>(m_lifetimeToken), data, compose,
                           callback](auto &&then) mutable {
    if (data.manufacturerId.isEmpty()) {
      then(QString());
      return;
    }
    getJson<QString>(
        userAgentHeader(), WikidataLogoParser::buildLabelUrl(data.manufacturerId),
        [](const QByteArray &body) { return WikidataLogoParser::parseEntityLabel(body); },
        [alive, then = std::forward<decltype(then)>(then)](ErrorUtils::Result<QString> r) mutable {
          if (alive.expired()) return;
          // Degrade, don't fail: the label is garnish on an already-usable
          // result.
          then(r.isOk() ? r.value() : QString());
        });
  };

  withManufacturer([this, alive = std::weak_ptr<int>(m_lifetimeToken), data, compose,
                    callback](const QString &manufacturer) mutable {
    // Hop 4 (optional): enwiki sitelink → Wikipedia REST summary.
    if (data.enwikiTitle.isEmpty()) {
      callback(compose(manufacturer, QString()));
      return;
    }
    getJson<QString>(
        userAgentHeader(), WikidataLogoParser::buildWikipediaSummaryUrl(data.enwikiTitle),
        [](const QByteArray &body) { return WikidataLogoParser::parseWikipediaSummary(body); },
        [alive, manufacturer, compose, callback](ErrorUtils::Result<QString> r) mutable {
          if (alive.expired()) return;
          callback(compose(manufacturer, r.isOk() ? r.value() : QString()));
        });
  });
}
