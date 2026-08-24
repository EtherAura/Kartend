// Tests for the Wikidata logo pipeline (Kartend-czna3): the pure wire-shape
// helpers in WikidataLogoParser plus the provider's two-hop fetchEntity
// orchestration, driven headless through ProviderBase's fetch seam. Fixture
// names are generic placeholders per the no-real-identifiers convention.
#include <QObject>
#include <QString>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include "collection/collectionconfig.h"
#include "entitymetadata.h"
#include "wikidatalogoparser.h"
#include "wikidatalogoprovider.h"

class TestWikidataLogoProvider : public QObject {
  Q_OBJECT
private slots:
  void cleanup();

  void buildSearchUrl_encodesNameEmptyForBlank();
  void buildLogoFileUrl_canonicalisesAndRejectsUnsafe();
  void parseEntitySearch_firstHitEmptyAndMalformed();
  void parseLogoClaim_stringShapeFilePrefixAndHostileSkipped();
  void parseEntityData_fullSparseAndMalformed();
  void parseEntityLabel_extractsEnglishLabel();
  void parseWikipediaSummary_extractAndDisambiguation();
  void buildDataUrls_shapeAndEncoding();

  void fetchEntity_happyPathBuildsSvgLogoAsset();
  void fetchEntity_dataRichEntityComposes();
  void parseEntityData_countryFallbackAndWebsiteSchemeGuard();
  void searchQueryLadder_compoundNamesFragment();
  void pickEntityForCollection_prefersMediaTypeMatch();
  void fetchEntity_ambiguousNameResolvesToConsoleNotPlanet();
  void fetchEntity_compoundNameFallsBackToFragmentQuery();
  void fetchEntity_noEntityIsNotFound();
  void fetchEntity_wrongTargetTypeIsInvalidArgument();
};

void TestWikidataLogoProvider::parseEntityData_fullSparseAndMalformed() {
  // Kartend-445su: one wbgetentities response carries logo + manufacturer +
  // inception + description + enwiki sitelink.
  const QByteArray full = R"({"entities":{"Q9622":{
    "descriptions":{"en":{"value":"home video game console"}},
    "sitelinks":{"enwiki":{"title":"Sega Genesis"}},
    "claims":{
      "P154":[{"mainsnak":{"datavalue":{"value":"Sega-Mega-Drive.svg"}}}],
      "P176":[{"mainsnak":{"datavalue":{"value":{"id":"Q122741"}}}}],
      "P571":[{"mainsnak":{"datavalue":{"value":{"time":"+1988-10-29T00:00:00Z"}}}}]
    }}}})";
  auto parsed = WikidataLogoParser::parseEntityData(full);
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value().logoFilename, QStringLiteral("Sega-Mega-Drive.svg"));
  QCOMPARE(parsed.value().manufacturerId, QStringLiteral("Q122741"));
  QCOMPARE(parsed.value().inceptionYear, QStringLiteral("1988"));
  QCOMPARE(parsed.value().description, QStringLiteral("home video game console"));
  QCOMPARE(parsed.value().enwikiTitle, QStringLiteral("Sega Genesis"));

  // Sparse entity: everything optional parses empty, no error. A hostile
  // logo filename is rejected exactly like parseLogoClaim rejects it.
  const QByteArray sparse = R"({"entities":{"Q1":{
    "claims":{"P154":[{"mainsnak":{"datavalue":{"value":"../../evil.svg"}}}]}}}})";
  auto sparseParsed = WikidataLogoParser::parseEntityData(sparse);
  QVERIFY(sparseParsed.isOk());
  QVERIFY(sparseParsed.value().logoFilename.isEmpty());
  QVERIFY(sparseParsed.value().manufacturerId.isEmpty());
  QVERIFY(sparseParsed.value().description.isEmpty());

  QVERIFY(WikidataLogoParser::parseEntityData(QByteArrayLiteral("nonsense")).isError());
}

