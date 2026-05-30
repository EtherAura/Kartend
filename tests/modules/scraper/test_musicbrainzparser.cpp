// Tests for MusicBrainzParser. Pure JSON → typed parsing — no network
// access. Fixtures are inline `R"({...})"` JSON literals shaped to
// match the real MusicBrainz API response payloads (sample queries
// against musicbrainz.org/ws/2/release/?fmt=json against well-known
// releases produce structures matching the ones below).
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "musicbrainzparser.h"
#include "scrapertypes.h"

class TestMusicBrainzParser : public QObject {
  Q_OBJECT
private slots:
  void parseSearchResponse_returnsCandidatesSortedByScoreDesc();
  void parseSearchResponse_buildsArtistDashTitleDisplayName();
  void parseSearchResponse_subtitleConcatenatesDateCountryFormat();
  void parseSearchResponse_skipsEntriesWithoutMbid();
  void parseSearchResponse_skipsNonUuidMbid();
  void parseSearchResponse_thumbnailUsesCoverArtArchiveUrl();
  void parseSearchResponse_emptyReleasesArrayIsSuccessNotError();
  void parseSearchResponse_malformedJsonReturnsError();
  void parseDetailResponse_extractsTitleArtistLabelDateGenre();
  void parseDetailResponse_storesMbidAndTrackCountInCustomFields();
  void parseDetailResponse_includesFrontAndBackCoverMedia();
  void parseDetailResponse_genresPreferredOverTags();
  void appendCoverArtUrls_skipsEmptyMbid();
  void appendCoverArtUrls_skipsNonUuidMbid();
};

namespace {

const QByteArray SEARCH_FIXTURE = R"({
  "created": "2026-01-01T00:00:00.000Z",
  "count": 2,
  "offset": 0,
  "releases": [
    {
      "id": "11111111-1111-4111-8111-111111111111",
      "score": 95,
      "title": "Album A",
      "status": "Official",
      "artist-credit": [{"name": "Artist X", "joinphrase": ""}],
      "date": "2020-05-01",
      "country": "US",
      "media": [{"format": "CD", "track-count": 10}]
    },
    {
      "id": "22222222-2222-4222-8222-222222222222",
      "score": 80,
      "title": "Album B",
      "artist-credit": [{"name": "Artist Y", "joinphrase": " feat. "},
                        {"name": "Guest"}],
      "date": "2021-03-10",
      "country": "GB",
      "media": [{"format": "Digital Media", "track-count": 8}]
    },
    {
      "id": "",
      "score": 60,
      "title": "No MBID — should be skipped"
    }
  ]
})";

const QByteArray DETAIL_FIXTURE = R"({
  "id": "11111111-1111-4111-8111-111111111111",
  "title": "Album A",
  "artist-credit": [{"name": "Artist X"}],
  "date": "2020-05-01",
  "country": "US",
  "barcode": "0123456789",
  "annotation": "Studio album from 2020.",
  "label-info": [
    {"label": {"name": "Label One"}},
    {"label": {"name": "Label Two"}}
  ],
  "media": [{"format": "CD", "track-count": 12}],
  "genres": [
    {"name": "rock"},
    {"name": "indie"}
  ],
  "tags": [
    {"name": "noisy", "count": 1}
  ]
})";

const QByteArray DETAIL_FIXTURE_TAGS_ONLY = R"({
  "id": "33333333-3333-4333-8333-333333333333",
  "title": "Tagged Album",
  "artist-credit": [{"name": "Tagged Artist"}],
  "media": [{"format": "Digital", "track-count": 1}],
  "tags": [
    {"name": "alpha", "count": 1},
    {"name": "beta",  "count": 8},
    {"name": "gamma", "count": 5},
    {"name": "delta", "count": 12},
    {"name": "epsilon", "count": 2}
  ]
})";

} // namespace

void TestMusicBrainzParser::parseSearchResponse_returnsCandidatesSortedByScoreDesc() {
  auto result = MusicBrainzParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  const auto candidates = result.value();
  QCOMPARE(candidates.size(), 2);
  QCOMPARE(candidates[0].providerSpecificId,
           QStringLiteral("11111111-1111-4111-8111-111111111111"));
  QCOMPARE(candidates[0].matchScore, 95);
  QCOMPARE(candidates[1].matchScore, 80);
}

void TestMusicBrainzParser::parseSearchResponse_buildsArtistDashTitleDisplayName() {
  auto result = MusicBrainzParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value()[0].displayName, QStringLiteral("Artist X — Album A"));
  // joinphrase preserved verbatim (note the literal " feat. " glue).
  QCOMPARE(result.value()[1].displayName, QStringLiteral("Artist Y feat. Guest — Album B"));
}

void TestMusicBrainzParser::parseSearchResponse_subtitleConcatenatesDateCountryFormat() {
  auto result = MusicBrainzParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value()[0].subtitle, QStringLiteral("2020-05-01 · US · CD"));
}

void TestMusicBrainzParser::parseSearchResponse_skipsEntriesWithoutMbid() {
  auto result = MusicBrainzParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  // Three releases in the fixture; one has empty id and must be dropped.
  QCOMPARE(result.value().size(), 2);
}

