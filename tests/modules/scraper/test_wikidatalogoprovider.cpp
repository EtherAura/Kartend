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
              "P176":[{"mainsnak":{"datavalue":{"value":{"id":"Q122741"}}}}],
              "P571":[{"mainsnak":{"datavalue":{"value":{"time":"+1988-10-29T00:00:00Z"}}}}]
            }}}})"));
        } else if (s.contains(QStringLiteral("ids=Q122741"))) {
          cb(QByteArrayLiteral(R"({"entities":{"Q122741":{"labels":{"en":{"value":"Sega"}}}}})"));
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
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldReleaseDate)),
           QStringLiteral("1988"));
  QCOMPARE(item.media.size(), 1);
  QCOMPARE(item.media.first().type, QStringLiteral("logo-svg"));
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