void TestWikidataLogoProvider::parseEntityLabel_extractsEnglishLabel() {
  const QByteArray body = R"({"entities":{"Q122741":{"labels":{"en":{"value":"Sega"}}}}})";
  auto parsed = WikidataLogoParser::parseEntityLabel(body);
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value(), QStringLiteral("Sega"));
  auto missing = WikidataLogoParser::parseEntityLabel(QByteArrayLiteral(R"({"entities":{}})"));
  QVERIFY(missing.isOk());
  QVERIFY(missing.value().isEmpty());
}

void TestWikidataLogoProvider::parseWikipediaSummary_extractAndDisambiguation() {
  const QByteArray body =
      R"({"type":"standard","extract":"The Sega Genesis is a 16-bit console."})";
  auto parsed = WikidataLogoParser::parseWikipediaSummary(body);
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value(), QStringLiteral("The Sega Genesis is a 16-bit console."));
  // A disambiguation page's extract is a list of unrelated meanings — the
  // parser must refuse it rather than hand the pane word salad.
  const QByteArray disambig = R"({"type":"disambiguation","extract":"Genesis may refer to:"})";
  auto refused = WikidataLogoParser::parseWikipediaSummary(disambig);
  QVERIFY(refused.isOk());
  QVERIFY(refused.value().isEmpty());
}

void TestWikidataLogoProvider::buildDataUrls_shapeAndEncoding() {
  const QUrl data = WikidataLogoParser::buildEntityDataUrl(QStringLiteral("Q9622"));
  QVERIFY(data.toString().contains(QStringLiteral("wbgetentities")));
  QVERIFY(data.toString().contains(QStringLiteral("claims%7Csitelinks%7Cdescriptions")) ||
          data.toString().contains(QStringLiteral("claims|sitelinks|descriptions")));
  const QUrl label = WikidataLogoParser::buildLabelUrl(QStringLiteral("Q122741"));
  QVERIFY(label.toString().contains(QStringLiteral("props=labels")));
  const QUrl wiki = WikidataLogoParser::buildWikipediaSummaryUrl(QStringLiteral("Sega Genesis"));
  QCOMPARE(wiki.host(), QStringLiteral("en.wikipedia.org"));
  QVERIFY(wiki.path().endsWith(QStringLiteral("Sega_Genesis")));
  QVERIFY(WikidataLogoParser::buildWikipediaSummaryUrl(QString()).isEmpty());
}

void TestWikidataLogoProvider::cleanup() {
  ProviderBase::setFetchFunctionForTesting({});
}

void TestWikidataLogoProvider::buildSearchUrl_encodesNameEmptyForBlank() {
  QVERIFY(WikidataLogoParser::buildSearchUrl(QStringLiteral("   ")).isEmpty());
  const QUrl url = WikidataLogoParser::buildSearchUrl(QStringLiteral("Maker Corp"));
  QCOMPARE(url.host(), QStringLiteral("www.wikidata.org"));
  const QUrlQuery q(url);
  QCOMPARE(q.queryItemValue(QStringLiteral("action")), QStringLiteral("wbsearchentities"));
  QCOMPARE(q.queryItemValue(QStringLiteral("search")), QStringLiteral("Maker Corp"));
  QCOMPARE(q.queryItemValue(QStringLiteral("type")), QStringLiteral("item"));
}

void TestWikidataLogoProvider::buildLogoFileUrl_canonicalisesAndRejectsUnsafe() {
  // Spaces canonicalise to underscores; the path targets Special:FilePath.
  const QUrl url = WikidataLogoParser::buildLogoFileUrl(QStringLiteral("Maker logo.svg"));
  QCOMPARE(url.host(), QStringLiteral("commons.wikimedia.org"));
  QVERIFY(url.toString().endsWith(QStringLiteral("Special:FilePath/Maker_logo.svg")));
  // Traversal / separators / extension-less names never become URLs.
  QVERIFY(WikidataLogoParser::buildLogoFileUrl(QStringLiteral("../etc/passwd")).isEmpty());
  QVERIFY(WikidataLogoParser::buildLogoFileUrl(QStringLiteral("a/b.svg")).isEmpty());
  QVERIFY(WikidataLogoParser::buildLogoFileUrl(QStringLiteral("noextension")).isEmpty());
}

