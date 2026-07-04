// Tests for MetadataProviderRegistry — the curated provider list, the
// per-category filter, and the synonym normalisation. Pure logic, no
// network, no filesystem; the WebSearchProvider URL builder gets exercised
// indirectly through the curated list's searchUrl() outputs.
#include <memory>

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUrl>
#include <QVariant>

#include "collection/collectionconfig.h"
#include "metadatalookupprovider.h"
#include "metadataprovider.h"
#include "metadataproviderregistry.h"
#include "scrapertypes.h"
#include "websearchprovider.h"

class TestMetadataProviderRegistry : public QObject {
  Q_OBJECT
private slots:
  void builtIn_isNonEmptyAndAllIdsUnique();
  void builtIn_everyProviderHasWebSearchCapability();
  void builtIn_searchUrlsArePercentEncoded();
  void builtIn_includesScreenScraperForGames();
  void normaliseCategory_handlesCanonicalAndSynonyms();
  void normaliseCategory_returnsEmptyForBlankInput();
  void normaliseCategory_passesThroughUnknownLowercase();
  void forCategory_emptyCategoryReturnsEverything();
  void forCategory_filtersByCanonicalTag();
  void forCategory_filtersBySynonym();
  void forCategory_emptyCategoriesProviderAlwaysIncluded();
  void scrapingProviders_returnsOnlyLookupCapableProviders();
  void defaultScraperForType_mapsStandardTypesToProviders();
  void defaultScraperForType_emptyForUntaggedAndUnmatched();
  void webSearchProvider_emptyQueryReturnsInvalidUrl();
  void builtIn_everyProviderSupportsGameEntityByDefault();
  void entityScrapeTarget_defaultsAndMetatypeRoundTrip();
  void forEntity_resolvesGameProvidersAndEmptyForUnsupported();
  void claimLookupProvider_overrideWinsOverCategory();
  void claimLookupProvider_unusableOverrideFallsBackToCategory();
  void claimLookupProvider_picksFirstLookupCapableForCategory();
  void claimLookupProvider_untaggedFallsBackToFirstLookupCapable();
  void claimLookupProvider_unmatchedCustomTagReturnsNull();
  void claimLookupProvider_specMetadataMatchesBuiltIn();
};

namespace {

/// Minimal CollectionConfig for claimLookupProvider — the helper selects on
/// exactly two fields: the free-form `type` and the per-collection scraper
/// override id.
CollectionConfig cfgFor(const QString &type, const QString &overrideId = {}) {
  CollectionConfig cfg;
  cfg.type = type;
  cfg.scraperOverrides.scraperProviderId = overrideId;
  return cfg;
}

} // namespace

void TestMetadataProviderRegistry::builtIn_isNonEmptyAndAllIdsUnique() {
  const auto providers = MetadataProviderRegistry::builtIn();
  QVERIFY(!providers.empty());
  QSet<QString> ids;
  for (const auto &p : providers) {
    QVERIFY2(!ids.contains(p->id()), qPrintable(QString("Duplicate provider id: %1").arg(p->id())));
    ids.insert(p->id());
    // Stable lowercase ids — used as ItemMetadata.source values; a
    // mixed-case or whitespace id would land in the DB and be hard to
    // round-trip in queries.
    QVERIFY2(p->id() == p->id().toLower(),
             qPrintable(QString("Provider id not lowercase: %1").arg(p->id())));
    QVERIFY(!p->id().contains(' '));
  }
}

void TestMetadataProviderRegistry::builtIn_everyProviderHasWebSearchCapability() {
  // Stage 1 baseline — every shipped provider must support WebSearch
  // since the context menu's "Look up online" entries are URL-driven.
  // When Stage 2 API providers ship they may still set WebSearch
  // alongside their MetadataLookup capability (most APIs also expose
  // a public web search URL).
  for (const auto &p : MetadataProviderRegistry::builtIn()) {
    QVERIFY2(p->capabilities().testFlag(MetadataProvider::Capability::WebSearch),
             qPrintable(QString("Provider %1 missing WebSearch capability").arg(p->id())));
  }
}

