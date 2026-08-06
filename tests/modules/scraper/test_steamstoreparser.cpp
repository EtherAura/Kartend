// SteamStoreParser: pure QByteArray -> Result transforms over inline JSON
// fixtures (storesearch + appdetails shapes), no network.
//
// Fixture URLs are deliberately scheme-less (no "//"): moc comment-lexes
// "//" even inside raw string literals, truncating every line after it —
// which corrupts its token stream until the Q_OBJECT class below becomes
// invisible ("No relevant classes found") and the test fails to link.
#include <QObject>
#include <QTest>

#include "steamstoreparser.h"

namespace {

const QByteArray kSearchBody = R"({
  "total": 2,
  "items": [
    {"id": 620, "name": "Portal 2", "tiny_image": "cdn.example/steam/apps/620/capsule_231x87.jpg"},
    {"id": 0, "name": "Bogus entry without id"},
    {"id": 400, "name": "Portal"}
  ]
})";

const QByteArray kDetailsBody = R"({
  "620": {
    "success": true,
    "data": {
      "name": "Portal 2",
      "short_description": "The &quot;perpetual testing initiative&quot; has been expanded.<br>Sequel.",
      "required_age": "13",
      "developers": ["Valve"],
      "publishers": ["Valve", ""],
      "genres": [{"id": "1", "description": "Action"}, {"id": "25", "description": "Adventure"}],
      "categories": [{"id": 2, "description": "Single-player"}, {"id": 38, "description": "Online Co-op"}, {"id": 22, "description": "Steam Achievements"}],
      "release_date": {"coming_soon": false, "date": "18 Apr, 2011"},
      "metacritic": {"score": 95, "url": "metacritic.example/game/pc/portal-2"},
      "header_image": "cdn.example/steam/apps/620/header.jpg",
      "background_raw": "cdn.example/steam/apps/620/page_bg_raw.jpg",
      "screenshots": [
        {"id": 0, "path_thumbnail": "cdn.example/steam/apps/620/ss1_thumb.jpg", "path_full": "cdn.example/steam/apps/620/ss1.jpg"},
        {"id": 1, "path_full": "cdn.example/steam/apps/620/ss2.jpg"}
      ],
      "movies": [
        {"id": 5, "name": "Portal 2 Trailer", "mp4": {"480": "video.example/store_trailers/5/movie480.mp4", "max": "video.example/store_trailers/5/movie_max.mp4"}}
      ]
    }
  }
})";

const QByteArray kNotFoundBody = R"({"999": {"success": false}})";

} // namespace

class TestSteamStoreParser : public QObject {
  Q_OBJECT

private slots:
  void searchParsesCandidatesAndSkipsInvalid();
  void detailMapsAllFields();
  void detailSuccessFalseIsNotFound();
  void malformedJsonIsAnError();
};

void TestSteamStoreParser::searchParsesCandidatesAndSkipsInvalid() {
  const auto result = SteamStoreParser::parseStoreSearch(kSearchBody);
  QVERIFY(!result.isError());
  QCOMPARE(result.value().size(), 2); // the id-less row is dropped
  const Scraper::ScrapeCandidate first = result.value().first();
  QCOMPARE(first.displayName, QStringLiteral("Portal 2"));
  QCOMPARE(first.providerSpecificId, QStringLiteral("620"));
  QVERIFY(first.thumbnailUrl.isValid());
}

void TestSteamStoreParser::detailMapsAllFields() {
  const auto result = SteamStoreParser::parseAppDetails(kDetailsBody, QStringLiteral("620"));
  QVERIFY(!result.isError());
  const Scraper::ScrapedItem item = result.value();
  QCOMPARE(item.sourceProviderId, QStringLiteral("steam"));
  QCOMPARE(item.title, QStringLiteral("Portal 2"));
  // HTML entities decoded, tags flattened (exact <br> line-break mapping is
  // Qt's business — assert the content, not the whitespace).
  QVERIFY(item.description.startsWith(
      QStringLiteral("The \"perpetual testing initiative\" has been expanded.")));
  QVERIFY(item.description.contains(QStringLiteral("Sequel.")));
  QVERIFY(!item.description.contains(QLatin1Char('<')));
  QCOMPARE(item.genre, QStringLiteral("Action, Adventure"));
  QCOMPARE(item.developer, QStringLiteral("Valve"));
  QCOMPARE(item.publisher, QStringLiteral("Valve")); // empty entry dropped
  QCOMPARE(item.releaseDate, QStringLiteral("18 Apr, 2011"));
  QCOMPARE(item.contentRating, QStringLiteral("13+"));
  // Store category ids map through the same taxonomy as appinfo; the
  // non-player flag (22) is ignored.
  QCOMPARE(item.players, QStringLiteral("Single-player, Online Co-op"));
  QCOMPARE(item.customFields.value(QStringLiteral("Metacritic")), QStringLiteral("95"));

  QCOMPARE(item.media.size(), 4);
  QCOMPARE(item.media.at(0).type, QStringLiteral("front"));
  QCOMPARE(item.media.at(1).type, QStringLiteral("screenshot"));
  QVERIFY(item.media.at(1).url.toString().endsWith(QStringLiteral("ss1.jpg")));
  QCOMPARE(item.media.at(2).type, QStringLiteral("background"));
  QVERIFY(item.media.at(2).url.toString().endsWith(QStringLiteral("page_bg_raw.jpg")));
  QCOMPARE(item.media.at(3).type, QStringLiteral("video"));
  QCOMPARE(item.media.at(3).label, QStringLiteral("Portal 2 Trailer"));
  QVERIFY(item.media.at(3).url.toString().endsWith(QStringLiteral("movie_max.mp4")));
}

void TestSteamStoreParser::detailSuccessFalseIsNotFound() {
  const auto result = SteamStoreParser::parseAppDetails(kNotFoundBody, QStringLiteral("999"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::RemoteResourceNotFound);
}

void TestSteamStoreParser::malformedJsonIsAnError() {
  const auto search = SteamStoreParser::parseStoreSearch(QByteArrayLiteral("<html>nope"));
  QVERIFY(search.isError());
  const auto detail =
      SteamStoreParser::parseAppDetails(QByteArrayLiteral("{broken"), QStringLiteral("620"));
  QVERIFY(detail.isError());
  QCOMPARE(detail.error().code, ErrorUtils::ErrorCode::NetworkRequestFailed);
}

QTEST_MAIN(TestSteamStoreParser)
#include "test_steamstoreparser.moc"