void TestWikidataLogoProvider::parseEntitySearch_firstHitEmptyAndMalformed() {
  auto hit = WikidataLogoParser::parseEntitySearch(
      QByteArrayLiteral(R"({"search":[{"id":"Q42","label":"Maker"},{"id":"Q43"}]})"));
  QVERIFY(hit.isOk());
  QCOMPARE(hit.value(), QStringLiteral("Q42"));

  auto none = WikidataLogoParser::parseEntitySearch(QByteArrayLiteral(R"({"search":[]})"));
  QVERIFY(none.isOk());
  QVERIFY(none.value().isEmpty()); // benign not-found, not an error

  QVERIFY(WikidataLogoParser::parseEntitySearch(QByteArrayLiteral("<html>")).isError());
}

void TestWikidataLogoProvider::parseLogoClaim_stringShapeFilePrefixAndHostileSkipped() {
  // P154's value is the bare Commons filename; a "File:" prefix is stripped;
  // a hostile first claim is SKIPPED in favour of the next usable one.
  auto logo = WikidataLogoParser::parseLogoClaim(QByteArrayLiteral(R"({
    "claims": {"P154": [
      {"mainsnak": {"datavalue": {"value": "../evil.svg"}}},
      {"mainsnak": {"datavalue": {"value": "File:Maker logo.svg"}}}
    ]}})"));
  QVERIFY(logo.isOk());
  QCOMPARE(logo.value(), QStringLiteral("Maker logo.svg"));

  auto claimless = WikidataLogoParser::parseLogoClaim(QByteArrayLiteral(R"({"claims":{}})"));
  QVERIFY(claimless.isOk());
  QVERIFY(claimless.value().isEmpty()); // entity without a logo — benign
}

