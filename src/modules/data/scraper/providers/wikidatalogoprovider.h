#ifndef WIKIDATALOGOPROVIDER_H
#define WIKIDATALOGOPROVIDER_H

#include <functional>

#include "providerbase.h"
#include "wikidatalogoparser.h"

#include <QString>
#include <QStringList>

struct CollectionConfig;

/// Wikidata / Wikimedia Commons logo provider (Kartend-czna3, user-directed
/// 2026-08-17): resolves a COLLECTION NAME to the organisation's logo via
/// Wikidata's P154 claim and downloads the ORIGINAL Commons file — an SVG
/// stays an SVG, so the navigation sidebar's colour/monochrome/tint styles
/// re-render it at any size from the vector.
///
/// This is an ENTITY-ONLY provider: it advertises no game categories, so
/// MetadataProviderRegistry never selects it as a collection's primary
/// scraper. Its one integration point is the entity coordinator's fallback —
/// when a collection's own provider reports a Platform entity as not-found
/// (the manufacturer-shell case: "Nintendo" is not a ScreenScraper system),
/// the coordinator retries the job through this provider with a Collection
/// target. lookup()/fetchDetail() exist only to satisfy the base contract.
class WikidataLogoProvider : public ProviderBase {
public:
  /// Resolves the collection whose logo is being fetched — the NAME is the
  /// search term. Same closure-accessor pattern as ScreenScraperProvider's
  /// CollectionAccessor: the caller owns index validity.
  using CollectionAccessor = std::function<const CollectionConfig *()>;

  /// @p isShellAccessor (optional): true when the collection is a SHELL —
  /// other collections name it as parent. Shells are named after
  /// companies/brands, so entity resolution prefers company vocabulary
  /// (Kartend-5b5r1 follow-up: the "Sony" shell must resolve to Sony
  /// Group, not a PlayStation, not the given name).
  explicit WikidataLogoProvider(CollectionAccessor collectionAccessor,
                                std::function<bool()> isShellAccessor = {});

  [[nodiscard]] QString id() const override { return QStringLiteral("wikidata"); }
  [[nodiscard]] QString displayName() const override { return QStringLiteral("Wikidata"); }
  /// Deliberately no real category: keeps the registry from ever offering
  /// this as a primary lookup provider (see class doc).
  [[nodiscard]] QStringList categories() const override { return {}; }
  [[nodiscard]] Capabilities capabilities() const override { return Capability::MediaFetch; }
  [[nodiscard]] QList<Scraper::ScrapeEntityType> supportedEntities() const override {
    return {Scraper::ScrapeEntityType::Collection};
  }

  void lookup(const QString &query, LookupCallback callback) override;
  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) override;
  void fetchMediaBytes(const QUrl &url, MediaCallback callback) override;
  void fetchEntity(const Scraper::EntityScrapeTarget &target, DetailCallback callback) override;

private:
  /// Kartend-445su: the optional text hops (manufacturer label, Wikipedia
  /// summary) and the final ScrapedItem composition, split out of
  /// fetchEntity so the four-hop chain stays readable.
  /// Kartend-6i10t: walk the search-query ladder (full name, then compound
  /// fragments) until pickEntityForCollection accepts a hit; falls back to
  /// the first raw hit of the first non-empty search, and reports an empty
  /// id when every query comes back dry. Errors forward through @p done.
  void resolveEntityId(const QStringList &queries, int index, const QString &fallbackId,
                       const QString &collectionType, bool preferCompany,
                       std::function<void(ErrorUtils::Result<QString>)> done);
  void finishEntityWithData(const QString &name, const QString &scopeKey,
                            const WikidataLogoParser::EntityData &data, DetailCallback callback);

  CollectionAccessor m_collectionAccessor;
  std::function<bool()> m_isShellAccessor;
};

#endif // WIKIDATALOGOPROVIDER_H
