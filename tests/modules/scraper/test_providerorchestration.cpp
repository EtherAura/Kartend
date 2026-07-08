// Provider-orchestration tests (Kartend-ym7z3): the request -> parse ->
// callback chain of the API-backed providers (MusicBrainz, Open Library,
// TMDB), exercised end-to-end through ProviderBase's test fetch seam
// (ProviderBase::setFetchFunctionForTesting) with canned JSON bodies —
// no QNetworkAccessManager, no network. The canned payloads mirror the
// inline fixtures of the per-provider parser suites
// (test_musicbrainzparser / test_openlibraryparser / test_tmdbparser),
// which keep owning the exhaustive field-mapping assertions; here we
// assert the orchestration contract instead:
//   - the outbound URL (endpoint + query params) and headers
//     (User-Agent always, Authorization for TMDB) of each lookup /
//     fetchDetail / fetchMediaBytes call;
//   - transport errors propagate to the caller's callback unchanged;
//   - parser errors (malformed body) surface as error Results;
//   - guard paths (blank query, missing candidate id, missing TMDB
//     token) answer synchronously without issuing a request;
//   - the shared bounded transient-retry loop in ProviderBase::getWithRetry
//     (Kartend-jjyst.10): a transient failure re-dispatches the same request
//     (each attempt is a separate captured request — the seam sits below the
//     loop), a deliberate RequestQueueCleared cancel never retries, and the
//     attempt count is bounded. Classification/backoff math is owned by
//     test_scraperretrypolicy.cpp.
// WebSearchProvider is deliberately absent: it has no HTTP path at all
// (it only builds a browser URL), and that surface is already covered
// by test_providerrequests.cpp.
#include <memory>
#include <utility>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "errorutils.h"
#include "musicbrainzprovider.h"
#include "openlibraryprovider.h"
#include "providerbase.h"
#include "scraperretrypolicy.h"
#include "scrapertypes.h"
#include "tmdbprovider.h"

namespace {

/// One captured outbound request: the URL/headers the provider built,
/// recorded by the fake fetch before the canned response is delivered.
struct CapturedRequest {
  QUrl url;
  Scraper::HttpClient::RawHeaders headers;
};

QByteArray headerValue(const Scraper::HttpClient::RawHeaders &headers, const QByteArray &name) {
  for (const auto &[key, value] : headers) {
    if (key == name) return value;
  }
  return {};
}

// --- Canned payloads -------------------------------------------------
// Shaped like the real API responses; trimmed copies of the parser-suite
// fixtures (those suites own the exhaustive parsing assertions).

const QByteArray MB_SEARCH_JSON = R"({
  "releases": [
    {
      "id": "11111111-1111-4111-8111-111111111111",
      "score": 95,
      "title": "Album A",
      "artist-credit": [{"name": "Artist X", "joinphrase": ""}],
      "date": "2020-05-01",
      "country": "US",
      "media": [{"format": "CD", "track-count": 10}]
    },
    {
      "id": "22222222-2222-4222-8222-222222222222",
      "score": 80,
      "title": "Album B",
      "artist-credit": [{"name": "Artist Y", "joinphrase": ""}],
      "date": "2021-03-10",
      "country": "GB",
      "media": [{"format": "Digital Media", "track-count": 8}]
    }
  ]
})";

const QByteArray MB_DETAIL_JSON = R"({
  "id": "11111111-1111-4111-8111-111111111111",
  "title": "Album A",
  "artist-credit": [{"name": "Artist X"}],
  "date": "2020-05-01",
  "country": "US",
  "label-info": [{"label": {"name": "Label One"}}],
  "media": [{"format": "CD", "track-count": 10}],
  "genres": [{"name": "electronic"}]
})";

const QByteArray OL_SEARCH_JSON = R"({
  "numFound": 1,
  "docs": [
    {
      "key": "/works/OL12345W",
      "title": "Foundation",
      "author_name": ["Isaac Asimov"],
      "first_publish_year": 1951,
      "cover_i": 8765432
    }
  ]
})";

const QByteArray OL_DETAIL_JSON = R"({
  "key": "/works/OL12345W",
  "title": "Foundation",
  "description": "First book in the series.",
  "subjects": ["Science Fiction"],
  "covers": [8765432],
  "first_publish_date": "1951-05-01"
})";

