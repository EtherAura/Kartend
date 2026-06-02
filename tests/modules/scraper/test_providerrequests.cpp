// Request-construction tests for the API-backed providers (TMDB, Open
// Library, MusicBrainz) and the generic WebSearchProvider. These provider
// classes shape the outbound search URL; previously they were only touched
// indirectly through test_metadataproviderregistry. No network: every
// assertion is on the QUrl that searchUrl() builds, plus the static provider
// metadata (id / categories / capabilities). (Kartend-3yyr1)
//
// The API lookup() request URLs + auth headers are built inside the async
// network path and aren't reachable without injecting the HTTP client, so
// they're out of scope here — searchUrl() is the deterministic, public
// request-construction surface.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUrl>

#include "metadataprovider.h"
#include "musicbrainzprovider.h"
#include "openlibraryprovider.h"
#include "tmdbprovider.h"
#include "websearchprovider.h"

using Capability = MetadataProvider::Capability;

class TestProviderRequests : public QObject {
  Q_OBJECT
private slots:
  void tmdb_searchUrl_buildsEncodedQuery();
  void tmdb_searchUrl_trimsBeforeEncoding();
  void tmdb_searchUrl_blankQueryIsInvalid();
  void tmdb_metadata();

  void openLibrary_searchUrl_buildsEncodedQuery();
  void openLibrary_searchUrl_blankQueryIsInvalid();
  void openLibrary_metadata();

  void musicBrainz_searchUrl_buildsEncodedQueryWithType();
  void musicBrainz_searchUrl_blankQueryIsInvalid();
  void musicBrainz_metadata();

  void webSearch_searchUrl_substitutesTemplate();
  void webSearch_searchUrl_emptyTemplateIsInvalid();
  void webSearch_searchUrl_blankQueryIsInvalid();
  void webSearch_metadata_passesThroughAndIsWebSearchOnly();

  void allProviders_specialCharactersArePercentEncoded();
};

namespace {
// TmdbProvider needs a GeneralSettings accessor; searchUrl() never reads it,
// so a null-returning accessor is sufficient for these tests.
TmdbProvider makeTmdb() {
  return TmdbProvider([]() -> const GeneralSettings * { return nullptr; });
}
} // namespace

void TestProviderRequests::tmdb_searchUrl_buildsEncodedQuery() {
  QCOMPARE(makeTmdb().searchUrl(QStringLiteral("Star Wars")),
           QUrl(QStringLiteral("https://www.themoviedb.org/search?query=Star%20Wars")));
}

void TestProviderRequests::tmdb_searchUrl_trimsBeforeEncoding() {
  // Leading/trailing whitespace is trimmed before encoding, so the built URL
  // matches the untrimmed call and carries no stray %20 padding.
  QCOMPARE(makeTmdb().searchUrl(QStringLiteral("  Star Wars  ")),
           QUrl(QStringLiteral("https://www.themoviedb.org/search?query=Star%20Wars")));
}

void TestProviderRequests::tmdb_searchUrl_blankQueryIsInvalid() {
  QVERIFY(!makeTmdb().searchUrl(QString()).isValid());
  QVERIFY(!makeTmdb().searchUrl(QStringLiteral("   ")).isValid());
}

void TestProviderRequests::tmdb_metadata() {
  const TmdbProvider provider = makeTmdb();
  QCOMPARE(provider.id(), QStringLiteral("tmdb"));
  QCOMPARE(provider.categories(), QStringList{QStringLiteral("video")});
  const auto caps = provider.capabilities();
  QVERIFY(caps.testFlag(Capability::WebSearch));
  QVERIFY(caps.testFlag(Capability::MetadataLookup));
  QVERIFY(caps.testFlag(Capability::MediaFetch));
}

void TestProviderRequests::openLibrary_searchUrl_buildsEncodedQuery() {
  QCOMPARE(OpenLibraryProvider().searchUrl(QStringLiteral("Dune Messiah")),
           QUrl(QStringLiteral("https://openlibrary.org/search?q=Dune%20Messiah")));
}

void TestProviderRequests::openLibrary_searchUrl_blankQueryIsInvalid() {
  QVERIFY(!OpenLibraryProvider().searchUrl(QString()).isValid());
  QVERIFY(!OpenLibraryProvider().searchUrl(QStringLiteral("  ")).isValid());
}