void TestMusicBrainzParser::parseSearchResponse_skipsNonUuidMbid() {
  // A release whose id isn't a canonical UUID (here a path-traversal attempt)
  // must be dropped, never turned into a coverartarchive.org URL path or a
  // detail-fetch id (Kartend-tjyh).
  const QByteArray fixture = R"({
    "releases": [
      {"id": "../../evil", "score": 99, "title": "Traversal"},
      {"id": "11111111-1111-4111-8111-111111111111", "score": 50, "title": "Valid"}
    ]
  })";
  auto result = MusicBrainzParser::parseSearchResponse(fixture);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0].providerSpecificId,
           QStringLiteral("11111111-1111-4111-8111-111111111111"));
}

void TestMusicBrainzParser::parseSearchResponse_thumbnailUsesCoverArtArchiveUrl() {
  auto result = MusicBrainzParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  const auto thumb = result.value()[0].thumbnailUrl.toString();
  QVERIFY2(
      thumb.startsWith("https://coverartarchive.org/release/11111111-1111-4111-8111-111111111111/"),
      qPrintable(thumb));
  QVERIFY(thumb.endsWith(".jpg"));
}

void TestMusicBrainzParser::parseSearchResponse_emptyReleasesArrayIsSuccessNotError() {
  // "no match" is a successful empty list, not a parse failure.
  const QByteArray empty = R"({"created":"x","count":0,"offset":0,"releases":[]})";
  auto result = MusicBrainzParser::parseSearchResponse(empty);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 0);
}

void TestMusicBrainzParser::parseSearchResponse_malformedJsonReturnsError() {
  auto result = MusicBrainzParser::parseSearchResponse(QByteArray("{not even json"));
  QVERIFY(result.isError());
}

void TestMusicBrainzParser::parseDetailResponse_extractsTitleArtistLabelDateGenre() {
  auto result = MusicBrainzParser::parseDetailResponse(DETAIL_FIXTURE);
  QVERIFY(result.isOk());
  const auto item = result.value();
  QCOMPARE(item.title, QStringLiteral("Artist X — Album A"));
  QCOMPARE(item.publisher, QStringLiteral("Label One, Label Two"));
  QCOMPARE(item.releaseDate, QStringLiteral("2020-05-01"));
  QCOMPARE(item.description, QStringLiteral("Studio album from 2020."));
  // Genres section (`genres` array) wins over `tags` when present.
  QCOMPARE(item.genre, QStringLiteral("rock, indie"));
  QCOMPARE(item.sourceProviderId, QStringLiteral("musicbrainz"));
}

void TestMusicBrainzParser::parseDetailResponse_storesMbidAndTrackCountInCustomFields() {
  auto result = MusicBrainzParser::parseDetailResponse(DETAIL_FIXTURE);
  QVERIFY(result.isOk());
  const auto item = result.value();
  QCOMPARE(item.customFields.value("musicbrainz_id"),
           QStringLiteral("11111111-1111-4111-8111-111111111111"));
  QCOMPARE(item.customFields.value("track_count"), QStringLiteral("12"));
  QCOMPARE(item.customFields.value("country"), QStringLiteral("US"));
  QCOMPARE(item.customFields.value("barcode"), QStringLiteral("0123456789"));
}

void TestMusicBrainzParser::parseDetailResponse_includesFrontAndBackCoverMedia() {
  auto result = MusicBrainzParser::parseDetailResponse(DETAIL_FIXTURE);
  QVERIFY(result.isOk());
  const auto item = result.value();
  QCOMPARE(item.media.size(), 2);
  QStringList types;
  for (const auto &m : item.media) types.append(m.type);
  QVERIFY(types.contains(QStringLiteral("front")));
  QVERIFY(types.contains(QStringLiteral("back")));
  QVERIFY(item.media.first().url.toString().contains(
      "coverartarchive.org/release/11111111-1111-4111-8111-111111111111"));
}

void TestMusicBrainzParser::parseDetailResponse_genresPreferredOverTags() {
  // When only `tags` is present, parser falls back to top-5-by-count.
  auto result = MusicBrainzParser::parseDetailResponse(DETAIL_FIXTURE_TAGS_ONLY);
  QVERIFY(result.isOk());
  const QString genre = result.value().genre;
  // Top by count: delta(12), beta(8), gamma(5), epsilon(2), alpha(1).
  QCOMPARE(genre, QStringLiteral("delta, beta, gamma, epsilon, alpha"));
}

void TestMusicBrainzParser::appendCoverArtUrls_skipsEmptyMbid() {
  Scraper::ScrapedItem item;
  MusicBrainzParser::appendCoverArtUrls(item, QString());
  QCOMPARE(item.media.size(), 0);
  MusicBrainzParser::appendCoverArtUrls(item,
                                        QStringLiteral("11111111-1111-4111-8111-111111111111"));
  QCOMPARE(item.media.size(), 2);
}

void TestMusicBrainzParser::appendCoverArtUrls_skipsNonUuidMbid() {
  Scraper::ScrapedItem item;
  // A non-UUID id (the detail response's `id` is independent untrusted data)
  // must not be interpolated into a cover URL (Kartend-tjyh).
  MusicBrainzParser::appendCoverArtUrls(item, QStringLiteral("../../evil"));
  MusicBrainzParser::appendCoverArtUrls(item, QStringLiteral("not-a-uuid"));
  QCOMPARE(item.media.size(), 0);
}

QTEST_MAIN(TestMusicBrainzParser)
#include "test_musicbrainzparser.moc"
