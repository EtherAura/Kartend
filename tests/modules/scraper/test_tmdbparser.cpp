// Tests for TmdbParser. Pure JSON → typed parsing — no network.
// Fixtures shaped to match real TMDB v3 API responses for /search/multi
// and /movie/<id>?append_to_response=release_dates and
// /tv/<id>?append_to_response=content_ratings.
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "tmdbparser.h"

class TestTmdbParser : public QObject {
  Q_OBJECT
private slots:
  void parseSearchResponse_returnsMovieAndTvCandidatesSkipsPersons();
  void parseSearchResponse_displayNameUsesTitleForMovieAndNameForTv();
  void parseSearchResponse_subtitleHasTypeAndYear();
  void parseSearchResponse_idCarriesMediaTypePrefix();
  void parseSearchResponse_thumbnailFromPosterPath();
  void parseSearchResponse_emptyResultsArrayIsSuccess();
  void parseSearchResponse_malformedJsonReturnsError();
  void parseDetailResponse_movieMapsCanonicalFields();
  void parseDetailResponse_tvMapsAirDateAndEpisodeRuntime();
  void parseDetailResponse_contentRatingPrefersUS();
  void parseDetailResponse_runtimeMinutesConvertedToSeconds();
  void parseDetailResponse_appendsPosterAndBackdrop();
};

namespace {

const QByteArray SEARCH_FIXTURE = R"({
  "page": 1,
  "results": [
    {
      "id": 11,
      "media_type": "movie",
      "title": "Star Wars",
      "release_date": "1977-05-25",
      "vote_average": 8.2,
      "poster_path": "/poster1.jpg"
    },
    {
      "id": 4194,
      "media_type": "tv",
      "name": "Star Trek",
      "first_air_date": "1966-09-08",
      "vote_average": 7.9,
      "poster_path": "/poster2.jpg"
    },
    {
      "id": 8001,
      "media_type": "person",
      "name": "Some Actor"
    }
  ]
})";

const QByteArray MOVIE_DETAIL_FIXTURE = R"({
  "id": 11,
  "title": "Star Wars",
  "overview": "A long time ago...",
  "release_date": "1977-05-25",
  "runtime": 121,
  "tagline": "A long time ago in a galaxy far, far away...",
  "vote_average": 8.2,
  "poster_path": "/poster1.jpg",
  "backdrop_path": "/backdrop1.jpg",
  "genres": [{"id": 1, "name": "Sci-Fi"}, {"id": 2, "name": "Adventure"}],
  "production_companies": [{"id": 1, "name": "Lucasfilm"}],
  "release_dates": {
    "results": [
      {"iso_3166_1": "GB", "release_dates": [{"certification": "U"}]},
      {"iso_3166_1": "US", "release_dates": [{"certification": "PG"}]}
    ]
  }
})";

const QByteArray TV_DETAIL_FIXTURE = R"({
  "id": 4194,
  "name": "Star Trek",
  "overview": "Pilot episode of...",
  "first_air_date": "1966-09-08",
  "episode_run_time": [50, 60],
  "poster_path": "/posterB.jpg",
  "backdrop_path": "/backdropB.jpg",
  "genres": [{"id": 1, "name": "Sci-Fi"}],
  "production_companies": [{"id": 1, "name": "Desilu"}],
  "content_ratings": {
    "results": [
      {"iso_3166_1": "DE", "rating": "12"},
      {"iso_3166_1": "US", "rating": "TV-PG"}
    ]
  }
})";

} // namespace

void TestTmdbParser::parseSearchResponse_returnsMovieAndTvCandidatesSkipsPersons() {
  auto result = TmdbParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  // Three results: movie + tv + person. Person row skipped.
  QCOMPARE(result.value().size(), 2);
}

void TestTmdbParser::parseSearchResponse_displayNameUsesTitleForMovieAndNameForTv() {
  auto result = TmdbParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value()[0].displayName, QStringLiteral("Star Wars"));
  QCOMPARE(result.value()[1].displayName, QStringLiteral("Star Trek"));
}

void TestTmdbParser::parseSearchResponse_subtitleHasTypeAndYear() {
  auto result = TmdbParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value()[0].subtitle, QStringLiteral("Movie · 1977"));
  QCOMPARE(result.value()[1].subtitle, QStringLiteral("TV · 1966"));
}