void TestProviderRequests::openLibrary_metadata() {
  const OpenLibraryProvider provider;
  QCOMPARE(provider.id(), QStringLiteral("openlibrary"));
  QCOMPARE(provider.categories(), QStringList{QStringLiteral("reference")});
  QVERIFY(provider.capabilities().testFlag(Capability::WebSearch));
}

void TestProviderRequests::musicBrainz_searchUrl_buildsEncodedQueryWithType() {
  // MusicBrainz pins &type=release on the public search page.
  QCOMPARE(MusicBrainzProvider().searchUrl(QStringLiteral("Daft Punk")),
           QUrl(QStringLiteral("https://musicbrainz.org/search?query=Daft%20Punk&type=release")));
}

void TestProviderRequests::musicBrainz_searchUrl_blankQueryIsInvalid() {
  QVERIFY(!MusicBrainzProvider().searchUrl(QString()).isValid());
  QVERIFY(!MusicBrainzProvider().searchUrl(QStringLiteral("\t ")).isValid());
}

void TestProviderRequests::musicBrainz_metadata() {
  const MusicBrainzProvider provider;
  QCOMPARE(provider.id(), QStringLiteral("musicbrainz"));
  QCOMPARE(provider.categories(), QStringList{QStringLiteral("audio")});
  QVERIFY(provider.capabilities().testFlag(Capability::WebSearch));
}

void TestProviderRequests::webSearch_searchUrl_substitutesTemplate() {
  const WebSearchProvider provider(QStringLiteral("mobygames"), QStringLiteral("MobyGames"),
                                   {QStringLiteral("video")},
                                   QStringLiteral("https://www.mobygames.com/search/?q=%1"));
  QCOMPARE(provider.searchUrl(QStringLiteral("Sonic 2")),
           QUrl(QStringLiteral("https://www.mobygames.com/search/?q=Sonic%202")));
}

void TestProviderRequests::webSearch_searchUrl_emptyTemplateIsInvalid() {
  const WebSearchProvider provider(QStringLiteral("x"), QStringLiteral("X"),
                                   {QStringLiteral("video")}, QString());
  QVERIFY(!provider.searchUrl(QStringLiteral("anything")).isValid());
}

void TestProviderRequests::webSearch_searchUrl_blankQueryIsInvalid() {
  const WebSearchProvider provider(QStringLiteral("x"), QStringLiteral("X"),
                                   {QStringLiteral("video")},
                                   QStringLiteral("https://example.com/?q=%1"));
  QVERIFY(!provider.searchUrl(QString()).isValid());
  QVERIFY(!provider.searchUrl(QStringLiteral("   ")).isValid());
}

void TestProviderRequests::webSearch_metadata_passesThroughAndIsWebSearchOnly() {
  const WebSearchProvider provider(QStringLiteral("mobygames"), QStringLiteral("MobyGames"),
                                   {QStringLiteral("video"), QStringLiteral("games")},
                                   QStringLiteral("https://www.mobygames.com/search/?q=%1"));
  QCOMPARE(provider.id(), QStringLiteral("mobygames"));
  QCOMPARE(provider.displayName(), QStringLiteral("MobyGames"));
  QCOMPARE(provider.categories(), (QStringList{QStringLiteral("video"), QStringLiteral("games")}));
  // The generic web-search provider is Stage-1 only.
  QCOMPARE(provider.capabilities(), Capability::WebSearch);
}

void TestProviderRequests::allProviders_specialCharactersArePercentEncoded() {
  // Ampersand + space must be percent-encoded so they don't fracture the
  // query string. QUrl::toPercentEncoding renders ' ' as %20 and '&' as %26.
  const QString query = QStringLiteral("Mario & Luigi");
  const QString encoded = QStringLiteral("Mario%20%26%20Luigi");
  QVERIFY(makeTmdb().searchUrl(query).toString(QUrl::FullyEncoded).contains(encoded));
  QVERIFY(OpenLibraryProvider().searchUrl(query).toString(QUrl::FullyEncoded).contains(encoded));
  QVERIFY(MusicBrainzProvider().searchUrl(query).toString(QUrl::FullyEncoded).contains(encoded));
}

QTEST_MAIN(TestProviderRequests)
#include "test_providerrequests.moc"
