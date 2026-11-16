// Kartend-5b5r1 (units-sold slice): a platform entity whose only usable data
// is the P2664 units-sold quantity the parser already extracts must come back
// from the real WikidataLogoProvider as a success carrying that value, not as
// not-found. A resolved entity with nothing usable — including one whose only
// units-sold claim the parser rejects — stays not-found, and an art- and
// metadata-bearing platform keeps its logo, its metadata and its units sold.
// Driven offline through ProviderBase's fetch seam; any request the fixture
// does not know fails the test. Fixture names and entity ids are generic
// placeholders.
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTest>
#include <QUrl>

#include <optional>

#include "collection/collectionconfig.h"
#include "entitymetadata.h"
#include "wikidatalogoprovider.h"

namespace {

/// Answers the search hop with Q900002 and the data hop with an entity made
/// of @p entityBody (the JSON members inside the entity object). The labels
/// hop answers only when @p labels is non-empty.
void installFetch(const QByteArray &entityBody, const QByteArray &labels = QByteArray()) {
  ProviderBase::setFetchFunctionForTesting(
      [entityBody, labels](const QUrl &url, const Scraper::HttpClient::RawHeaders &,
                           Scraper::HttpClient::ResponseCallback cb, const QStringList &) {
        const QString s = url.toString();
        if (s.contains(QStringLiteral("wbsearchentities"))) {
          cb(QByteArrayLiteral(R"({"search":[{"id":"Q900002","label":"Example Console",)"
                               R"("description":"home video game console",)"
                               R"("match":{"text":"Example Console"}}]})"));
        } else if (s.contains(QStringLiteral("ids=Q900002"))) {
          cb(R"({"entities":{"Q900002":{)" + entityBody + "}}}");
        } else if (!labels.isEmpty() && s.contains(QStringLiteral("ids=Q900100"))) {
          cb(labels);
        } else {
          QFAIL(qPrintable(QStringLiteral("unexpected fetch: %1").arg(s)));
        }
      });
}

std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> runFetch() {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Example Console");
  cfg.type = QStringLiteral("Games");
  WikidataLogoProvider provider([&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("uuid-console");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  return result;
}

const QString kUnitsSoldKey = QLatin1String(EntityMetadataStore::kFieldUnitsSold);

} // namespace

class TestWikidataPlatformSpecs : public QObject {
  Q_OBJECT
private slots:
  void cleanup();

  void unitsSoldOnlyEntityIsRetained();
  void emptyEntityIsNotFound_data();
  void emptyEntityIsNotFound();
  void artAndMetadataEntityKeepsUnitsSold();
};

void TestWikidataPlatformSpecs::cleanup() {
  ProviderBase::setFetchFunctionForTesting({});
}

void TestWikidataPlatformSpecs::unitsSoldOnlyEntityIsRetained() {
  // No logo, no P18 photo, no description, no manufacturer, no sitelink: the
  // units-sold quantity is the entity's only usable data.
  installFetch(QByteArrayLiteral(R"("claims":{
      "P2664":[{"mainsnak":{"datavalue":{"value":{"amount":"+1000000"}}}}]})"));

  const auto result = runFetch();
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.sourceProviderId, QStringLiteral("wikidata"));
  QCOMPARE(item.customFields.value(kUnitsSoldKey), QStringLiteral("1000000"));
  QCOMPARE(item.customFields.size(), 1);
  QVERIFY(item.description.isEmpty());
  QVERIFY(item.media.isEmpty());
}

void TestWikidataPlatformSpecs::emptyEntityIsNotFound_data() {
  QTest::addColumn<QByteArray>("entityBody");
  QTest::newRow("no claims") << QByteArrayLiteral(R"("claims":{})");
  // The parser rejects a negative quantity and a non-numeric one; with
  // nothing else usable the entity must stay not-found.
  QTest::newRow("negative units sold") << QByteArrayLiteral(R"("claims":{
      "P2664":[{"mainsnak":{"datavalue":{"value":{"amount":"-1000000"}}}}]})");
  QTest::newRow("non-numeric units sold") << QByteArrayLiteral(R"("claims":{
      "P2664":[{"mainsnak":{"datavalue":{"value":{"amount":"+1.5e6"}}}}]})");
}

void TestWikidataPlatformSpecs::emptyEntityIsNotFound() {
  QFETCH(QByteArray, entityBody);
  installFetch(entityBody);

  const auto result = runFetch();
  QVERIFY(result.has_value());
  QVERIFY(result->isError());
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::RemoteResourceNotFound);
}

void TestWikidataPlatformSpecs::artAndMetadataEntityKeepsUnitsSold() {
  installFetch(QByteArrayLiteral(R"(
      "descriptions":{"en":{"value":"home video game console"}},
      "claims":{
        "P154":[{"mainsnak":{"datavalue":{"value":"Example console logo.svg"}}}],
        "P176":[{"mainsnak":{"datavalue":{"value":{"id":"Q900100"}}}}],
        "P2664":[{"mainsnak":{"datavalue":{"value":{"amount":"+2000000"}}}}]})"),
               QByteArrayLiteral(R"({"entities":{
        "Q900100":{"labels":{"en":{"value":"Example Maker"}}}}})"));

  const auto result = runFetch();
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.description, QStringLiteral("home video game console"));
  QCOMPARE(item.customFields.value(kUnitsSoldKey), QStringLiteral("2000000"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldManufacturer)),
           QStringLiteral("Example Maker"));
  QCOMPARE(item.media.size(), 1);
  const Scraper::MediaAsset &logo = item.media.first();
  QCOMPARE(logo.type, QStringLiteral("logo-svg"));
  QCOMPARE(logo.scope, Scraper::MediaScope::Collection);
  QCOMPARE(logo.scopeKey, QStringLiteral("uuid-console"));
  QCOMPARE(logo.entityRole, Scraper::EntityArtRole::Logo);
}

QTEST_MAIN(TestWikidataPlatformSpecs)
#include "test_wikidataplatformspecs.moc"
