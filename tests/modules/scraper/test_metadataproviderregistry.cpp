// Tests for MetadataProviderRegistry — the curated provider list, the
// per-category filter, and the synonym normalisation. Pure logic, no
// network, no filesystem; the WebSearchProvider URL builder gets exercised
// indirectly through the curated list's searchUrl() outputs.
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUrl>

#include "metadataprovider.h"
#include "metadataproviderregistry.h"
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
};

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

QTEST_MAIN(TestMetadataProviderRegistry)
#include "test_metadataproviderregistry.moc"