void TestMetadataProviderRegistry::builtIn_searchUrlsArePercentEncoded() {
  // Spaces and special characters in the user's query must come out
  // percent-encoded so the URL stays valid across all the host
  // services. Use a query with a space to surface the encoding.
  for (const auto &p : MetadataProviderRegistry::builtIn()) {
    const QUrl url = p->searchUrl(QStringLiteral("super mario"));
    QVERIFY2(url.isValid(), qPrintable(QString("Invalid URL from %1").arg(p->id())));
    // Default toString() decodes the query for display — use the
    // FullyEncoded form to inspect the wire shape that QDesktopServices
    // actually transmits. Either %20 or + is acceptable; our impl
    // emits %20.
    const QString wire = url.toString(QUrl::FullyEncoded);
    QVERIFY2(wire.contains("%20") || wire.contains("+"),
             qPrintable(QString("URL not encoded for %1: %2").arg(p->id(), wire)));
    QVERIFY(!wire.contains(" "));
  }
}

void TestMetadataProviderRegistry::builtIn_includesScreenScraperForGames() {
  // ScreenScraper.fr is the marquee provider for the games category —
  // the API integration follow-up will build on this URL provider's
  // shape. Sanity-check it ships and is tagged correctly.
  bool found = false;
  for (const auto &p : MetadataProviderRegistry::builtIn()) {
    if (p->id() == QStringLiteral("screenscraper")) {
      found = true;
      QVERIFY(p->categories().contains(QStringLiteral("games")));
      const QUrl url = p->searchUrl(QStringLiteral("test"));
      QVERIFY(url.host().contains("screenscraper.fr"));
      break;
    }
  }
  QVERIFY(found);
}

void TestMetadataProviderRegistry::normaliseCategory_handlesCanonicalAndSynonyms() {
  // Canonical tags pass through unchanged.
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("games"), QStringLiteral("games"));
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("video"), QStringLiteral("video"));
  // Case insensitivity.
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("GAMES"), QStringLiteral("games"));
  // Synonyms collapse to canonical.
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("rom"), QStringLiteral("games"));
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("movies"), QStringLiteral("video"));
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("music"), QStringLiteral("audio"));
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("books"), QStringLiteral("reference"));
  // Whitespace trimmed.
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("  Games  "), QStringLiteral("games"));
}

void TestMetadataProviderRegistry::normaliseCategory_returnsEmptyForBlankInput() {
  QCOMPARE(MetadataProviderRegistry::normaliseCategory(""), QString());
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("   "), QString());
}

void TestMetadataProviderRegistry::normaliseCategory_passesThroughUnknownLowercase() {
  // Custom user tags (not in the synonym table) survive normalisation
  // as lowercase — providers can still opt into them by listing the
  // exact tag in their categories(). This keeps the door open for
  // user-defined collection types without forcing them into a canonical
  // bucket.
  QCOMPARE(MetadataProviderRegistry::normaliseCategory("HomeBrew"), QStringLiteral("homebrew"));
}

void TestMetadataProviderRegistry::forCategory_emptyCategoryReturnsEverything() {
  const auto providers = MetadataProviderRegistry::builtIn();
  const auto filtered = MetadataProviderRegistry::forCategory(providers, QString());
  QCOMPARE(static_cast<size_t>(filtered.size()), providers.size());
}

void TestMetadataProviderRegistry::forCategory_filtersByCanonicalTag() {
  const auto providers = MetadataProviderRegistry::builtIn();
  const auto games = MetadataProviderRegistry::forCategory(providers, QStringLiteral("games"));
  QVERIFY(!games.isEmpty());
  for (auto *p : games) {
    QVERIFY(p->categories().contains(QStringLiteral("games")) || p->categories().isEmpty());
  }
  // Audio providers must NOT appear in the games filter.
  for (auto *p : games) {
    QVERIFY(!p->categories().contains(QStringLiteral("audio")));
  }
}

void TestMetadataProviderRegistry::forCategory_filtersBySynonym() {
  // "movies" should resolve to the video tag and surface TMDB / IMDb.
  const auto providers = MetadataProviderRegistry::builtIn();
  const auto movies = MetadataProviderRegistry::forCategory(providers, QStringLiteral("movies"));
  QVERIFY(!movies.isEmpty());
  QStringList ids;
  for (auto *p : movies) {
    ids.append(p->id());
  }
  QVERIFY(ids.contains(QStringLiteral("tmdb")));
  QVERIFY(ids.contains(QStringLiteral("imdb")));
}