void TestWikidataLogoProvider::fetchEntity_happyPathBuildsSvgLogoAsset() {
  ProviderBase::setFetchFunctionForTesting([](const QUrl &url,
                                              const Scraper::HttpClient::RawHeaders &,
                                              Scraper::HttpClient::ResponseCallback cb,
                                              const QStringList &) {
    const QString s = url.toString();
    if (s.contains(QStringLiteral("wbsearchentities"))) {
      cb(QByteArrayLiteral(R"({"search":[{"id":"Q42"}]})"));
    } else if (s.contains(QStringLiteral("wbgetentities"))) {
      // Kartend-445su: the logo hop grew into the DATA hop — one
      // wbgetentities response. Logo-only entity (no manufacturer, no
      // sitelink) keeps this the pure-art happy path; the data-rich
      // composition is covered by fetchEntity_dataRichEntityComposes.
      cb(QByteArrayLiteral(
          R"({"entities":{"Q42":{"claims":{"P154":[{"mainsnak":{"datavalue":{"value":"Maker logo.svg"}}}]}}}})"));
    } else {
      QFAIL(qPrintable(QStringLiteral("unexpected fetch: %1").arg(s)));
    }
  });

  CollectionConfig cfg;
  cfg.name = QStringLiteral("Maker");
  WikidataLogoProvider provider([&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("abc123");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.sourceProviderId, QStringLiteral("wikidata"));
  QCOMPARE(item.media.size(), 1);
  const Scraper::MediaAsset &asset = item.media.first();
  // SVG filename → the logo-svg type dir the tree's silhouette probing
  // prefers; role Logo wires collectionIcon; Collection scope + uuid key
  // give the `_shared/logo-svg/collection_abc123.svg` on-disk shape.
  QCOMPARE(asset.type, QStringLiteral("logo-svg"));
  QCOMPARE(asset.scope, Scraper::MediaScope::Collection);
  QCOMPARE(asset.scopeKey, QStringLiteral("abc123"));
  QCOMPARE(asset.entityRole, Scraper::EntityArtRole::Logo);
  QCOMPARE(asset.url.host(), QStringLiteral("commons.wikimedia.org"));
}

void TestWikidataLogoProvider::fetchEntity_dataRichEntityComposes() {
  // Kartend-445su end-to-end: search -> entity data -> manufacturer label ->
  // Wikipedia summary. The composed item carries the prose description (the
  // summary outranks Wikidata's one-liner), the manufacturer label under the
  // store's well-known key, the inception year, and the logo asset.
  ProviderBase::setFetchFunctionForTesting(
      [](const QUrl &url, const Scraper::HttpClient::RawHeaders &,
         Scraper::HttpClient::ResponseCallback cb, const QStringList &) {
        const QString s = url.toString();
        if (s.contains(QStringLiteral("wbsearchentities"))) {
          cb(QByteArrayLiteral(R"({"search":[{"id":"Q9622"}]})"));
        } else if (s.contains(QStringLiteral("ids=Q9622"))) {
          cb(QByteArrayLiteral(R"({"entities":{"Q9622":{
            "descriptions":{"en":{"value":"one-line fallback"}},
            "sitelinks":{"enwiki":{"title":"Sega Genesis"}},
            "claims":{
              "P154":[{"mainsnak":{"datavalue":{"value":"Sega logo.svg"}}}],
              "P18":[{"mainsnak":{"datavalue":{"value":"Sega-Genesis-Mk2.jpg"}}}],
              "P176":[{"mainsnak":{"datavalue":{"value":{"id":"Q122741"}}}}],
              "P495":[{"mainsnak":{"datavalue":{"value":{"id":"Q17"}}}}],
              "P178":[{"mainsnak":{"datavalue":{"value":{"id":"Q122741"}}}}],
              "P136":[{"mainsnak":{"datavalue":{"value":{"id":"Q8076"}}}}],
              "P856":[{"mainsnak":{"datavalue":{"value":"https://sega.example"}}}],
              "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q756290"}}}}],
              "P2560":[{"mainsnak":{"datavalue":{"value":{"id":"Q98120"}}}}],
              "P361":[{"mainsnak":{"datavalue":{"value":{"id":"Q99999"}}}},
                      {"mainsnak":{"datavalue":{"value":{"id":"Q7783"}}}}],
              "P2664":[{"mainsnak":{"datavalue":{"value":{"amount":"+30750000"}}}}],
              "P577":[{"mainsnak":{"datavalue":{"value":{"time":"+1989-08-14T00:00:00Z"}}}},
                      {"mainsnak":{"datavalue":{"value":{"time":"+1988-10-29T00:00:00Z"}}}}],
              "P2669":[{"mainsnak":{"datavalue":{"value":{"time":"+1997-00-00T00:00:00Z"}}}}],
              "P571":[{"mainsnak":{"datavalue":{"value":{"time":"+1986-10-29T00:00:00Z"}}}}]
            }}}})"));
        } else if (s.contains(QStringLiteral("ids=Q122741"))) {
          // Kartend-6i10t: ONE batched labels request for every referenced
          // entity — manufacturer, country, developer, genre — not one hop
          // per id. The batched URL joins ids with '|'.
          QVERIFY2(s.contains(QStringLiteral("Q17")) && s.contains(QStringLiteral("Q8076")),
                   qPrintable(QStringLiteral("labels hop not batched: %1").arg(s)));
          cb(QByteArrayLiteral(R"({"entities":{
            "Q122741":{"labels":{"en":{"value":"Sega"}}},
            "Q17":{"labels":{"en":{"value":"Japan"}}},
            "Q8076":{"labels":{"en":{"value":"Console game"}}},
            "Q756290":{"labels":{"en":{"value":"Motorola 68000"}}},
            "Q98120":{"labels":{"en":{"value":"Yamaha YM7101"}}},
            "Q99999":{"labels":{"en":{"value":"Sega hardware family"}}},
            "Q7783":{"labels":{"en":{"value":"fourth generation of video game consoles"}}}}})"));
        } else if (s.contains(QStringLiteral("en.wikipedia.org"))) {
          cb(QByteArrayLiteral(
              R"({"type":"standard","extract":"The Sega Genesis is a 16-bit console."})"));
        } else {
          QFAIL(qPrintable(QStringLiteral("unexpected fetch: %1").arg(s)));
        }
      });

  CollectionConfig cfg;
  cfg.name = QStringLiteral("Mega Drive");
  WikidataLogoProvider provider([&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("uuid-md");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.description, QStringLiteral("The Sega Genesis is a 16-bit console."));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldManufacturer)),
           QStringLiteral("Sega"));
  // Release span asserted below with the rest of the spec sheet
  // (Kartend-5b5r1 made P577+P2669 beat the bare P571 year).
  // Kartend-6i10t: the wider fact set — labels resolved from the batched
  // hop; developer shares the manufacturer's id and label; the website is a
  // plain string with no hop.
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldCountry)),
           QStringLiteral("Japan"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldDeveloper)),
           QStringLiteral("Sega"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldGenre)),
           QStringLiteral("Console game"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldWebsite)),
           QStringLiteral("https://sega.example"));
  QVERIFY(!item.customFields.contains(QLatin1String(EntityMetadataStore::kFieldPublisher)));
  // Kartend-5b5r1: the spec sheet. Release span composes P577's EARLIEST
  // year with P2669's end year, beating P571 inception; the generation is
  // the P361 value whose label names one; units sold keeps raw digits.
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldCpu)),
           QStringLiteral("Motorola 68000"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldGpu)),
           QStringLiteral("Yamaha YM7101"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldGeneration)),
           QStringLiteral("fourth generation of video game consoles"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldUnitsSold)),
           QStringLiteral("30750000"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldReleaseDate)),
           QStringLiteral("1988–1997"));
  // Logo + the P18 console photo (role None → shared art, no config slot).
  QCOMPARE(item.media.size(), 2);
  QCOMPARE(item.media.first().type, QStringLiteral("logo-svg"));
  QCOMPARE(item.media.last().type, QStringLiteral("photo"));
  QCOMPARE(item.media.last().entityRole, Scraper::EntityArtRole::None);
}