const QByteArray TMDB_SEARCH_JSON = R"({
  "page": 1,
  "results": [
    {
      "id": 11,
      "media_type": "movie",
      "title": "Movie A",
      "release_date": "1977-05-25",
      "vote_average": 8.2,
      "poster_path": "/poster1.jpg"
    },
    {
      "id": 4194,
      "media_type": "tv",
      "name": "Show B",
      "first_air_date": "1966-09-08",
      "vote_average": 7.9,
      "poster_path": "/poster2.jpg"
    }
  ]
})";

const QByteArray TMDB_MOVIE_DETAIL_JSON = R"({
  "id": 11,
  "title": "Movie A",
  "overview": "A feature film.",
  "release_date": "1977-05-25",
  "runtime": 121,
  "genres": [{"id": 1, "name": "Sci-Fi"}],
  "production_companies": [{"id": 1, "name": "Studio One"}],
  "release_dates": {
    "results": [{"iso_3166_1": "US", "release_dates": [{"certification": "PG"}]}]
  }
})";

const QByteArray TMDB_TV_DETAIL_JSON = R"({
  "id": 4194,
  "name": "Show B",
  "overview": "A series.",
  "first_air_date": "1966-09-08",
  "episode_run_time": [50],
  "genres": [{"id": 1, "name": "Sci-Fi"}],
  "content_ratings": {
    "results": [{"iso_3166_1": "US", "rating": "TV-PG"}]
  }
})";

ErrorUtils::ErrorContext cannedTransportError() {
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                         QStringLiteral("simulated transport failure"),
                                         QStringLiteral("FakeFetch"));
}

/// A server-side 5xx — RetryPolicy::isTransient classifies it retryable, so
/// it exercises ProviderBase's bounded retry loop (Kartend-jjyst.10).
ErrorUtils::ErrorContext transient503Error() {
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::NetworkRequestFailed,
                                         QStringLiteral("simulated HTTP 503"),
                                         QStringLiteral("FakeFetch"))
      .withHttpStatus(503);
}

/// HttpClient::clearPending's marker for a deliberately cancelled queued
/// request (batch cancel). Must NEVER be retried (Kartend-jjyst.2).
ErrorUtils::ErrorContext queueClearedError() {
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::RequestQueueCleared,
                                         QStringLiteral("request queue cleared"),
                                         QStringLiteral("FakeFetch"));
}

GeneralSettings settingsWithTmdbToken(const QString &token) {
  GeneralSettings settings;
  settings.scraper.credentials[QStringLiteral("tmdb")][QStringLiteral("api_token")] = token;
  return settings;
}

} // namespace

class TestProviderOrchestration : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();

  // MusicBrainz
  void mb_lookup_buildsSearchRequest_andDeliversParsedCandidates();
  void mb_lookup_blankQuery_deliversEmptyListWithoutRequest();
  void mb_lookup_transportErrorPropagatesUnchanged();
  void mb_lookup_malformedBodySurfacesParserError();
  void mb_fetchDetail_buildsMbidRequest_andDeliversParsedItem();
  void mb_fetchDetail_missingMbid_errorsWithoutRequest();
  void mb_fetchMediaBytes_deliversBytesAndSendsUserAgent();

  // Open Library
  void ol_lookup_titleQuery_usesFreeTextParam_andDeliversCandidates();
  void ol_lookup_isbnQuery_usesIsbnParam();
  void ol_lookup_transportErrorPropagatesUnchanged();
  void ol_fetchDetail_requestsWorkJson_andDeliversParsedItem();
  void ol_fetchDetail_missingWorkKey_errorsWithoutRequest();

  // TMDB
  void tmdb_lookup_missingToken_errorsWithoutRequest();
  void tmdb_lookup_sendsBearerTokenToMultiSearch_andDeliversCandidates();
  void tmdb_fetchDetail_movieRoute_requestsReleaseDates_andDeliversItem();
  void tmdb_fetchDetail_tvRoute_requestsContentRatings();
  void tmdb_fetchDetail_malformedCandidateId_errorsWithoutRequest();
  void tmdb_fetchMediaBytes_userAgentOnly_noAuthorizationLeak();
  // Kartend-ckepd.5/.6: TMDB Collection entity scraping.
  void tmdb_fetchEntity_collection_searchesByName_stampsUuidScopeKeyAndProvider();
  void tmdb_fetchEntity_nonCollectionType_errorsWithoutRequest();
  void tmdb_fetchEntity_missingToken_errorsWithoutRequest();
  void tmdb_fetchEntity_emptyName_errorsWithoutRequest();
  void tmdb_fetchEntity_emptyResults_isNotFound();

  // ProviderBase transient-retry loop (Kartend-jjyst.10), driven through
  // MusicBrainz as a representative getJson consumer.
  void retry_transient503ThenSuccess_redispatchesAndDeliversOneResult();
  void retry_transient503Persisting_boundedAttemptsThenSurfacesError();
  void retry_requestQueueCleared_isNotRetried();