void TestMetadataProviderRegistry::forCategory_emptyCategoriesProviderAlwaysIncluded() {
  // A provider with empty categories() (applies-everywhere) must show
  // up in every filter call. Stage 1 has no such provider in the
  // curated list, so build a one-off and run forCategory directly.
  std::vector<std::unique_ptr<MetadataProvider>> all;
  all.push_back(std::make_unique<WebSearchProvider>(QStringLiteral("anywhere"),
                                                    QStringLiteral("Anywhere"), QStringList{},
                                                    QStringLiteral("https://example.com/?q=%1")));
  const auto filtered = MetadataProviderRegistry::forCategory(all, QStringLiteral("games"));
  QCOMPARE(filtered.size(), 1);
  QCOMPARE(filtered.first()->id(), QStringLiteral("anywhere"));
}

void TestMetadataProviderRegistry::scrapingProviders_returnsOnlyLookupCapableProviders() {
  // The scraper picker in the collection dialogs offers only providers
  // that can run an actual metadata scrape (MetadataLookup) — pinning a
  // URL-only "look up online" provider as a collection's scraper would
  // be a no-op.
  const auto choices = MetadataProviderRegistry::scrapingProviders();
  QVERIFY(!choices.isEmpty());
  QSet<QString> ids;
  for (const auto &c : choices) {
    QVERIFY(!c.id.isEmpty());
    QVERIFY(!c.displayName.isEmpty());
    ids.insert(c.id);
  }
  // The four API providers are lookup-capable.
  QVERIFY(ids.contains(QStringLiteral("screenscraper")));
  QVERIFY(ids.contains(QStringLiteral("tmdb")));
  QVERIFY(ids.contains(QStringLiteral("musicbrainz")));
  QVERIFY(ids.contains(QStringLiteral("openlibrary")));
  // URL-only providers are excluded.
  QVERIFY(!ids.contains(QStringLiteral("mobygames")));
  QVERIFY(!ids.contains(QStringLiteral("imdb")));
  QVERIFY(!ids.contains(QStringLiteral("discogs")));
  // Every returned id must be a real, lookup-capable built-in provider.
  const auto all = MetadataProviderRegistry::builtIn();
  for (const auto &c : choices) {
    bool found = false;
    for (const auto &p : all) {
      if (p->id() == c.id) {
        found = true;
        QVERIFY2(p->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup),
                 qPrintable(QString("scrapingProviders surfaced non-lookup id: %1").arg(c.id)));
      }
    }
    QVERIFY2(found, qPrintable(QString("scrapingProviders id not in builtIn: %1").arg(c.id)));
  }
}

void TestMetadataProviderRegistry::defaultScraperForType_mapsStandardTypesToProviders() {
  // The creation dialog uses these mappings to auto-associate a scraper
  // from the chosen media type.
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType("Video"), QStringLiteral("tmdb"));
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType("Audio"), QStringLiteral("musicbrainz"));
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType("Games"),
           QStringLiteral("screenscraper"));
  // "Documents" resolves through the synonym table to the reference
  // category, whose lookup provider is Open Library.
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType("Documents"),
           QStringLiteral("openlibrary"));
  // A synonym of a standard type resolves identically.
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType("Movies"), QStringLiteral("tmdb"));
  // Case-insensitive.
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType("games"),
           QStringLiteral("screenscraper"));
}

void TestMetadataProviderRegistry::defaultScraperForType_emptyForUntaggedAndUnmatched() {
  // Untagged collection — no type, no default scraper.
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType(""), QString());
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType("   "), QString());
  // "Images" is a standard media type but ships no image scraper.
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType("Images"), QString());
  // A custom tag matching no provider category resolves to empty — the
  // dialog then leaves the scraper on Automatic for the user to pick.
  QCOMPARE(MetadataProviderRegistry::defaultScraperForType("Homebrew"), QString());
}

void TestMetadataProviderRegistry::webSearchProvider_emptyQueryReturnsInvalidUrl() {
  WebSearchProvider p(QStringLiteral("test"), QStringLiteral("Test"), {QStringLiteral("games")},
                      QStringLiteral("https://example.com/?q=%1"));
  QVERIFY(!p.searchUrl("").isValid());
  QVERIFY(!p.searchUrl("   ").isValid());
  QVERIFY(p.searchUrl("hello").isValid());
}

