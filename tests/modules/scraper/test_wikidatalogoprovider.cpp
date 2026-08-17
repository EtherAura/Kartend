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

  void fetchEntity_happyPathBuildsSvgLogoAsset();
  void fetchEntity_noEntityIsNotFound();
  void fetchEntity_wrongTargetTypeIsInvalidArgument();
};

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
  ProviderBase::setFetchFunctionForTesting(
      [](const QUrl &url, const Scraper::HttpClient::RawHeaders &,
         Scraper::HttpClient::ResponseCallback cb, const QStringList &) {
        const QString s = url.toString();
        if (s.contains(QStringLiteral("wbsearchentities"))) {
          cb(QByteArrayLiteral(R"({"search":[{"id":"Q42"}]})"));
        } else if (s.contains(QStringLiteral("wbgetclaims"))) {
          cb(QByteArrayLiteral(
              R"({"claims":{"P154":[{"mainsnak":{"datavalue":{"value":"Maker logo.svg"}}}]}})"));
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

void TestWikidataLogoProvider::fetchEntity_noEntityIsNotFound() {
  ProviderBase::setFetchFunctionForTesting(
      [](const QUrl &, const Scraper::HttpClient::RawHeaders &,
         Scraper::HttpClient::ResponseCallback cb, const QStringList &) {
        cb(QByteArrayLiteral(R"({"search":[]})"));
      });
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