private:
  /// Arms the seam to capture every request into m_requests and answer
  /// with @p response (one canned response reused for every request the
  /// test issues — each test drives exactly one HTTP roundtrip).
  void respondWith(ErrorUtils::Result<QByteArray> response);

  /// Sequence variant for the retry tests: each captured request consumes
  /// the next canned response; the last one repeats once the list is down
  /// to a single entry (so "always 503" is just a one-element sequence).
  void respondWithSequence(QList<ErrorUtils::Result<QByteArray>> responses);

  QList<CapturedRequest> m_requests;
};

void TestProviderOrchestration::init() {
  m_requests.clear();
}

void TestProviderOrchestration::cleanup() {
  // Restore the live-HttpClient path so no seam state leaks across tests.
  ProviderBase::setFetchFunctionForTesting({});
}

void TestProviderOrchestration::respondWith(ErrorUtils::Result<QByteArray> response) {
  ProviderBase::setFetchFunctionForTesting(
      [this, response = std::move(response)](const QUrl &url,
                                             const Scraper::HttpClient::RawHeaders &headers,
                                             Scraper::HttpClient::ResponseCallback callback,
                                             const QStringList & /*allowedHostSuffixes*/) {
        m_requests.append({url, headers});
        callback(response);
      });
}

void TestProviderOrchestration::respondWithSequence(
    QList<ErrorUtils::Result<QByteArray>> responses) {
  // shared_ptr: the seam lambda must be copyable (std::function), and the
  // remaining-responses list mutates across dispatches.
  auto remaining = std::make_shared<QList<ErrorUtils::Result<QByteArray>>>(std::move(responses));
  ProviderBase::setFetchFunctionForTesting(
      [this, remaining](const QUrl &url, const Scraper::HttpClient::RawHeaders &headers,
                        Scraper::HttpClient::ResponseCallback callback,
                        const QStringList & /*allowedHostSuffixes*/) {
        m_requests.append({url, headers});
        const ErrorUtils::Result<QByteArray> response =
            remaining->size() > 1 ? remaining->takeFirst() : remaining->first();
        callback(response);
      });
}

// --- MusicBrainz ------------------------------------------------------

void TestProviderOrchestration::mb_lookup_buildsSearchRequest_andDeliversParsedCandidates() {
  respondWith(MB_SEARCH_JSON);
  MusicBrainzProvider provider;

  QList<Scraper::ScrapeCandidate> delivered;
  bool called = false;
  provider.lookup(QStringLiteral("  Album A  "),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isOk());
                    delivered = result.value();
                  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  const QUrl url = m_requests.first().url;
  QCOMPARE(url.host(), QStringLiteral("musicbrainz.org"));
  QCOMPARE(url.path(), QStringLiteral("/ws/2/release/"));
  const QUrlQuery query(url);
  QCOMPARE(query.queryItemValue(QStringLiteral("query")), QStringLiteral("Album A"));
  QCOMPARE(query.queryItemValue(QStringLiteral("fmt")), QStringLiteral("json"));
  QCOMPARE(query.queryItemValue(QStringLiteral("limit")), QStringLiteral("10"));
  QVERIFY(headerValue(m_requests.first().headers, QByteArrayLiteral("User-Agent"))
              .startsWith(QByteArrayLiteral("Kartend/")));

  QCOMPARE(delivered.size(), 2);
  QCOMPARE(delivered.first().providerSpecificId,
           QStringLiteral("11111111-1111-4111-8111-111111111111"));
}

void TestProviderOrchestration::mb_lookup_blankQuery_deliversEmptyListWithoutRequest() {
  respondWith(MB_SEARCH_JSON);
  MusicBrainzProvider provider;

  bool called = false;
  provider.lookup(QStringLiteral("   "),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isOk());
                    QVERIFY(result.value().isEmpty());
                  });

  QVERIFY(called);
  QVERIFY(m_requests.isEmpty());
}