void TestMetadataProviderRegistry::builtIn_everyProviderSupportsGameEntityByDefault() {
  // Kartend-ckepd.1: supportedEntities() defaults to {Game}; no shipped
  // provider has opted into Platform/Collection/Category yet, so every curated
  // provider must still report Game (and nothing else has regressed it away).
  for (const auto &p : MetadataProviderRegistry::builtIn()) {
    QVERIFY2(p->supportedEntities().contains(Scraper::ScrapeEntityType::Game),
             qPrintable(QString("Provider %1 does not support the Game entity").arg(p->id())));
  }
}

void TestMetadataProviderRegistry::entityScrapeTarget_defaultsAndMetatypeRoundTrip() {
  // Default target is a Game with no identity and an unset collection index.
  const Scraper::EntityScrapeTarget def;
  QCOMPARE(def.type, Scraper::ScrapeEntityType::Game);
  QVERIFY(def.identity.isEmpty());
  QCOMPARE(def.collectionIndex, -1);
  // Metatype is registered (Q_DECLARE_METATYPE) so the descriptor can ride in a
  // CollectionJob / queued signal once entity jobs are dispatched.
  const Scraper::EntityScrapeTarget t{Scraper::ScrapeEntityType::Platform, QStringLiteral("42"), 3};
  const QVariant v = QVariant::fromValue(t);
  QVERIFY(v.isValid());
  const auto rt = v.value<Scraper::EntityScrapeTarget>();
  QCOMPARE(rt.type, Scraper::ScrapeEntityType::Platform);
  QCOMPARE(rt.identity, QStringLiteral("42"));
  QCOMPARE(rt.collectionIndex, 3);
}

void TestMetadataProviderRegistry::forEntity_resolvesGameProvidersAndEmptyForUnsupported() {
  // Kartend-ckepd.2: forEntity filters by supportedEntities(). Every provider
  // supports Game. ScreenScraper opted into Platform (Kartend-ckepd.4) and TMDB
  // into Collection (Kartend-ckepd.5) — each the only one so far. Category has
  // no upstream source and is deliberately unsupported by every provider.
  const auto all = MetadataProviderRegistry::builtIn();
  const auto gameProviders =
      MetadataProviderRegistry::forEntity(all, Scraper::ScrapeEntityType::Game);
  QCOMPARE(gameProviders.size(), static_cast<int>(all.size()));
  const auto platformProviders =
      MetadataProviderRegistry::forEntity(all, Scraper::ScrapeEntityType::Platform);
  QCOMPARE(platformProviders.size(), 1);
  QCOMPARE(platformProviders.first()->id(), QStringLiteral("screenscraper"));
  const auto collectionProviders =
      MetadataProviderRegistry::forEntity(all, Scraper::ScrapeEntityType::Collection);
  QCOMPARE(collectionProviders.size(), 1);
  QCOMPARE(collectionProviders.first()->id(), QStringLiteral("tmdb"));
  QVERIFY(MetadataProviderRegistry::forEntity(all, Scraper::ScrapeEntityType::Category).isEmpty());
}

void TestMetadataProviderRegistry::claimLookupProvider_overrideWinsOverCategory() {
  // An explicit per-collection override naming a lookup-capable provider wins
  // even when the collection type would resolve to a different provider.
  const auto claimed = MetadataProviderRegistry::claimLookupProvider(
      cfgFor(QStringLiteral("games"), QStringLiteral("tmdb")));
  QVERIFY(claimed);
  QCOMPARE(claimed->id(), QStringLiteral("tmdb"));
  QVERIFY(claimed->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup));
  // The persisted override may carry stray whitespace — trimmed before match
  // (parity with the old ScraperController picker, which trimmed at the call
  // site).
  const auto padded = MetadataProviderRegistry::claimLookupProvider(
      cfgFor(QStringLiteral("games"), QStringLiteral("  tmdb  ")));
  QVERIFY(padded);
  QCOMPARE(padded->id(), QStringLiteral("tmdb"));
}

