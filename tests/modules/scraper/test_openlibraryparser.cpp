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
  void parseSearchResponse_boundsCandidateCountFromAnOversizedArray();
  void parseDetailResponse_boundsAndDedupesAJoinedField();
  void parseDetailResponse_extractsTitleSubjectsAndKey();
  void parseDetailResponse_descriptionAsObjectIsExtracted();
  void parseDetailResponse_includesCoverMediaWhenCoversListPresent();
  void appendCoverUrls_skipsZeroOrNegative();
  void extractIsbnFromText_findsIsbn13WithDashes();
  void extractIsbnFromText_findsIsbn10WithoutPrefix();
  void extractIsbnFromText_returnsEmptyForNoMatch();
  void extractIsbnFromText_isbn13PreferredOverIsbn10WhenBothPresent();
  void extractIsbnFromText_rejectsInvalidTenDigitRun();
  void extractIsbnFromText_skipsInvalidRunAndFindsValidIsbn();
  void extractIsbnFromText_acceptsIsbn10WithXCheckDigit();
  void extractIsbnFromText_rejectsInvalidIsbn13();
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

// Kartend-v3u04. The provider asks for 10 results; a compromised or MITM'd
// one can answer with an array as long as the body cap allows, and the parser
// used to reserve() straight from that length. The cap is far above anything
// real, so this drives it from above with a synthetic array — what matters is
// that the count stops climbing with the response, not the exact value.
void TestOpenLibraryParser::parseSearchResponse_boundsCandidateCountFromAnOversizedArray() {
  QByteArray json = QByteArrayLiteral("{\"docs\":[");
  constexpr int kEntries = 600; // comfortably past the 256 cap
  for (int i = 0; i < kEntries; ++i) {
    if (i > 0) {
      json.append(',');
    }
    json.append("{\"key\":\"/works/OL");
    json.append(QByteArray::number(i));
    json.append("W\",\"title\":\"T");
    json.append(QByteArray::number(i));
    json.append("\"}");
  }
  json.append("]}");

  auto result = OpenLibraryParser::parseSearchResponse(json);
  QVERIFY(result.isOk());
  QVERIFY2(result.value().size() < kEntries,
           "candidate count must not track the response's array length");
  QCOMPARE(result.value().size(), 256);
  // Truncation takes the head, so the results a user would actually look at
  // are the ones the provider ranked first.
  QCOMPARE(result.value().first().providerSpecificId, QStringLiteral("OL0W"));
}

// The joined fields deduped with a linear QStringList::contains inside the
// loop — quadratic over an array the response sizes. Order still has to
// survive (primary subject first), and duplicates still have to collapse.
void TestOpenLibraryParser::parseDetailResponse_boundsAndDedupesAJoinedField() {
  QByteArray json = QByteArrayLiteral("{\"title\":\"Bounded\",\"subjects\":[");
  // 40 distinct values, each repeated 5x and interleaved, so a dedup that
  // merely truncated would produce a different answer than one that dedupes.
  for (int repeat = 0; repeat < 5; ++repeat) {
    for (int i = 0; i < 40; ++i) {
      if (repeat > 0 || i > 0) {
        json.append(',');
      }
      json.append("\"Subject ");
      json.append(QByteArray::number(i));
      json.append('"');
    }
  }
  json.append("]}");

  auto result = OpenLibraryParser::parseDetailResponse(json, QStringLiteral("OL1W"));
  QVERIFY(result.isOk());
  const QStringList genres = result.value().genre.split(QStringLiteral(", "));
  QCOMPARE(genres.size(), 40); // deduped, not truncated at the raw 200
  QCOMPARE(genres.first(), QStringLiteral("Subject 0"));
  QCOMPARE(genres.last(), QStringLiteral("Subject 39"));
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

void TestOpenLibraryParser::extractIsbnFromText_rejectsInvalidTenDigitRun() {
  // A bare 10-digit run that isn't a valid ISBN-10 (its check digit doesn't
  // compute) must NOT be treated as an ISBN — that fed a bogus isbn= query and
  // auto-applied wrong matches in batch (Kartend-tipud).
  QCOMPARE(OpenLibraryParser::extractIsbnFromText(QStringLiteral("Episode 1234567890.mkv")),
           QString());
}

void TestOpenLibraryParser::extractIsbnFromText_skipsInvalidRunAndFindsValidIsbn() {
  // An invalid run earlier in the string must not shadow a real ISBN later.
  const QString text = QStringLiteral("id 1234567890 isbn 0553293354.epub");
  QCOMPARE(OpenLibraryParser::extractIsbnFromText(text), QStringLiteral("0553293354"));
}

void TestOpenLibraryParser::extractIsbnFromText_acceptsIsbn10WithXCheckDigit() {
  // A valid ISBN-10 whose check digit is 'X' (=10) must be captured + validated.
  QCOMPARE(OpenLibraryParser::extractIsbnFromText(QStringLiteral("020161622X.epub")),
           QStringLiteral("020161622X"));
}

void TestOpenLibraryParser::extractIsbnFromText_rejectsInvalidIsbn13() {
  // A 978/979-prefixed 13-digit run with a wrong check digit is rejected, not
  // queried.
  QCOMPARE(OpenLibraryParser::extractIsbnFromText(QStringLiteral("9780451524936")), QString());
}

QTEST_MAIN(TestOpenLibraryParser)
#include "test_openlibraryparser.moc"