void TestWikidataLogoProvider::parseEntityData_countryFallbackAndWebsiteSchemeGuard() {
  // P495 absent → P17 fills countryId; a javascript: "website" is refused
  // (the value becomes a clickable anchor in the sidebar). buildLabelsUrl
  // dedups repeated ids into one query parameter.
  const QByteArray json = QByteArrayLiteral(R"json({"entities":{"Q1":{
    "claims":{
      "P17":[{"mainsnak":{"datavalue":{"value":{"id":"Q30"}}}}],
      "P856":[{"mainsnak":{"datavalue":{"value":"javascript:alert(1)"}}}]
    }}}})json");
  const auto parsed = WikidataLogoParser::parseEntityData(json);
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value().countryId, QStringLiteral("Q30"));
  QVERIFY(parsed.value().websiteUrl.isEmpty());

  const QUrl url = WikidataLogoParser::buildLabelsUrl(
      {QStringLiteral("Q30"), QStringLiteral("Q30"), QStringLiteral("Q31")});
  QVERIFY(url.toString().contains(QStringLiteral("Q30%7CQ31")) ||
          url.toString().contains(QStringLiteral("Q30|Q31")));
}

void TestWikidataLogoProvider::searchQueryLadder_compoundNamesFragment() {
  const QStringList ladder = WikidataLogoParser::searchQueryLadder(
      QStringLiteral("Famicom - Nintendo Entertainment System"));
  QCOMPARE(ladder, (QStringList{QStringLiteral("Famicom - Nintendo Entertainment System"),
                                QStringLiteral("Famicom"),
                                QStringLiteral("Nintendo Entertainment System")}));
  // A simple name yields just itself; hyphenated single words don't split.
  QCOMPARE(WikidataLogoParser::searchQueryLadder(QStringLiteral("Saturn")),
           (QStringList{QStringLiteral("Saturn")}));
  QCOMPARE(WikidataLogoParser::searchQueryLadder(QStringLiteral("Neo-Geo")),
           (QStringList{QStringLiteral("Neo-Geo")}));
}