void TestTmdbParser::parseSearchResponse_idCarriesMediaTypePrefix() {
  auto result = TmdbParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value()[0].providerSpecificId, QStringLiteral("movie:11"));
  QCOMPARE(result.value()[1].providerSpecificId, QStringLiteral("tv:4194"));
}

void TestTmdbParser::parseSearchResponse_thumbnailFromPosterPath() {
  auto result = TmdbParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  QVERIFY(result.value()[0].thumbnailUrl.toString().contains("/poster1.jpg"));
  QVERIFY(result.value()[0].thumbnailUrl.toString().contains("image.tmdb.org"));
}

void TestTmdbParser::parseSearchResponse_emptyResultsArrayIsSuccess() {
  auto result = TmdbParser::parseSearchResponse(R"({"page":1,"results":[]})");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 0);
}

void TestTmdbParser::parseSearchResponse_malformedJsonReturnsError() {
  auto result = TmdbParser::parseSearchResponse(QByteArray("{not json"));
  QVERIFY(result.isError());
}

void TestTmdbParser::parseDetailResponse_movieMapsCanonicalFields() {
  auto result = TmdbParser::parseDetailResponse(MOVIE_DETAIL_FIXTURE, QStringLiteral("movie"));
  QVERIFY(result.isOk());
  const auto item = result.value();
  QCOMPARE(item.title, QStringLiteral("Star Wars"));
  QCOMPARE(item.description, QStringLiteral("A long time ago..."));
  QCOMPARE(item.releaseDate, QStringLiteral("1977-05-25"));
  QCOMPARE(item.publisher, QStringLiteral("Lucasfilm"));
  QCOMPARE(item.genre, QStringLiteral("Sci-Fi, Adventure"));
  QCOMPARE(item.sourceProviderId, QStringLiteral("tmdb"));
  QCOMPARE(item.customFields.value("media_type"), QStringLiteral("movie"));
  QCOMPARE(item.customFields.value("tmdb_id"), QStringLiteral("11"));
  QCOMPARE(item.customFields.value("tagline"),
           QStringLiteral("A long time ago in a galaxy far, far away..."));
}

void TestTmdbParser::parseDetailResponse_tvMapsAirDateAndEpisodeRuntime() {
  auto result = TmdbParser::parseDetailResponse(TV_DETAIL_FIXTURE, QStringLiteral("tv"));
  QVERIFY(result.isOk());
  const auto item = result.value();
  QCOMPARE(item.title, QStringLiteral("Star Trek"));
  QCOMPARE(item.releaseDate, QStringLiteral("1966-09-08"));
  // First entry of episode_run_time = canonical episode length.
  QCOMPARE(item.runtimeSeconds, 50 * 60);
}

void TestTmdbParser::parseDetailResponse_contentRatingPrefersUS() {
  auto movie = TmdbParser::parseDetailResponse(MOVIE_DETAIL_FIXTURE, QStringLiteral("movie"));
  QVERIFY(movie.isOk());
  QCOMPARE(movie.value().contentRating, QStringLiteral("PG"));
  auto tv = TmdbParser::parseDetailResponse(TV_DETAIL_FIXTURE, QStringLiteral("tv"));
  QVERIFY(tv.isOk());
  QCOMPARE(tv.value().contentRating, QStringLiteral("TV-PG"));
}

void TestTmdbParser::parseDetailResponse_runtimeMinutesConvertedToSeconds() {
  auto result = TmdbParser::parseDetailResponse(MOVIE_DETAIL_FIXTURE, QStringLiteral("movie"));
  QVERIFY(result.isOk());
  QCOMPARE(result.value().runtimeSeconds, 121 * 60);
}

void TestTmdbParser::parseDetailResponse_appendsPosterAndBackdrop() {
  auto result = TmdbParser::parseDetailResponse(MOVIE_DETAIL_FIXTURE, QStringLiteral("movie"));
  QVERIFY(result.isOk());
  const auto item = result.value();
  QCOMPARE(item.media.size(), 2);
  QCOMPARE(item.media[0].type, QStringLiteral("front"));
  QVERIFY(item.media[0].url.toString().contains("/poster1.jpg"));
  QCOMPARE(item.media[1].type, QStringLiteral("fanart"));
  QVERIFY(item.media[1].url.toString().contains("/backdrop1.jpg"));
}

QTEST_MAIN(TestTmdbParser)
#include "test_tmdbparser.moc"
