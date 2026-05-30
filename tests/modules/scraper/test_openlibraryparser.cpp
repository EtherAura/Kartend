// Tests for OpenLibraryParser. Pure JSON → typed parsing — no
// network. Fixtures shaped to match real Open Library API responses
// (sample queries against openlibrary.org/search.json produce
// structures matching the ones below).
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "openlibraryparser.h"

class TestOpenLibraryParser : public QObject {
  Q_OBJECT
private slots:
  void parseSearchResponse_returnsCandidates();
  void parseSearchResponse_buildsAuthorDashTitleDisplayName();
  void parseSearchResponse_strips_works_keyPrefix();
  void parseSearchResponse_subtitleIncludesYearAndPublisher();
  void parseSearchResponse_thumbnailUrlPresentWhenCoverIdSet();
  void parseSearchResponse_skipsEntriesWithoutKey();
  void parseSearchResponse_skipsMalformedWorkKey();
  void parseSearchResponse_emptyDocsArrayIsSuccess();
  void parseSearchResponse_malformedJsonReturnsError();
  void parseDetailResponse_extractsTitleSubjectsAndKey();
  void parseDetailResponse_descriptionAsObjectIsExtracted();
  void parseDetailResponse_includesCoverMediaWhenCoversListPresent();
  void appendCoverUrls_skipsZeroOrNegative();
  void extractIsbnFromText_findsIsbn13WithDashes();
  void extractIsbnFromText_findsIsbn10WithoutPrefix();
  void extractIsbnFromText_returnsEmptyForNoMatch();
  void extractIsbnFromText_isbn13PreferredOverIsbn10WhenBothPresent();
};

namespace {

const QByteArray SEARCH_FIXTURE = R"({
  "numFound": 2,
  "start": 0,
  "docs": [
    {
      "key": "/works/OL12345W",
      "title": "Foundation",
      "author_name": ["Isaac Asimov"],
      "first_publish_year": 1951,
      "publisher": ["Gnome Press", "Spectra"],
      "isbn": ["0553293354", "9780553293357"],
      "cover_i": 8765432,
      "subject": ["Science Fiction"]
    },
    {
      "key": "/works/OL67890W",
      "title": "Untitled Author",
      "first_publish_year": 0,
      "publisher": ["Solo Press"]
    },
    {
      "key": "",
      "title": "No key — should be skipped"
    }
  ]
})";

const QByteArray DETAIL_FIXTURE_FLAT = R"({
  "key": "/works/OL12345W",
  "title": "Foundation",
  "description": "First book in the series.",
  "subjects": ["Science Fiction", "Adventure"],
  "covers": [8765432, 999],
  "first_publish_date": "1951-05-01"
})";

const QByteArray DETAIL_FIXTURE_OBJECT_DESCRIPTION = R"({
  "key": "/works/OL12345W",
  "title": "Foundation",
  "description": {"type": "/type/text", "value": "Object-form description"},
  "subjects": []
})";

} // namespace

void TestOpenLibraryParser::parseSearchResponse_returnsCandidates() {
  auto result = OpenLibraryParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  // Three docs in the fixture; one has empty key so it's dropped.
  QCOMPARE(result.value().size(), 2);
}

void TestOpenLibraryParser::parseSearchResponse_buildsAuthorDashTitleDisplayName() {
  auto result = OpenLibraryParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value()[0].displayName, QStringLiteral("Isaac Asimov — Foundation"));
  // Untitled author falls back to title only.
  QCOMPARE(result.value()[1].displayName, QStringLiteral("Untitled Author"));
}

void TestOpenLibraryParser::parseSearchResponse_strips_works_keyPrefix() {
  auto result = OpenLibraryParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value()[0].providerSpecificId, QStringLiteral("OL12345W"));
  QCOMPARE(result.value()[1].providerSpecificId, QStringLiteral("OL67890W"));
}

void TestOpenLibraryParser::parseSearchResponse_subtitleIncludesYearAndPublisher() {
  auto result = OpenLibraryParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value()[0].subtitle, QStringLiteral("1951 · Gnome Press"));
}

void TestOpenLibraryParser::parseSearchResponse_thumbnailUrlPresentWhenCoverIdSet() {
  auto result = OpenLibraryParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  const auto thumb = result.value()[0].thumbnailUrl.toString();
  QVERIFY2(thumb.contains("covers.openlibrary.org/b/id/8765432"), qPrintable(thumb));
  // The second candidate has no cover_i — thumbnail URL should be invalid.
  QVERIFY(!result.value()[1].thumbnailUrl.isValid() || result.value()[1].thumbnailUrl.isEmpty());
}

void TestOpenLibraryParser::parseSearchResponse_skipsEntriesWithoutKey() {
  auto result = OpenLibraryParser::parseSearchResponse(SEARCH_FIXTURE);
  QVERIFY(result.isOk());
  // Three entries in fixture; empty-key one dropped → 2 left.
  QCOMPARE(result.value().size(), 2);
}

