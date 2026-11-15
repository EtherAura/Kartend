// Kartend-5b5r1: a platform entity whose only usable Wikidata claims are
// spec-sheet fields the parser already extracts (P2664 units sold, P880 CPU)
// must be a metadata success carrying those values — not booked as
// not-found with the spec sheet thrown away. The real WikidataLogoProvider
// answers offline through ProviderBase's fetch seam; any request the fixture
// does not know fails the test. Provider boundary only: no database, no
// persistence claim. Fixture names and entity ids are generic placeholders.
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

using EntityResult = ErrorUtils::Result<Scraper::ScrapedItem>;

/// Answers the search hop with Q900002 and the entity-data hop with
/// @p entityBody. @p labelsBody (when non-empty) answers the batched labels
/// hop; @p summaryBody (when non-empty) the Wikipedia summary hop. Every
/// other request — including a labels or summary hop the fixture did not
/// expect — fails the test.
void installFetch(const QByteArray &entityBody, const QByteArray &labelsBody = {},
                  const QByteArray &summaryBody = {}) {
  ProviderBase::setFetchFunctionForTesting(
      [entityBody, labelsBody,
       summaryBody](const QUrl &url, const Scraper::HttpClient::RawHeaders &,
                    Scraper::HttpClient::ResponseCallback cb, const QStringList &) {
        const QString s = url.toString();
        if (s.contains(QStringLiteral("wbsearchentities"))) {
          cb(QByteArrayLiteral(R"({"search":[{"id":"Q900002","label":"Example Console",)"
                               R"("description":"home video game console",)"
                               R"("match":{"text":"Example Console"}}]})"));
        } else if (s.contains(QStringLiteral("ids=Q900002"))) {
          cb(entityBody);
        } else if (!labelsBody.isEmpty() && s.contains(QStringLiteral("props=labels"))) {
          cb(labelsBody);
        } else if (!summaryBody.isEmpty() && s.contains(QStringLiteral("en.wikipedia.org"))) {
          cb(summaryBody);
        } else {
          QFAIL(qPrintable(QStringLiteral("unexpected fetch: %1").arg(s)));
        }
      });
}

std::optional<EntityResult> runFetchEntity() {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Example Console");
  cfg.type = QStringLiteral("Games");
  WikidataLogoProvider provider([&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("uuid-console");
  std::optional<EntityResult> result;
  provider.fetchEntity(target, [&result](const EntityResult &r) { result = r; });
  return result;
}

} // namespace

class TestWikidataPlatformSpecs : public QObject {
  Q_OBJECT
private slots:
  void cleanup();

  void unitsSoldOnlyEntityIsMetadataSuccess();
  void cpuOnlyEntityIsMetadataSuccess();
  void emptyEntityStaysNotFound();
  void artBearingEntityKeepsSpecsAndArt();
};

void TestWikidataPlatformSpecs::cleanup() {
  ProviderBase::setFetchFunctionForTesting({});
}

void TestWikidataPlatformSpecs::unitsSoldOnlyEntityIsMetadataSuccess() {
  // No logo, no P18 photo, no description, no manufacturer, no sitelink —
  // only P2664 units sold, a plain quantity that needs no labels hop.
  installFetch(QByteArrayLiteral(R"({"entities":{"Q900002":{"claims":{
      "P2664":[{"mainsnak":{"datavalue":{"value":{"amount":"+49100000"}}}}]}}}})"));

  const auto result = runFetchEntity();
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.sourceProviderId, QStringLiteral("wikidata"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldUnitsSold)),
           QStringLiteral("49100000"));
  QVERIFY(item.description.isEmpty());
  QVERIFY(item.media.isEmpty());
}

void TestWikidataPlatformSpecs::cpuOnlyEntityIsMetadataSuccess() {
  // Only P880 CPU — an entity reference resolved by the batched labels hop.
  installFetch(QByteArrayLiteral(R"({"entities":{"Q900002":{"claims":{
      "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q900010"}}}}]}}}})"),
               QByteArrayLiteral(R"({"entities":{
      "Q900010":{"labels":{"en":{"value":"Example CPU"}}}}})"));

  const auto result = runFetchEntity();
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldCpu)),
           QStringLiteral("Example CPU"));
  QVERIFY(item.media.isEmpty());
}

void TestWikidataPlatformSpecs::emptyEntityStaysNotFound() {
  // The entity resolves but carries no usable claim at all — including a
  // malformed (negative) units-sold quantity the parser rejects.
  installFetch(QByteArrayLiteral(R"({"entities":{"Q900002":{"claims":{
      "P2664":[{"mainsnak":{"datavalue":{"value":{"amount":"-5"}}}}]}}}})"));

  const auto result = runFetchEntity();
  QVERIFY(result.has_value());
  QVERIFY(result->isError());
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::RemoteResourceNotFound);
}

void TestWikidataPlatformSpecs::artBearingEntityKeepsSpecsAndArt() {
  // An ordinary platform: logo, description, sitelink, manufacturer and
  // specs. The result is unchanged by the spec-only acceptance.
  installFetch(QByteArrayLiteral(R"({"entities":{"Q900002":{
      "descriptions":{"en":{"value":"home video game console"}},
      "sitelinks":{"enwiki":{"title":"Example Console"}},
      "claims":{
        "P154":[{"mainsnak":{"datavalue":{"value":"Example console logo.svg"}}}],
        "P176":[{"mainsnak":{"datavalue":{"value":{"id":"Q900011"}}}}],
        "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q900010"}}}}],
        "P2664":[{"mainsnak":{"datavalue":{"value":{"amount":"+49100000"}}}}]}}}})"),
               QByteArrayLiteral(R"({"entities":{
      "Q900010":{"labels":{"en":{"value":"Example CPU"}}},
      "Q900011":{"labels":{"en":{"value":"Example Maker"}}}}})"),
               QByteArrayLiteral(R"({"type":"standard","extract":"An example console."})"));

  const auto result = runFetchEntity();
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.description, QStringLiteral("An example console."));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldManufacturer)),
           QStringLiteral("Example Maker"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldCpu)),
           QStringLiteral("Example CPU"));
  QCOMPARE(item.customFields.value(QLatin1String(EntityMetadataStore::kFieldUnitsSold)),
           QStringLiteral("49100000"));
  QCOMPARE(item.media.size(), 1);
  QCOMPARE(item.media.first().type, QStringLiteral("logo-svg"));
  QCOMPARE(item.media.first().entityRole, Scraper::EntityArtRole::Logo);
  QCOMPARE(item.media.first().scopeKey, QStringLiteral("uuid-console"));
}

QTEST_MAIN(TestWikidataPlatformSpecs)
#include "test_wikidataplatformspecs.moc"
