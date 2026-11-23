// Kartend-5b5r1 (units-sold slice): a platform entity whose only usable data
// is the P2664 units-sold quantity the parser already extracts must come back
// from the real WikidataLogoProvider as a success carrying that value, not as
// not-found. A resolved entity with nothing usable — including one whose only
// units-sold claim the parser rejects — stays not-found, and an art- and
// metadata-bearing platform keeps its logo, its metadata and its units sold.
// CPU-only slice: a platform whose only usable data is the P880 CPU reference
// succeeds with the CPU label the batched labels hop resolves; with no usable
// resolved label it stays not-found, and GPU alone does not admit an entity.
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
const QString kCpuKey = QLatin1String(EntityMetadataStore::kFieldCpu);

} // namespace

class TestWikidataPlatformSpecs : public QObject {
  Q_OBJECT
private slots:
  void cleanup();

  void unitsSoldOnlyEntityIsRetained();
  void emptyEntityIsNotFound_data();
  void emptyEntityIsNotFound();
  void artAndMetadataEntityKeepsUnitsSold();
  void cpuOnlyEntityIsRetained();
  void cpuOnlyEntityWithoutUsableLabelIsNotFound_data();
  void cpuOnlyEntityWithoutUsableLabelIsNotFound();
  void gpuOnlyEntityStaysNotFound();
  void cpuWithPhotoKeepsPhoto_data();
  void cpuWithPhotoKeepsPhoto();
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

// CPU-only slice: a platform whose only usable data is the P880 CPU reference
// must survive the provider's usable-data gate, resolve the CPU's label
// through the batched labels hop, and come back as a metadata-only success.
void TestWikidataPlatformSpecs::cpuOnlyEntityIsRetained() {
  installFetch(QByteArrayLiteral(R"("claims":{
      "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q900100"}}}}]})"),
               QByteArrayLiteral(R"({"entities":{
        "Q900100":{"labels":{"en":{"value":"Example CPU 32-bit"}}}}})"));

  const auto result = runFetch();
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.sourceProviderId, QStringLiteral("wikidata"));
  QCOMPARE(item.customFields.value(kCpuKey), QStringLiteral("Example CPU 32-bit"));
  QCOMPARE(item.customFields.size(), 1);
  QVERIFY(item.description.isEmpty());
  QVERIFY(item.media.isEmpty());
  // CPU-only: the saved row's title, description, art and other specs must
  // survive the save.
  QVERIFY(item.cpuOnlyEntityMetadata);
}

void TestWikidataPlatformSpecs::cpuOnlyEntityWithoutUsableLabelIsNotFound_data() {
  QTest::addColumn<QByteArray>("labels");
  // The labels hop succeeds but supplies no usable label for the CPU.
  QTest::newRow("entity without labels") << QByteArrayLiteral(R"({"entities":{"Q900100":{}}})");
  QTest::newRow("no English label") << QByteArrayLiteral(R"({"entities":{
        "Q900100":{"labels":{"de":{"value":"Beispiel-CPU"}}}}})");
  QTest::newRow("empty English label") << QByteArrayLiteral(R"({"entities":{
        "Q900100":{"labels":{"en":{"value":""}}}}})");
  QTest::newRow("label for another entity only") << QByteArrayLiteral(R"({"entities":{
        "Q900101":{"labels":{"en":{"value":"Unrelated"}}}}})");
  QTest::newRow("no entities") << QByteArrayLiteral(R"({"entities":{}})");
  // A body the labels parser cannot read degrades to no labels, exactly as
  // it does for every other entity; with nothing else usable that is still
  // not a success.
  QTest::newRow("unparseable labels body") << QByteArrayLiteral("not json");
}

void TestWikidataPlatformSpecs::cpuOnlyEntityWithoutUsableLabelIsNotFound() {
  QFETCH(QByteArray, labels);
  installFetch(QByteArrayLiteral(R"("claims":{
      "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q900100"}}}}]})"),
               labels);

  const auto result = runFetch();
  QVERIFY(result.has_value());
  QVERIFY2(result->isError(), "a CPU-only entity with no usable CPU label must not succeed");
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::RemoteResourceNotFound);
}

// GPU is not made eligible by the CPU slice: a GPU-only entity stays
// not-found without even asking for labels (the fixture fails any request it
// was not given a labels body for).
void TestWikidataPlatformSpecs::gpuOnlyEntityStaysNotFound() {
  installFetch(QByteArrayLiteral(R"("claims":{
      "P2560":[{"mainsnak":{"datavalue":{"value":{"id":"Q900100"}}}}]})"));

  const auto result = runFetch();
  QVERIFY(result.has_value());
  QVERIFY(result->isError());
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::RemoteResourceNotFound);
}

// The CPU-only gate must not swallow a CPU that rides along with ordinary
// data: a photo-bearing entity whose CPU label is missing still succeeds with
// its photo, and one whose CPU label resolves carries both.
void TestWikidataPlatformSpecs::cpuWithPhotoKeepsPhoto_data() {
  QTest::addColumn<QByteArray>("labels");
  QTest::addColumn<QString>("expectedCpu");
  QTest::newRow("CPU label resolved") << QByteArrayLiteral(R"({"entities":{
        "Q900100":{"labels":{"en":{"value":"Example CPU"}}}}})")
                                      << QStringLiteral("Example CPU");
  QTest::newRow("CPU label missing")
      << QByteArrayLiteral(R"({"entities":{"Q900100":{}}})") << QString();
}

void TestWikidataPlatformSpecs::cpuWithPhotoKeepsPhoto() {
  QFETCH(QByteArray, labels);
  QFETCH(QString, expectedCpu);
  installFetch(QByteArrayLiteral(R"("claims":{
      "P18":[{"mainsnak":{"datavalue":{"value":"Example console.jpg"}}}],
      "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q900100"}}}}]})"),
               labels);

  const auto result = runFetch();
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.customFields.value(kCpuKey), expectedCpu);
  QCOMPARE(item.customFields.contains(kCpuKey), !expectedCpu.isEmpty());
  QCOMPARE(item.media.size(), 1);
  QCOMPARE(item.media.first().type, QStringLiteral("photo"));
  // Ordinary (non-CPU-only) results keep the full upsert.
  QVERIFY(!item.cpuOnlyEntityMetadata);
}

QTEST_MAIN(TestWikidataPlatformSpecs)
#include "test_wikidataplatformspecs.moc"