void TestWikidataLogoProvider::pickEntityForCollection_prefersMediaTypeMatch() {
  const QList<WikidataLogoParser::SearchHit> hits = {
      {QStringLiteral("Q193"), QStringLiteral("Saturn"),
       QStringLiteral("sixth planet from the Sun"), QStringLiteral("Saturn")},
      {QStringLiteral("Q200912"), QStringLiteral("Sega Saturn"),
       QStringLiteral("home video game console developed by Sega"), QStringLiteral("Saturn")},
  };
  // Games type — and the inherited-blank type subcollections carry — both
  // land on the console, never the planet: exact-match tie, vocabulary
  // breaks it.
  QCOMPARE(WikidataLogoParser::pickEntityForCollection(hits, QStringLiteral("Games"),
                                                       QStringLiteral("Saturn")),
           QStringLiteral("Q200912"));
  QCOMPARE(WikidataLogoParser::pickEntityForCollection(hits, QString(), QStringLiteral("Saturn")),
           QStringLiteral("Q200912"));
  // Kartend-5b5r1 follow-up ("Sony" scraped as PS3): an EXACT alias match
  // must beat a prefix hit even when only the prefix hit speaks the games
  // vocabulary. PS3 listed first to prove ordering doesn't decide.
  const QList<WikidataLogoParser::SearchHit> sony = {
      {QStringLiteral("Q10683"), QStringLiteral("PlayStation 3"),
       QStringLiteral("home video game console developed by Sony"),
       QStringLiteral("Sony PlayStation 3")},
      {QStringLiteral("Q41187"), QStringLiteral("Sony Group Corporation"),
       QStringLiteral("Japanese multinational conglomerate"), QStringLiteral("Sony")},
  };
  QCOMPARE(WikidataLogoParser::pickEntityForCollection(sony, QStringLiteral("Games"),
                                                       QStringLiteral("Sony")),
           QStringLiteral("Q41187"));
  // Live field report round 2 ("Sony" scraped as MALE GIVEN NAME): the
  // real hit list has an exactly-matching junk entity and the company only
  // as a PREFIX match ("Sony Group"). Junk never wins; for a SHELL
  // collection the company vocabulary outranks the console words, so Sony
  // Group beats the PlayStation too.
  const QList<WikidataLogoParser::SearchHit> sonyLive = {
      {QStringLiteral("Q41187"), QStringLiteral("Sony Group"),
       QStringLiteral("Japanese multinational conglomerate corporation"),
       QStringLiteral("Sony Group")},
      {QStringLiteral("Q10683"), QStringLiteral("PlayStation 3"),
       QStringLiteral("video game console developed Sony Interactive Entertainment"),
       QStringLiteral("Sony PlayStation 3")},
      {QStringLiteral("Q65177437"), QStringLiteral("Sony"), QStringLiteral("male given name"),
       QStringLiteral("Sony")},
  };
  QCOMPARE(WikidataLogoParser::pickEntityForCollection(sonyLive, QStringLiteral("Games"),
                                                       QStringLiteral("Sony"),
                                                       /*preferCompany=*/true),
           QStringLiteral("Q41187"));
  // Nothing matching exactly OR by vocabulary → empty (caller escalates),
  // NOT the first hit.
  const QList<WikidataLogoParser::SearchHit> junk = {
      {QStringLiteral("Q1"), QStringLiteral("Saturnalia Festival"),
       QStringLiteral("ancient Roman festival"), QStringLiteral("Saturnalia Festival")}};
  QVERIFY(WikidataLogoParser::pickEntityForCollection(junk, QStringLiteral("Games"),
                                                      QStringLiteral("Saturn"))
              .isEmpty());
}