void TestProviderOrchestration::mb_lookup_transportErrorPropagatesUnchanged() {
  respondWith(cannedTransportError());
  MusicBrainzProvider provider;

  bool called = false;
  provider.lookup(QStringLiteral("Album A"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isError());
                    QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileReadError);
                    QCOMPARE(result.error().message, QStringLiteral("simulated transport failure"));
                  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
}

void TestProviderOrchestration::mb_lookup_malformedBodySurfacesParserError() {
  respondWith(QByteArrayLiteral("this is not json"));
  MusicBrainzProvider provider;

  bool called = false;
  provider.lookup(QStringLiteral("Album A"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isError());
                  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
}

void TestProviderOrchestration::mb_fetchDetail_buildsMbidRequest_andDeliversParsedItem() {
  respondWith(MB_DETAIL_JSON);
  MusicBrainzProvider provider;

  Scraper::ScrapeCandidate candidate;
  candidate.providerSpecificId = QStringLiteral("11111111-1111-4111-8111-111111111111");

  bool called = false;
  provider.fetchDetail(candidate, [&](ErrorUtils::Result<Scraper::ScrapedItem> result) {
    called = true;
    QVERIFY(result.isOk());
    // The MB parser titles items "Artist — Album" (the em-dash join).
    QCOMPARE(result.value().title, QStringLiteral("Artist X — Album A"));
    QCOMPARE(result.value().sourceProviderId, QStringLiteral("musicbrainz"));
  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  const QUrl url = m_requests.first().url;
  QCOMPARE(url.path(), QStringLiteral("/ws/2/release/11111111-1111-4111-8111-111111111111"));
  const QUrlQuery query(url);
  QCOMPARE(query.queryItemValue(QStringLiteral("fmt")), QStringLiteral("json"));
  QVERIFY(query.queryItemValue(QStringLiteral("inc")).contains(QStringLiteral("artists")));
}

void TestProviderOrchestration::mb_fetchDetail_missingMbid_errorsWithoutRequest() {
  respondWith(MB_DETAIL_JSON);
  MusicBrainzProvider provider;

  bool called = false;
  provider.fetchDetail(Scraper::ScrapeCandidate{},
                       [&](ErrorUtils::Result<Scraper::ScrapedItem> result) {
                         called = true;
                         QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidArgument));
                       });

  QVERIFY(called);
  QVERIFY(m_requests.isEmpty());
}

void TestProviderOrchestration::mb_fetchMediaBytes_deliversBytesAndSendsUserAgent() {
  respondWith(QByteArrayLiteral("\x89PNG fake image bytes"));
  MusicBrainzProvider provider;

  const QUrl mediaUrl(QStringLiteral(
      "https://coverartarchive.org/release/11111111-1111-4111-8111-111111111111/front"));
  bool called = false;
  provider.fetchMediaBytes(mediaUrl, [&](ErrorUtils::Result<QByteArray> result) {
    called = true;
    QVERIFY(result.isOk());
    QCOMPARE(result.value(), QByteArrayLiteral("\x89PNG fake image bytes"));
  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  QCOMPARE(m_requests.first().url, mediaUrl);
  QVERIFY(headerValue(m_requests.first().headers, QByteArrayLiteral("User-Agent"))
              .startsWith(QByteArrayLiteral("Kartend/")));
}

// --- Open Library -----------------------------------------------------

void TestProviderOrchestration::ol_lookup_titleQuery_usesFreeTextParam_andDeliversCandidates() {
  respondWith(OL_SEARCH_JSON);
  OpenLibraryProvider provider;

  bool called = false;
  provider.lookup(QStringLiteral("Foundation"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isOk());
                    QCOMPARE(result.value().size(), 1);
                    QCOMPARE(result.value().first().providerSpecificId, QStringLiteral("OL12345W"));
                  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  const QUrl url = m_requests.first().url;
  QCOMPARE(url.host(), QStringLiteral("openlibrary.org"));
  QCOMPARE(url.path(), QStringLiteral("/search.json"));
  const QUrlQuery query(url);
  QCOMPARE(query.queryItemValue(QStringLiteral("q")), QStringLiteral("Foundation"));
  QVERIFY(!query.hasQueryItem(QStringLiteral("isbn")));
}

void TestProviderOrchestration::ol_lookup_isbnQuery_usesIsbnParam() {
  respondWith(OL_SEARCH_JSON);
  OpenLibraryProvider provider;

  bool called = false;
  provider.lookup(QStringLiteral("9780553293357 - Foundation"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isOk());
                  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  const QUrlQuery query(m_requests.first().url);
  QCOMPARE(query.queryItemValue(QStringLiteral("isbn")), QStringLiteral("9780553293357"));
  QVERIFY(!query.hasQueryItem(QStringLiteral("q")));
}

void TestProviderOrchestration::ol_lookup_transportErrorPropagatesUnchanged() {
  respondWith(cannedTransportError());
  OpenLibraryProvider provider;

  bool called = false;
  provider.lookup(QStringLiteral("Foundation"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isError());
                    QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileReadError);
                  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
}

void TestProviderOrchestration::ol_fetchDetail_requestsWorkJson_andDeliversParsedItem() {
  respondWith(OL_DETAIL_JSON);
  OpenLibraryProvider provider;

  Scraper::ScrapeCandidate candidate;
  candidate.providerSpecificId = QStringLiteral("OL12345W");

  bool called = false;
  provider.fetchDetail(candidate, [&](ErrorUtils::Result<Scraper::ScrapedItem> result) {
    called = true;
    QVERIFY(result.isOk());
    QCOMPARE(result.value().title, QStringLiteral("Foundation"));
  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  QCOMPARE(m_requests.first().url,
           QUrl(QStringLiteral("https://openlibrary.org/works/OL12345W.json")));
}

void TestProviderOrchestration::ol_fetchDetail_missingWorkKey_errorsWithoutRequest() {
  respondWith(OL_DETAIL_JSON);
  OpenLibraryProvider provider;

  bool called = false;
  provider.fetchDetail(Scraper::ScrapeCandidate{},
                       [&](ErrorUtils::Result<Scraper::ScrapedItem> result) {
                         called = true;
                         QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidArgument));
                       });

  QVERIFY(called);
  QVERIFY(m_requests.isEmpty());
}

// --- TMDB ---------------------------------------------------------------

void TestProviderOrchestration::tmdb_lookup_missingToken_errorsWithoutRequest() {
  respondWith(TMDB_SEARCH_JSON);
  TmdbProvider provider([]() -> const GeneralSettings * { return nullptr; },
                        []() -> const CollectionConfig * { return nullptr; });

  bool called = false;
  provider.lookup(QStringLiteral("Movie A"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidArgument));
                    QVERIFY(result.error().message.contains(QStringLiteral("token")));
                  });

  QVERIFY(called);
  QVERIFY(m_requests.isEmpty());
}

void TestProviderOrchestration::tmdb_lookup_sendsBearerTokenToMultiSearch_andDeliversCandidates() {
  respondWith(TMDB_SEARCH_JSON);
  const GeneralSettings settings = settingsWithTmdbToken(QStringLiteral("tok123"));
  TmdbProvider provider([&settings]() { return &settings; },
                        []() -> const CollectionConfig * { return nullptr; });

  bool called = false;
  provider.lookup(QStringLiteral("Movie A"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    called = true;
                    QVERIFY(result.isOk());
                    QCOMPARE(result.value().size(), 2);
                    QCOMPARE(result.value().first().providerSpecificId, QStringLiteral("movie:11"));
                  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  const QUrl url = m_requests.first().url;
  QCOMPARE(url.host(), QStringLiteral("api.themoviedb.org"));
  QCOMPARE(url.path(), QStringLiteral("/3/search/multi"));
  const QUrlQuery query(url);
  QCOMPARE(query.queryItemValue(QStringLiteral("query")), QStringLiteral("Movie A"));
  QCOMPARE(query.queryItemValue(QStringLiteral("include_adult")), QStringLiteral("false"));
  QCOMPARE(headerValue(m_requests.first().headers, QByteArrayLiteral("Authorization")),
           QByteArrayLiteral("Bearer tok123"));
}

void TestProviderOrchestration::tmdb_fetchDetail_movieRoute_requestsReleaseDates_andDeliversItem() {
  respondWith(TMDB_MOVIE_DETAIL_JSON);
  const GeneralSettings settings = settingsWithTmdbToken(QStringLiteral("tok123"));
  TmdbProvider provider([&settings]() { return &settings; },
                        []() -> const CollectionConfig * { return nullptr; });

  Scraper::ScrapeCandidate candidate;
  candidate.providerSpecificId = QStringLiteral("movie:11");

  bool called = false;
  provider.fetchDetail(candidate, [&](ErrorUtils::Result<Scraper::ScrapedItem> result) {
    called = true;
    QVERIFY(result.isOk());
    QCOMPARE(result.value().title, QStringLiteral("Movie A"));
  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  const QUrl url = m_requests.first().url;
  QCOMPARE(url.path(), QStringLiteral("/3/movie/11"));
  const QUrlQuery query(url);
  QCOMPARE(query.queryItemValue(QStringLiteral("append_to_response")),
           QStringLiteral("release_dates"));
}

void TestProviderOrchestration::tmdb_fetchDetail_tvRoute_requestsContentRatings() {
  respondWith(TMDB_TV_DETAIL_JSON);
  const GeneralSettings settings = settingsWithTmdbToken(QStringLiteral("tok123"));
  TmdbProvider provider([&settings]() { return &settings; },
                        []() -> const CollectionConfig * { return nullptr; });

  Scraper::ScrapeCandidate candidate;
  candidate.providerSpecificId = QStringLiteral("tv:4194");

  bool called = false;
  provider.fetchDetail(candidate, [&](ErrorUtils::Result<Scraper::ScrapedItem> result) {
    called = true;
    QVERIFY(result.isOk());
    QCOMPARE(result.value().title, QStringLiteral("Show B"));
  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  const QUrl url = m_requests.first().url;
  QCOMPARE(url.path(), QStringLiteral("/3/tv/4194"));
  const QUrlQuery query(url);
  QCOMPARE(query.queryItemValue(QStringLiteral("append_to_response")),
           QStringLiteral("content_ratings"));
}

void TestProviderOrchestration::tmdb_fetchDetail_malformedCandidateId_errorsWithoutRequest() {
  respondWith(TMDB_MOVIE_DETAIL_JSON);
  const GeneralSettings settings = settingsWithTmdbToken(QStringLiteral("tok123"));
  TmdbProvider provider([&settings]() { return &settings; },
                        []() -> const CollectionConfig * { return nullptr; });

  // No media-type prefix, unknown media type, and non-numeric id must all
  // refuse before any request is issued (the Kartend-tjyh injection guard).
  const QStringList badIds = {QStringLiteral("11"), QStringLiteral("person:11"),
                              QStringLiteral("movie:11/../tv")};
  for (const QString &badId : badIds) {
    Scraper::ScrapeCandidate candidate;
    candidate.providerSpecificId = badId;
    bool called = false;
    provider.fetchDetail(candidate, [&](ErrorUtils::Result<Scraper::ScrapedItem> result) {
      called = true;
      QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidArgument));
    });
    QVERIFY(called);
  }
  QVERIFY(m_requests.isEmpty());
}

void TestProviderOrchestration::tmdb_fetchMediaBytes_userAgentOnly_noAuthorizationLeak() {
  respondWith(QByteArrayLiteral("jpeg bytes"));
  const GeneralSettings settings = settingsWithTmdbToken(QStringLiteral("tok123"));
  TmdbProvider provider([&settings]() { return &settings; },
                        []() -> const CollectionConfig * { return nullptr; });

  const QUrl imageUrl(QStringLiteral("https://image.tmdb.org/t/p/original/poster1.jpg"));
  bool called = false;
  provider.fetchMediaBytes(imageUrl, [&](ErrorUtils::Result<QByteArray> result) {
    called = true;
    QVERIFY(result.isOk());
    QCOMPARE(result.value(), QByteArrayLiteral("jpeg bytes"));
  });

  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  QCOMPARE(m_requests.first().url, imageUrl);
  // The image CDN is public: the bearer token must not ride along.
  QVERIFY(headerValue(m_requests.first().headers, QByteArrayLiteral("Authorization")).isEmpty());
  QVERIFY(headerValue(m_requests.first().headers, QByteArrayLiteral("User-Agent"))
              .startsWith(QByteArrayLiteral("Kartend/")));
}

// --- TMDB Collection entity scraping (Kartend-ckepd.5/.6) ------------------

void TestProviderOrchestration::
    tmdb_fetchEntity_collection_searchesByName_stampsUuidScopeKeyAndProvider() {
  // fetchEntity(Collection) searches TMDB by the collection NAME (from the
  // accessor), maps the top match's poster → Collection-scoped Logo and backdrop
  // → Collection-scoped Background, stamps the OWNING collection uuid (the target
  // identity) as scopeKey, and tags sourceProviderId=tmdb. The parser leaves
  // scopeKey empty, so the PROVIDER is the only place the uuid-stamping and
  // sourceProviderId are covered.
  const QByteArray body = R"({"results":[
    {"id":10,"name":"Star Wars Collection","poster_path":"/poster.jpg","backdrop_path":"/backdrop.jpg"}
  ]})";
  respondWith(body);
  const GeneralSettings settings = settingsWithTmdbToken(QStringLiteral("tok123"));
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Star Wars");
  TmdbProvider provider([&settings]() { return &settings; },
                        [&cfg]() -> const CollectionConfig * { return &cfg; });

  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("coll-uuid-123");

  bool called = false;
  provider.fetchEntity(target, [&](const ErrorUtils::Result<Scraper::ScrapedItem> &result) {
    called = true;
    QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
    const auto &item = result.value();
    QCOMPARE(item.sourceProviderId, QStringLiteral("tmdb"));
    QCOMPARE(item.media.size(), 2);
    QVERIFY(item.media[0].scope == Scraper::MediaScope::Collection);
    QVERIFY(item.media[0].entityRole == Scraper::EntityArtRole::Logo);
    QVERIFY(item.media[1].scope == Scraper::MediaScope::Collection);
    QVERIFY(item.media[1].entityRole == Scraper::EntityArtRole::Background);
    for (const Scraper::MediaAsset &a : item.media) {
      QCOMPARE(a.scopeKey, QStringLiteral("coll-uuid-123"));
    }
  });
  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
  const QUrl url = m_requests.first().url;
  QCOMPARE(url.host(), QStringLiteral("api.themoviedb.org"));
  QCOMPARE(url.path(), QStringLiteral("/3/search/collection"));
  QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("query")), QStringLiteral("Star Wars"));
  QCOMPARE(headerValue(m_requests.first().headers, QByteArrayLiteral("Authorization")),
           QByteArrayLiteral("Bearer tok123"));
}

void TestProviderOrchestration::tmdb_fetchEntity_nonCollectionType_errorsWithoutRequest() {
  // TMDB entity scraping only supports Collection — a Platform target is rejected
  // synchronously with InvalidArgument and no HTTP request.
  respondWith(QByteArrayLiteral("{}"));
  const GeneralSettings settings = settingsWithTmdbToken(QStringLiteral("tok123"));
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Star Wars");
  TmdbProvider provider([&settings]() { return &settings; },
                        [&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Platform;
  target.identity = QStringLiteral("42");
  bool called = false;
  provider.fetchEntity(target, [&](const ErrorUtils::Result<Scraper::ScrapedItem> &result) {
    called = true;
    QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidArgument));
  });
  QVERIFY(called);
  QVERIFY(m_requests.isEmpty());
}

void TestProviderOrchestration::tmdb_fetchEntity_missingToken_errorsWithoutRequest() {
  // No API token → InvalidArgument (not configured), before any request.
  respondWith(QByteArrayLiteral("{}"));
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Star Wars");
  TmdbProvider provider([]() -> const GeneralSettings * { return nullptr; },
                        [&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("coll-uuid");
  bool called = false;
  provider.fetchEntity(target, [&](const ErrorUtils::Result<Scraper::ScrapedItem> &result) {
    called = true;
    QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidArgument));
  });
  QVERIFY(called);
  QVERIFY(m_requests.isEmpty());
}

void TestProviderOrchestration::tmdb_fetchEntity_emptyName_errorsWithoutRequest() {
  // Token present but the collection has no resolvable name (null accessor) →
  // InvalidArgument, no request (TMDB can't be searched without a query).
  respondWith(QByteArrayLiteral("{}"));
  const GeneralSettings settings = settingsWithTmdbToken(QStringLiteral("tok123"));
  TmdbProvider provider([&settings]() { return &settings; },
                        []() -> const CollectionConfig * { return nullptr; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("coll-uuid");
  bool called = false;
  provider.fetchEntity(target, [&](const ErrorUtils::Result<Scraper::ScrapedItem> &result) {
    called = true;
    QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidArgument));
  });
  QVERIFY(called);
  QVERIFY(m_requests.isEmpty());
}

void TestProviderOrchestration::tmdb_fetchEntity_emptyResults_isNotFound() {
  // A search that matches nothing is a routine RemoteResourceNotFound
  // (re-queueable, e8aag) — niche collections often have no TMDB franchise. The
  // request DID go out; it just matched nothing.
  respondWith(QByteArrayLiteral(R"({"results":[]})"));
  const GeneralSettings settings = settingsWithTmdbToken(QStringLiteral("tok123"));
  CollectionConfig cfg;
  cfg.name = QStringLiteral("No Such Franchise");
  TmdbProvider provider([&settings]() { return &settings; },
                        [&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("coll-uuid");
  bool called = false;
  provider.fetchEntity(target, [&](const ErrorUtils::Result<Scraper::ScrapedItem> &result) {
    called = true;
    QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::RemoteResourceNotFound));
  });
  QVERIFY(called);
  QCOMPARE(m_requests.size(), 1);
}

// --- ProviderBase transient-retry loop (Kartend-jjyst.10) ---------------

void TestProviderOrchestration::retry_transient503ThenSuccess_redispatchesAndDeliversOneResult() {
  respondWithSequence({transient503Error(), MB_SEARCH_JSON});
  MusicBrainzProvider provider;

  int deliveries = 0;
  QList<Scraper::ScrapeCandidate> delivered;
  provider.lookup(QStringLiteral("Album A"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    ++deliveries;
                    QVERIFY(result.isOk());
                    delivered = result.value();
                  });

  // The first attempt answered 503 synchronously; the loop must NOT surface
  // it — the retry waits out the backoff on the event loop.
  QCOMPARE(deliveries, 0);
  QCOMPARE(m_requests.size(), 1);

  QTRY_COMPARE_WITH_TIMEOUT(deliveries, 1, 10000);
  // Exactly two dispatches (initial + one retry), one delivered result.
  QCOMPARE(m_requests.size(), 2);
  // Both attempts re-issued the SAME idempotent request.
  QCOMPARE(m_requests.at(1).url, m_requests.at(0).url);
  QCOMPARE(delivered.size(), 2);
}

void TestProviderOrchestration::retry_transient503Persisting_boundedAttemptsThenSurfacesError() {
  respondWithSequence({transient503Error()}); // every attempt answers 503
  MusicBrainzProvider provider;

  int deliveries = 0;
  ErrorUtils::ErrorContext surfaced;
  provider.lookup(QStringLiteral("Album A"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    ++deliveries;
                    QVERIFY(result.isError());
                    surfaced = result.error();
                  });

  QCOMPARE(deliveries, 0);
  // Backoff between attempts is 500ms then 1000ms (RetryPolicy defaults);
  // give the two waits generous headroom.
  QTRY_COMPARE_WITH_TIMEOUT(deliveries, 1, 15000);
  // Bounded: initial attempt + kDefaultMaxRetries retries, then the last
  // error surfaces exactly once.
  QCOMPARE(m_requests.size(), 1 + Scraper::RetryPolicy::kDefaultMaxRetries);
  QCOMPARE(surfaced.httpStatus, 503);
}

void TestProviderOrchestration::retry_requestQueueCleared_isNotRetried() {
  respondWithSequence({queueClearedError(), MB_SEARCH_JSON});
  MusicBrainzProvider provider;

  int deliveries = 0;
  ErrorUtils::ErrorContext surfaced;
  provider.lookup(QStringLiteral("Album A"),
                  [&](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
                    ++deliveries;
                    QVERIFY(result.isError());
                    surfaced = result.error();
                  });

  // A deliberate local cancel surfaces synchronously — no retry timer may be
  // scheduled for a run the user killed (Kartend-jjyst.2), even though a
  // "success" is queued up behind it.
  QCOMPARE(deliveries, 1);
  QCOMPARE(m_requests.size(), 1);
  QCOMPARE(surfaced.code, ErrorUtils::ErrorCode::RequestQueueCleared);

  // And nothing fires later either: pump the event loop past the first-retry
  // backoff window and re-assert.
  QTest::qWait(700);
  QCOMPARE(deliveries, 1);
  QCOMPARE(m_requests.size(), 1);
}

QTEST_MAIN(TestProviderOrchestration)
#include "test_providerorchestration.moc"