void TestMetadataProviderRegistry::claimLookupProvider_unusableOverrideFallsBackToCategory() {
  // An override naming a URL-only provider (imdb has no MetadataLookup) is
  // unusable for a scrape — fall through to the category path.
  const auto urlOnly = MetadataProviderRegistry::claimLookupProvider(
      cfgFor(QStringLiteral("video"), QStringLiteral("imdb")));
  QVERIFY(urlOnly);
  QCOMPARE(urlOnly->id(), QStringLiteral("tmdb"));
  // An override naming no registered provider at all falls back the same way.
  const auto unknown = MetadataProviderRegistry::claimLookupProvider(
      cfgFor(QStringLiteral("video"), QStringLiteral("no-such-provider")));
  QVERIFY(unknown);
  QCOMPARE(unknown->id(), QStringLiteral("tmdb"));
}

void TestMetadataProviderRegistry::claimLookupProvider_picksFirstLookupCapableForCategory() {
  // No override: the first lookup-capable provider whose category matches the
  // (normalised) collection type wins — same mapping the creation dialog's
  // defaultScraperForType exposes.
  QCOMPARE(MetadataProviderRegistry::claimLookupProvider(cfgFor(QStringLiteral("games")))->id(),
           QStringLiteral("screenscraper"));
  // Synonym ("movies" → video) and case-insensitivity resolve identically.
  QCOMPARE(MetadataProviderRegistry::claimLookupProvider(cfgFor(QStringLiteral("Movies")))->id(),
           QStringLiteral("tmdb"));
  QCOMPARE(MetadataProviderRegistry::claimLookupProvider(cfgFor(QStringLiteral("Audio")))->id(),
           QStringLiteral("musicbrainz"));
  QCOMPARE(MetadataProviderRegistry::claimLookupProvider(cfgFor(QStringLiteral("books")))->id(),
           QStringLiteral("openlibrary"));
}

void TestMetadataProviderRegistry::claimLookupProvider_untaggedFallsBackToFirstLookupCapable() {
  // Untagged collection: forCategory("") surfaces the full menu, so the claim
  // resolves to the first lookup-capable provider in display order.
  const auto claimed = MetadataProviderRegistry::claimLookupProvider(cfgFor(QString()));
  QVERIFY(claimed);
  QCOMPARE(claimed->id(), QStringLiteral("screenscraper"));
}

void TestMetadataProviderRegistry::claimLookupProvider_unmatchedCustomTagReturnsNull() {
  // A custom tag matching no provider category resolves to nothing — the
  // caller reports "no provider applies" instead of scraping with a random
  // provider.
  QVERIFY(!MetadataProviderRegistry::claimLookupProvider(cfgFor(QStringLiteral("homebrew"))));
}

void TestMetadataProviderRegistry::claimLookupProvider_specMetadataMatchesBuiltIn() {
  // Drift guard: claimLookupProvider selects on the registry's static spec
  // table WITHOUT constructing the non-matching providers, so the table's
  // id / lookup-capability / category mirrors of the provider classes must
  // stay in sync with what the constructed instances actually report.
  //
  // Per-id + capability check: claiming with an unmatched type and an
  // explicit override must succeed exactly for the lookup-capable providers
  // (the unmatched type makes the category fallback yield null, isolating
  // the override path).
  const QString unmatched = QStringLiteral("zz-no-such-category");
  for (const auto &p : MetadataProviderRegistry::builtIn()) {
    const auto claimed = MetadataProviderRegistry::claimLookupProvider(cfgFor(unmatched, p->id()));
    const bool lookupCapable =
        p->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup);
    if (lookupCapable) {
      QVERIFY2(claimed, qPrintable(QString("claim failed for lookup-capable %1").arg(p->id())));
      QCOMPARE(claimed->id(), p->id());
      QVERIFY(claimed->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup));
    } else {
      QVERIFY2(!claimed,
               qPrintable(QString("claim built a non-lookup provider for %1").arg(p->id())));
    }
  }
  // Category check: for every canonical tag the claim must land on the same
  // provider defaultScraperForType computes from the constructed instances'
  // categories() lists.
  const QStringList canonical = {QStringLiteral("games"), QStringLiteral("video"),
                                 QStringLiteral("audio"), QStringLiteral("reference")};
  for (const QString &cat : canonical) {
    const auto claimed = MetadataProviderRegistry::claimLookupProvider(cfgFor(cat));
    QVERIFY2(claimed, qPrintable(QString("no claim for canonical category %1").arg(cat)));
    QCOMPARE(claimed->id(), MetadataProviderRegistry::defaultScraperForType(cat));
  }
}

QTEST_MAIN(TestMetadataProviderRegistry)
#include "test_metadataproviderregistry.moc"