void TestWikidataLogoProvider::fetchEntity_ambiguousNameResolvesToConsoleNotPlanet() {
  // "Saturn" (games collection): the planet ranks first in the search, but
  // the picker must choose the console — the data hop's ids parameter is
  // the proof.
  ProviderBase::setFetchFunctionForTesting([](const QUrl &url,
                                              const Scraper::HttpClient::RawHeaders &,
                                              Scraper::HttpClient::ResponseCallback cb,
                                              const QStringList &) {
    const QString s = url.toString();
    if (s.contains(QStringLiteral("wbsearchentities"))) {
      cb(QByteArrayLiteral(
          R"({"search":[{"id":"Q193","label":"Saturn","description":"sixth planet from the Sun","match":{"text":"Saturn"}},)"
          R"({"id":"Q200912","label":"Sega Saturn","description":"home video game console developed by Sega","match":{"text":"Saturn"}}]})"));
    } else if (s.contains(QStringLiteral("ids=Q200912"))) {
      cb(QByteArrayLiteral(R"({"entities":{"Q200912":{
            "descriptions":{"en":{"value":"home video game console"}},
            "claims":{}}}})"));
    } else if (s.contains(QStringLiteral("ids=Q193"))) {
      QFAIL("resolved to the planet, not the console");
    } else {
      QFAIL(qPrintable(QStringLiteral("unexpected fetch: %1").arg(s)));
    }
  });

  CollectionConfig cfg;
  cfg.name = QStringLiteral("Saturn");
  cfg.type = QStringLiteral("Games");
  WikidataLogoProvider provider([&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("uuid-sat");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  QCOMPARE(result->value().description, QStringLiteral("home video game console"));
}

void TestWikidataLogoProvider::fetchEntity_compoundNameFallsBackToFragmentQuery() {
  // "Famicom - Nintendo Entertainment System" matches nothing as-is; the
  // ladder's "Famicom" fragment finds the NES entity.
  ProviderBase::setFetchFunctionForTesting([](const QUrl &url,
                                              const Scraper::HttpClient::RawHeaders &,
                                              Scraper::HttpClient::ResponseCallback cb,
                                              const QStringList &) {
    const QString s = url.toString();
    if (s.contains(QStringLiteral("wbsearchentities")) &&
        s.contains(QStringLiteral("Entertainment"))) {
      cb(QByteArrayLiteral(R"({"search":[]})")); // full compound name: no hits
    } else if (s.contains(QStringLiteral("wbsearchentities")) &&
               s.contains(QStringLiteral("Famicom"))) {
      cb(QByteArrayLiteral(
          R"({"search":[{"id":"Q172742","label":"Nintendo Entertainment System","description":"home video game console by Nintendo"}]})"));
    } else if (s.contains(QStringLiteral("ids=Q172742"))) {
      cb(QByteArrayLiteral(R"({"entities":{"Q172742":{
            "descriptions":{"en":{"value":"home video game console by Nintendo"}},
            "claims":{}}}})"));
    } else {
      QFAIL(qPrintable(QStringLiteral("unexpected fetch: %1").arg(s)));
    }
  });

  CollectionConfig cfg;
  cfg.name = QStringLiteral("Famicom - Nintendo Entertainment System");
  cfg.type = QStringLiteral("Games");
  WikidataLogoProvider provider([&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("uuid-nes");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  QCOMPARE(result->value().title, QStringLiteral("Famicom - Nintendo Entertainment System"));
  QCOMPARE(result->value().description, QStringLiteral("home video game console by Nintendo"));
}

void TestWikidataLogoProvider::fetchEntity_noEntityIsNotFound() {
  ProviderBase::setFetchFunctionForTesting(
      [](const QUrl &, const Scraper::HttpClient::RawHeaders &,
         Scraper::HttpClient::ResponseCallback cb,
         const QStringList &) { cb(QByteArrayLiteral(R"({"search":[]})")); });
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Utterly Unknown");
  WikidataLogoProvider provider([&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("u1");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY(result->isError());
  // Not-found shaped so the coordinator books it (and never re-falls-back).
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::RemoteResourceNotFound);
}

void TestWikidataLogoProvider::fetchEntity_wrongTargetTypeIsInvalidArgument() {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Maker");
  WikidataLogoProvider provider([&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Platform;

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY(result->isError());
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

QTEST_MAIN(TestWikidataLogoProvider)
#include "test_wikidatalogoprovider.moc"
