// FlathubParser: pure QByteArray -> Result transforms over inline JSON
// fixtures (appstream shape, verified against the live v2 API), no network —
// plus FlathubProvider's identity resolution, which is synchronous and
// network-free by construction (an exact app id needs no search request, and
// a free-form name has no key-less GET endpoint to hit).
#include <QDir>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "flathubparser.h"
#include "flathubprovider.h"
#include "kartlink.h"

namespace {

// The live API serves releases[].timestamp as a STRING ("1771200000"); the
// fixture pins that quirk so a strict toDouble()/toInt() regression fails
// here rather than silently dropping every release date.
const QByteArray kAppstreamBody = R"({
  "name": "0 A.D.",
  "summary": "Real-time strategy game",
  "description": "<p>0 A.D. is a real-time strategy (RTS) game of ancient warfare.</p><ul><li>Twelve civilizations</li></ul>",
  "developer_name": "Wildfire Games",
  "project_license": "GPL-2.0+ and CC-BY-SA",
  "categories": ["Game", "StrategyGame"],
  "releases": [
    {"type": "stable", "version": "0.28.0", "timestamp": "1771200000"},
    {"type": "stable", "version": "0.27.1", "timestamp": "1752364800"}
  ]
})";

const QByteArray kMinimalBody = R"({
  "name": "Tiny App",
  "summary": "Just a summary",
  "categories": ["Game"]
})";

} // namespace

class TestFlathubParser : public QObject {
  Q_OBJECT

private slots:
  void appstreamMapsAllFields();
  void descriptionFallsBackToSummaryAndGenreDropsBareGame();
  void emptyObjectIsNotFound();
  void invalidJsonIsError();

  // FlathubProvider identity resolution (synchronous, no network)
  void providerLookupResolvesAppIdShapedQueryOnly();
  void providerLookupReadsAppIdFromKartlinkStub();
  void providerSearchUrlPercentEncodes();
};

void TestFlathubParser::appstreamMapsAllFields() {
  const auto result =
      FlathubParser::parseAppstream(kAppstreamBody, QStringLiteral("com.play0ad.zeroad"));
  QVERIFY(result.isOk());
  const Scraper::ScrapedItem item = result.value();
  QCOMPARE(item.sourceProviderId, QStringLiteral("flathub"));
  QCOMPARE(item.title, QStringLiteral("0 A.D."));
  // HTML flattened, not echoed.
  QVERIFY(item.description.startsWith(
      QStringLiteral("0 A.D. is a real-time strategy (RTS) game of ancient warfare.")));
  QVERIFY(!item.description.contains(QLatin1Char('<')));
  QCOMPARE(item.developer, QStringLiteral("Wildfire Games"));
  // The umbrella "Game" entry is noise in a games collection; only the
  // informative category survives.
  QCOMPARE(item.genre, QStringLiteral("StrategyGame"));
  // releases[0] (newest) as an ISO date, string timestamp and all.
  QCOMPARE(item.releaseDate, QStringLiteral("2026-02-16"));
  QCOMPARE(item.customFields.value(QStringLiteral("License")),
           QStringLiteral("GPL-2.0+ and CC-BY-SA"));
}

void TestFlathubParser::descriptionFallsBackToSummaryAndGenreDropsBareGame() {
  const auto result = FlathubParser::parseAppstream(kMinimalBody, QStringLiteral("io.tiny.App"));
  QVERIFY(result.isOk());
  const Scraper::ScrapedItem item = result.value();
  QCOMPARE(item.description, QStringLiteral("Just a summary"));
  // Only "Game" configured -> no genre at all rather than a redundant one.
  QCOMPARE(item.genre, QString());
  QCOMPARE(item.releaseDate, QString());
  QVERIFY(!item.customFields.contains(QStringLiteral("License")));
}

void TestFlathubParser::emptyObjectIsNotFound() {
  // Unknown refs normally 404 before the parser runs; a 200 whose body is
  // an empty object is the defensive in-band equivalent — a routine
  // not-found the batch runner buckets, not an error.
  const auto result =
      FlathubParser::parseAppstream(QByteArrayLiteral("{}"), QStringLiteral("io.gone.App"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::RemoteResourceNotFound);
}

void TestFlathubParser::invalidJsonIsError() {
  const auto result =
      FlathubParser::parseAppstream(QByteArrayLiteral("<html>oops"), QStringLiteral("io.x.Y"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::NetworkRequestFailed);
}

void TestFlathubParser::providerLookupResolvesAppIdShapedQueryOnly() {
  FlathubProvider provider;

  QList<Scraper::ScrapeCandidate> got;
  bool called = false;
  provider.lookup(QStringLiteral("com.play0ad.zeroad"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isOk());
                    got = result.value();
                  });
  QVERIFY(called);
  QCOMPARE(got.size(), 1);
  QCOMPARE(got.first().providerSpecificId, QStringLiteral("com.play0ad.zeroad"));
  QCOMPARE(got.first().matchScore, 100);

  // A display name is not an id: no candidates, and — critically — no
  // network request to a search endpoint that does not exist.
  called = false;
  provider.lookup(QStringLiteral("0 A.D."),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isOk());
                    QVERIFY(result.value().isEmpty());
                  });
  QVERIFY(called);
}

void TestFlathubParser::providerLookupReadsAppIdFromKartlinkStub() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString stubPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("0 A.D..kartlink"));
  KartLink::LinkData link;
  link.source = QStringLiteral("flatpak");
  link.target = QStringLiteral("com.play0ad.zeroad");
  link.title = QStringLiteral("0 A.D.");
  QVERIFY(KartLink::write(stubPath, link));

  FlathubProvider provider;
  MetadataLookupProvider::LookupContext ctx;
  ctx.query = QStringLiteral("0 A.D.");
  ctx.filePath = stubPath;

  bool called = false;
  provider.lookup(ctx, [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
    called = true;
    QVERIFY(result.isOk());
    QCOMPARE(result.value().size(), 1);
    QCOMPARE(result.value().first().providerSpecificId, QStringLiteral("com.play0ad.zeroad"));
    QCOMPARE(result.value().first().displayName, QStringLiteral("0 A.D."));
  });
  QVERIFY(called);
}

void TestFlathubParser::providerSearchUrlPercentEncodes() {
  // toEncoded, not toString: QUrl::toString() re-decodes %20 for display.
  const FlathubProvider provider;
  QCOMPARE(provider.searchUrl(QStringLiteral("0 A.D.")).toEncoded(),
           QByteArrayLiteral("https://flathub.org/apps/search?q=0%20A.D."));
  QVERIFY(provider.searchUrl(QStringLiteral("   ")).isEmpty());
}

QTEST_MAIN(TestFlathubParser)
#include "test_flathubparser.moc"