void TestOpenLibraryParser::parseSearchResponse_skipsMalformedWorkKey() {
  // A key that isn't OL<digits>W after the /works/ strip (here a path-traversal
  // attempt) must be dropped, never interpolated into the detail URL path
  // (Kartend-tjyh).
  const QByteArray fixture = R"({
    "docs": [
      {"key": "/works/../../evil", "title": "Traversal"},
      {"key": "/works/OL12345W", "title": "Valid"}
    ]
  })";
  auto result = OpenLibraryParser::parseSearchResponse(fixture);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0].providerSpecificId, QStringLiteral("OL12345W"));
}

void TestOpenLibraryParser::parseSearchResponse_emptyDocsArrayIsSuccess() {
  auto result =
      OpenLibraryParser::parseSearchResponse(QByteArray(R"({"numFound":0,"start":0,"docs":[]})"));
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 0);
}

void TestOpenLibraryParser::parseSearchResponse_malformedJsonReturnsError() {
  auto result = OpenLibraryParser::parseSearchResponse(QByteArray("{not json"));
  QVERIFY(result.isError());
}

void TestOpenLibraryParser::parseDetailResponse_extractsTitleSubjectsAndKey() {
  auto result =
      OpenLibraryParser::parseDetailResponse(DETAIL_FIXTURE_FLAT, QStringLiteral("OL12345W"));
  QVERIFY(result.isOk());
  const auto item = result.value();
  QCOMPARE(item.title, QStringLiteral("Foundation"));
  QCOMPARE(item.description, QStringLiteral("First book in the series."));
  QCOMPARE(item.genre, QStringLiteral("Science Fiction, Adventure"));
  QCOMPARE(item.releaseDate, QStringLiteral("1951-05-01"));
  QCOMPARE(item.customFields.value("openlibrary_key"), QStringLiteral("OL12345W"));
  QCOMPARE(item.sourceProviderId, QStringLiteral("openlibrary"));
}

void TestOpenLibraryParser::parseDetailResponse_descriptionAsObjectIsExtracted() {
  auto result = OpenLibraryParser::parseDetailResponse(DETAIL_FIXTURE_OBJECT_DESCRIPTION,
                                                       QStringLiteral("OL12345W"));
  QVERIFY(result.isOk());
  QCOMPARE(result.value().description, QStringLiteral("Object-form description"));
}

void TestOpenLibraryParser::parseDetailResponse_includesCoverMediaWhenCoversListPresent() {
  auto result =
      OpenLibraryParser::parseDetailResponse(DETAIL_FIXTURE_FLAT, QStringLiteral("OL12345W"));
  QVERIFY(result.isOk());
  const auto item = result.value();
  QCOMPARE(item.media.size(), 2);
  // First media is the medium cover ("front" type → primary slot).
  QCOMPARE(item.media[0].type, QStringLiteral("front"));
  QVERIFY(item.media[0].url.toString().contains("8765432-M.jpg"));
  // Second is the large cover under fanart-type slot.
  QCOMPARE(item.media[1].type, QStringLiteral("fanart"));
  QVERIFY(item.media[1].url.toString().contains("8765432-L.jpg"));
}

void TestOpenLibraryParser::appendCoverUrls_skipsZeroOrNegative() {
  Scraper::ScrapedItem item;
  OpenLibraryParser::appendCoverUrls(item, 0);
  OpenLibraryParser::appendCoverUrls(item, -5);
  QCOMPARE(item.media.size(), 0);
  OpenLibraryParser::appendCoverUrls(item, 12345);
  QCOMPARE(item.media.size(), 2);
}

void TestOpenLibraryParser::extractIsbnFromText_findsIsbn13WithDashes() {
  // Filename style with dashes inside the ISBN.
  const QString fname = QStringLiteral("978-0-451-52493-5 - Nineteen Eighty-Four.epub");
  QCOMPARE(OpenLibraryParser::extractIsbnFromText(fname), QStringLiteral("9780451524935"));
}

void TestOpenLibraryParser::extractIsbnFromText_findsIsbn10WithoutPrefix() {
  QCOMPARE(OpenLibraryParser::extractIsbnFromText(QStringLiteral("0553293354.epub")),
           QStringLiteral("0553293354"));
}

void TestOpenLibraryParser::extractIsbnFromText_returnsEmptyForNoMatch() {
  QCOMPARE(OpenLibraryParser::extractIsbnFromText(QStringLiteral("Just a title.epub")), QString());
}

void TestOpenLibraryParser::extractIsbnFromText_isbn13PreferredOverIsbn10WhenBothPresent() {
  // 13 is a strict superset of 10 in length terms; the extractor must
  // try ISBN-13 first or it'd return the first 10 digits of the 13.
  const QString text = QStringLiteral("ISBN 9780451524935 (also 0553293354)");
  QCOMPARE(OpenLibraryParser::extractIsbnFromText(text), QStringLiteral("9780451524935"));
}

QTEST_MAIN(TestOpenLibraryParser)
#include "test_openlibraryparser.moc"
